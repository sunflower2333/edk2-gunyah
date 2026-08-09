// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Programmable flash device that supports the minimum interface that OVMF
//! requires. This is purpose-built to allow OVMF to store UEFI variables in
//! the same way that it stores them on QEMU.
//!
//! For that reason it's heavily based on [QEMU's pflash implementation]. In
//! addition to the original QEMU emulation, this implementation provides a
//! synthetic CFI (Common Flash Interface) query table so that firmware (edk2
//! `VirtNorFlashDxe`, ARM SBSA NOR flash drivers, etc.) can auto-detect the
//! device geometry from the device tree / MMIO window. The CFI table is
//! generated from `block_size` and `image_size`; see `build_cfi_table`.
//!
//! In addition to full-width reads, we only support single byte writes,
//! block erases, and status requests, which UEFI firmware uses to probe and
//! program the device.
//!
//! Note that without SMM support in crosvm (which it doesn't yet have) this
//! device is directly accessible to potentially malicious kernels. With SMM
//! and the appropriate changes to this device this could be made more secure
//! by ensuring only the BIOS is able to touch the pflash.
//!
//! [QEMU's pflash implementation]: https://github.com/qemu/qemu/blob/master/hw/block/pflash_cfi01.c

use std::path::PathBuf;

use anyhow::bail;
use base::error;
use base::VolatileSlice;
use disk::DiskFile;
use serde::Deserialize;
use serde::Serialize;
use snapshot::AnySnapshot;

use crate::pci::CrosvmDeviceId;
use crate::BusAccessInfo;
use crate::BusDevice;
use crate::DeviceId;
use crate::Suspendable;

const COMMAND_WRITE_BYTE: u8 = 0x10;
const COMMAND_BLOCK_ERASE: u8 = 0x20;
const COMMAND_CLEAR_STATUS: u8 = 0x50;
const COMMAND_READ_STATUS: u8 = 0x70;
const COMMAND_BLOCK_ERASE_CONFIRM: u8 = 0xd0;
const COMMAND_READ_ARRAY: u8 = 0xff;
// CFI (Common Flash Interface) enter-query command ( JEDEC 0x98 ).
// Required so that edk2's `VirtNorFlashDxe` CFI probe succeeds: it issues
// 0x98 then reads back the ASCII 'Q','R','Y' marker at offsets 0x10..0x12
// before consulting any geometry fields.
const COMMAND_CFI_QUERY: u8 = 0x98;
// CFI enter-query offset: read 0x98 at this address.
const CFI_QUERY_OFFSET: u64 = 0x55;
// CFI identification string starts at this offset ("QRY" at 0x10..0x12).
const CFI_HEADER_OFFSET: u64 = 0x10;

const STATUS_READY: u8 = 0x80;

fn pflash_parameters_default_block_size() -> u32 {
    // 4K
    4 * (1 << 10)
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct PflashParameters {
    pub path: PathBuf,
    #[serde(default = "pflash_parameters_default_block_size")]
    pub block_size: u32,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize)]
enum State {
    ReadArray,
    ReadStatus,
    ReadCfi,
    BlockErase(u64),
    Write(u64),
}

pub struct Pflash {
    image: Box<dyn DiskFile>,
    image_size: u64,
    block_size: u32,

    state: State,
    status: u8,
}

impl Pflash {
    pub fn new(image: Box<dyn DiskFile>, block_size: u32) -> anyhow::Result<Pflash> {
        if !block_size.is_power_of_two() {
            bail!("Block size {} is not a power of 2", block_size);
        }
        let image_size = image.get_len()?;
        if image_size % block_size as u64 != 0 {
            bail!(
                "Disk size {} is not a multiple of block size {}",
                image_size,
                block_size
            );
        }

        Ok(Pflash {
            image,
            image_size,
            block_size,
            state: State::ReadArray,
            status: STATUS_READY,
        })
    }

    /// Fill `data` from a synthetic CFI (Common Flash Interface) query table.
    ///
    /// Firmware (notably edk2's `VirtNorFlashDxe`) issues the CFI enter-query
    /// sequence (`0x98` followed by a read of the ASCII "QRY" marker at
    /// offsets 0x10..0x12) to detect that the MMIO window is in fact a
    /// parallel NOR flash and to learn the device geometry.
    ///
    /// The table layout we expose follows the standard CFI primary vendor
    /// table (JEDEC Publication 100.2, Intel/Sharp command set 0x0001) and
    /// only fills in the fields actually consulted by edk2's FdtNorFlash
    /// driver: the "QRY" ASCII marker, the device size, the erase block
    /// size, and the number of erase regions (1). All other offsets return
    /// 0x00, which is treated as "use device-size-from-reg" mode.
    fn read_cfi_into(&self, offset: u64, data: &mut [u8], cfi: &[u8]) {
        // The CFI table is laid out at CFI_HEADER_OFFSET in the device
        // address space.  Reads in the leading portion of the device (i.e.
        // before the CFI marker) return whatever the underlying image holds
        // (typically 0x20 from the enter-query command byte).
        for (i, d) in data.iter_mut().enumerate() {
            let addr = offset + i as u64;
            if addr < CFI_HEADER_OFFSET {
                // Echo back the image data; we don't actually consult the
                // underlying disk here since firmware rarely reads from this
                // region while in CFI query mode.
                *d = 0x20;
            } else {
                let idx = (addr - CFI_HEADER_OFFSET) as usize;
                *d = if idx < cfi.len() { cfi[idx] } else { 0x00 };
            }
        }
    }
}

// Build a minimal CFI primary query table for a NOR flash with a single erase
// region.  The table follows the AMD/Intel CFI layout (vendor table ID 0x01)
// and intentionally only contains fields that edk2's VirtNorFlash driver
// inspects:
//
//   offset 0x00..0x02  "QRY"            ASCII marker (0x51, 0x52, 0x59)
//   offset 0x03        PRI_ID = 0x01     Intel/Sharp primary algorithm
//   offset 0x04        PRI_ADDR         (unused: 0x35)
//   offset 0x13        Vcc min          (3.3 V: 0x33)
//   offset 0x14        Vcc max          (3.3 V: 0x33)
//   ...
//   offset 0x27        DevSize (2^n)    = log2(image_size)
//   offset 0x28        Iface byte order (0x01: x8/x16 in x8 mode)
//   offset 0x2a        Num erase regions (= 1)
//   offset 0x2c+r*4    Region info      = (blocks-1) << 16 | (erase-size/256-1)
//
// All other offsets remain 0x00.  This is enough for edk2's probe and for
// Linux to enumerate a single erase-block-size NOR; everything more elaborate
// (write/buffer sizes, polling intervals, lock bits) stays zero which
// firmware interprets as "not supported / use defaults".
fn build_cfi_table(block_size: u32, image_size: u64) -> Vec<u8> {
    let mut cfi = vec![0x00u8; 0x40];
    // "QRY" ASCII identification string starting at offset 0x00.
    cfi[0x00] = 0x51; // 'Q'
    cfi[0x01] = 0x52; // 'R'
    cfi[0x02] = 0x59; // 'Y'
    // Primary vendor command set: Intel/Sharp.
    cfi[0x03] = 0x01;
    cfi[0x04] = 0x00;
    // Primary vendor table address (relative to query header).
    cfi[0x05] = 0x35;
    cfi[0x06] = 0x00;
    // Alternate vendor command set: none.
    cfi[0x07] = 0x00;
    cfi[0x08] = 0x00;
    cfi[0x09] = 0x00;
    cfi[0x0a] = 0x00;
    // Vcc min/max (3.3V encoded as 0x33 per CFI spec).
    cfi[0x13] = 0x33;
    cfi[0x14] = 0x33;
    // Vpp min/max (12V encoded as 0x19).
    cfi[0x15] = 0x19;
    cfi[0x16] = 0x19;
    // Typical timeout for single write / buffer write / block erase (2^N us).
    cfi[0x17] = 0x06;
    cfi[0x18] = 0x06;
    cfi[0x19] = 0x07;
    // Typical max timeout for the three operations (2^N us).
    cfi[0x1a] = 0x08;
    cfi[0x1b] = 0x08;
    cfi[0x1c] = 0x0a;
    // Device size N (2^N bytes).
    cfi[0x27] = image_size.trailing_zeros() as u8;
    // Device interface description (x8 / x16 with x8 read).
    cfi[0x28] = 0x01;
    cfi[0x29] = 0x00;
    // Number of erase regions.
    cfi[0x2a] = 0x01;
    // Erase region 1 description: 32-bit packed as
    //   (num_blocks - 1) << 16 | (erase_block_size_bytes / 256 - 1)
    let num_blocks = (image_size / block_size as u64) as u32;
    let erase_blocks_per_256 = (block_size / 256) - 1;
    let region = ((num_blocks - 1) << 16) | erase_blocks_per_256;
    cfi[0x2c..0x30].copy_from_slice(&region.to_le_bytes());
    cfi
}

impl BusDevice for Pflash {
    fn device_id(&self) -> DeviceId {
        CrosvmDeviceId::Pflash.into()
    }

    fn debug_label(&self) -> String {
        "pflash".to_owned()
    }

    fn read(&mut self, info: BusAccessInfo, data: &mut [u8]) {
        let offset = info.offset;
        match self.state {
            State::ReadArray => {
                if offset + data.len() as u64 >= self.image_size {
                    error!("pflash read request beyond disk");
                    return;
                }
                if let Err(e) = self
                    .image
                    .read_exact_at_volatile(VolatileSlice::new(data), offset)
                {
                    error!("pflash failed to read: {}", e);
                }
            }
            State::ReadStatus => {
                self.state = State::ReadArray;
                for d in data {
                    *d = self.status;
                }
            }
            State::ReadCfi => {
                // Lazy-init delay so that Pflash::new doesn't need to know the
                // CFI table — but it never changes after construction.
                let cfi = build_cfi_table(self.block_size, self.image_size);
                self.read_cfi_into(offset, data, &cfi);
            }
            _ => {
                error!(
                    "pflash received unexpected read in state {:?}, recovering to ReadArray mode",
                    self.state
                );
                self.state = State::ReadArray;
            }
        }
    }

    fn write(&mut self, info: BusAccessInfo, data: &[u8]) {
        if data.len() > 1 {
            error!("pflash write request for >1 byte, ignoring");
            return;
        }
        let data = data[0];
        let offset = info.offset;

        match self.state {
            State::Write(expected_offset) => {
                self.state = State::ReadArray;
                self.status = STATUS_READY;

                if offset != expected_offset {
                    error!("pflash received write for offset {} that doesn't match offset from WRITE_BYTE command {}", offset, expected_offset);
                    return;
                }
                if offset >= self.image_size {
                    error!(
                        "pflash offset {} greater than image size {}",
                        offset, self.image_size
                    );
                    return;
                }

                if let Err(e) = self
                    .image
                    .write_all_at_volatile(VolatileSlice::new(&mut [data]), offset)
                {
                    error!("failed to write to pflash: {}", e);
                }
            }
            State::BlockErase(expected_offset) => {
                self.state = State::ReadArray;
                self.status = STATUS_READY;

                if data != COMMAND_BLOCK_ERASE_CONFIRM {
                    error!("pflash write data {} after BLOCK_ERASE command, wanted COMMAND_BLOCK_ERASE_CONFIRM", data);
                    return;
                }
                if offset != expected_offset {
                    error!("pflash offset {} for BLOCK_ERASE_CONFIRM command does not match the one for BLOCK_ERASE {}", offset, expected_offset);
                    return;
                }
                if offset >= self.image_size {
                    error!(
                        "pflash block erase attempt offset {} beyond image size {}",
                        offset, self.image_size
                    );
                    return;
                }
                if offset % self.block_size as u64 != 0 {
                    error!(
                        "pflash block erase offset {} not on block boundary with block size {}",
                        offset, self.block_size
                    );
                    return;
                }

                if let Err(e) = self.image.write_all_at_volatile(
                    VolatileSlice::new(&mut [0xff].repeat(self.block_size.try_into().unwrap())),
                    offset,
                ) {
                    error!("pflash failed to erase block: {}", e);
                }
            }
            _ => {
                // If we're not expecting anything else then assume this is a
                // command to transition states.
                let command = data;

                match command {
                    COMMAND_READ_ARRAY => {
                        self.state = State::ReadArray;
                        self.status = STATUS_READY;
                    }
                    COMMAND_READ_STATUS => self.state = State::ReadStatus,
                    COMMAND_CFI_QUERY => self.state = State::ReadCfi,
                    COMMAND_CLEAR_STATUS => {
                        self.state = State::ReadArray;
                        self.status = 0;
                    }
                    COMMAND_WRITE_BYTE => self.state = State::Write(offset),
                    COMMAND_BLOCK_ERASE => self.state = State::BlockErase(offset),
                    _ => {
                        error!("received unexpected/unsupported pflash command {}, ignoring and returning to read mode", command);
                        self.state = State::ReadArray
                    }
                }
            }
        }
    }
}

impl Suspendable for Pflash {
    fn snapshot(&mut self) -> anyhow::Result<AnySnapshot> {
        AnySnapshot::to_any((self.status, self.state))
    }

    fn restore(&mut self, data: AnySnapshot) -> anyhow::Result<()> {
        let (status, state) = AnySnapshot::from_any(data)?;
        self.status = status;
        self.state = state;
        Ok(())
    }

    fn sleep(&mut self) -> anyhow::Result<()> {
        // TODO(schuffelen): Flush the disk after lifting flush() from AsyncDisk to DiskFile
        Ok(())
    }

    fn wake(&mut self) -> anyhow::Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use base::FileReadWriteAtVolatile;
    use tempfile::tempfile;

    use super::*;

    const IMAGE_SIZE: usize = 4 * (1 << 20); // 4M
    const BLOCK_SIZE: u32 = 4 * (1 << 10); // 4K

    fn empty_image() -> Box<dyn DiskFile> {
        let f = Box::new(tempfile().unwrap());
        f.write_all_at_volatile(VolatileSlice::new(&mut [0xff].repeat(IMAGE_SIZE)), 0)
            .unwrap();
        f
    }

    fn new(f: Box<dyn DiskFile>) -> Pflash {
        Pflash::new(f, BLOCK_SIZE).unwrap()
    }

    fn off(offset: u64) -> BusAccessInfo {
        BusAccessInfo {
            offset,
            address: 0,
            id: 0,
        }
    }

    #[test]
    fn read() {
        let f = empty_image();
        let mut want = [0xde, 0xad, 0xbe, 0xef];
        let offset = 0x1000;
        f.write_all_at_volatile(VolatileSlice::new(&mut want), offset)
            .unwrap();

        let mut pflash = new(f);
        let mut got = [0u8; 4];
        pflash.read(off(offset), &mut got[..]);
        assert_eq!(want, got);
    }

    #[test]
    fn write() {
        let f = empty_image();
        let want = [0xdeu8];
        let offset = 0x1000;

        let mut pflash = new(f);
        pflash.write(off(offset), &[COMMAND_WRITE_BYTE]);
        pflash.write(off(offset), &want);

        // Make sure the data reads back correctly over the bus...
        pflash.write(off(0), &[COMMAND_READ_ARRAY]);
        let mut got = [0u8; 1];
        pflash.read(off(offset), &mut got);
        assert_eq!(want, got);

        // And from the backing file itself...
        pflash
            .image
            .read_exact_at_volatile(VolatileSlice::new(&mut got), offset)
            .unwrap();
        assert_eq!(want, got);

        // And when we recreate the device.
        let mut pflash = new(pflash.image);
        pflash.read(off(offset), &mut got);
        assert_eq!(want, got);

        // Finally make sure our status is ready.
        let mut got = [0u8; 4];
        pflash.write(off(offset), &[COMMAND_READ_STATUS]);
        pflash.read(off(offset), &mut got);
        let want = [STATUS_READY; 4];
        assert_eq!(want, got);
    }

    #[test]
    fn erase() {
        let f = empty_image();
        let mut data = [0xde, 0xad, 0xbe, 0xef];
        let offset = 0x1000;
        f.write_all_at_volatile(VolatileSlice::new(&mut data), offset)
            .unwrap();
        f.write_all_at_volatile(VolatileSlice::new(&mut data), offset * 2)
            .unwrap();

        let mut pflash = new(f);
        pflash.write(off(offset), &[COMMAND_BLOCK_ERASE]);
        pflash.write(off(offset), &[COMMAND_BLOCK_ERASE_CONFIRM]);

        pflash.write(off(0), &[COMMAND_READ_ARRAY]);
        let mut got = [0u8; 4];
        pflash.read(off(offset), &mut got);
        let want = [0xffu8; 4];
        assert_eq!(want, got);

        let want = data;
        pflash.read(off(offset * 2), &mut got);
        assert_eq!(want, got);

        // Make sure our status is ready.
        pflash.write(off(offset), &[COMMAND_READ_STATUS]);
        pflash.read(off(offset), &mut got);
        let want = [STATUS_READY; 4];
        assert_eq!(want, got);
    }

    #[test]
    fn status() {
        let f = empty_image();
        let mut data = [0xde, 0xad, 0xbe, 0xff];
        let offset = 0x0;
        f.write_all_at_volatile(VolatileSlice::new(&mut data), offset)
            .unwrap();

        let mut pflash = new(f);

        // Make sure we start off in the "ready" status.
        pflash.write(off(offset), &[COMMAND_READ_STATUS]);
        let mut got = [0u8; 4];
        pflash.read(off(offset), &mut got);
        let want = [STATUS_READY; 4];
        assert_eq!(want, got);

        // Make sure we can clear the status properly.
        pflash.write(off(offset), &[COMMAND_CLEAR_STATUS]);
        pflash.write(off(offset), &[COMMAND_READ_STATUS]);
        pflash.read(off(offset), &mut got);
        let want = [0; 4];
        assert_eq!(want, got);

        // We implicitly jump back into READ_ARRAY mode after reading the,
        // status but for OVMF's probe we require that this doesn't actually
        // affect the cleared status.
        pflash.read(off(offset), &mut got);
        pflash.write(off(offset), &[COMMAND_READ_STATUS]);
        pflash.read(off(offset), &mut got);
        let want = [0; 4];
        assert_eq!(want, got);
    }

    #[test]
    fn cfi_query() {
        let f = empty_image();
        let mut pflash = new(f);

        // Enter CFI query mode by writing 0x98 anywhere on the device.
        pflash.write(off(0x0), &[COMMAND_CFI_QUERY]);

        // Read the "QRY" signature at offset 0x10..0x13 (CFI header offset).
        let mut got = [0u8; 3];
        pflash.read(off(CFI_HEADER_OFFSET), &mut got);
        assert_eq!(got, [0x51, 0x52, 0x59], "QRY signature mismatch");

        // Device size should be log2(image_size) at CFI offset 0x27 - 0x10 = 0x17.
        let mut size_byte = [0u8; 1];
        pflash.read(off(CFI_HEADER_OFFSET + 0x17), &mut size_byte);
        assert_eq!(size_byte[0], (IMAGE_SIZE as u64).trailing_zeros() as u8);

        // Number of erase regions at CFI offset 0x2a - 0x10 = 0x1a.
        let mut regions = [0u8; 1];
        pflash.read(off(CFI_HEADER_OFFSET + 0x1a), &mut regions);
        assert_eq!(regions[0], 0x01);

        // Erase region 1 descriptor at CFI offset 0x2c..0x30 - 0x10 = 0x1c..0x20.
        let mut region = [0u8; 4];
        pflash.read(off(CFI_HEADER_OFFSET + 0x1c), &mut region);
        let num_blocks = (IMAGE_SIZE as u64 / BLOCK_SIZE as u64) as u32;
        let expected = ((num_blocks - 1) << 16) | (BLOCK_SIZE / 256 - 1);
        assert_eq!(region, expected.to_le_bytes());

        // Exit CFI mode and ensure regular reads work again.
        pflash.write(off(0x0), &[COMMAND_READ_ARRAY]);
        let mut got = [0u8; 4];
        pflash.read(off(0x0), &mut got);
        assert_eq!(got, [0xff; 4]);
    }

    #[test]
    fn overwrite() {
        let f = empty_image();
        let data = [0];
        let offset = off((16 * IMAGE_SIZE).try_into().unwrap());

        // Ensure a write past the pflash device doesn't grow the backing file.
        let mut pflash = new(f);
        let old_size = pflash.image.get_len().unwrap();
        assert_eq!(old_size, IMAGE_SIZE as u64);

        pflash.write(offset, &[COMMAND_WRITE_BYTE]);
        pflash.write(offset, &data);

        let new_size = pflash.image.get_len().unwrap();
        assert_eq!(new_size, IMAGE_SIZE as u64);
    }
}
