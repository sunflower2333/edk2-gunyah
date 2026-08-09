# edk2-gunyah

UEFI Firmware for crosvm + gunyah virtualization platform

## Quick build

Install the arm64 cross compiler toolchain (e.g. `aarch64-linux-gnu-gcc`) and iasl(e.g. `acpica-tools`).

- Build **Release** Variant:
```bash
./build.sh
```

- Build **Debug** Variant:
```bash
EDK2_TARGET=DEBUG ./build.sh
```

Artifacts named `edk2-gunyah.fd`.

## Secure Boot

The firmware is built with **Secure Boot support enabled by default** and
**enforced ON automatically on the first boot of a clean variable store**
through the GunyahPkg-supplied `SecureBootProvisioningDxe` driver (see the
"Boot-time configuration" subsection below). It ships with **Microsoft's
official SecureBoot keys** embedded as firmware defaults
(sourced from [microsoft/secureboot_objects](https://github.com/microsoft/secureboot_objects)):

| Variable | Embedded certificate                                   | Source path (microsoft/secureboot_objects) |
|----------|--------------------------------------------------------|--------------------------------------------|
| PK       | Microsoft Windows OEM Devices PK                       | `PreSignedObjects/PK/Certificate/WindowsOEMDevicesPK.der` |
| KEK      | Microsoft Corporation KEK CA 2011                     | `PreSignedObjects/KEK/Certificates/MicCorKEKCA2011_2011-06-24.der` |
| db       | Microsoft Windows Production PCA 2011                  | `PreSignedObjects/DB/Certificates/MicWinProPCA2011_2011-10-19.der` |
| db       | Microsoft Corporation UEFI CA 2011                    | `PreSignedObjects/DB/Certificates/MicCorUEFCA2011_2011-06-27.der` |
| dbx      | Microsoft signed revocation list (arm64)               | `PostSignedObjects/DBX/arm64/DBXUpdate.bin` |

### Boot-time configuration (no rebuild needed)

Secure Boot is **enforced automatically on the first boot of a clean variable
store**: the bundled `SecureBootProvisioningDxe` driver reproduces the
"Reset to default keys" menu action and enrolls the embedded Microsoft keys
into the real PK/KEK/db/dbx/dbt authenticated variables, then flips
**`Attempt Secure Boot`** to ON. So you should see **Secure Boot ON** in the
UEFI Setup menu the first time you boot — no manual provisioning required.

You still have full control afterward from the UEFI Setup menu:

1. Enter UEFI Setup (via the platform's hotkey entry when configured).
2. Navigate to `Device Manager` → `Secure Boot Configuration`.
3. Toggle **`Attempt Secure Boot`** (checkbox) to enable/disable enforcement.
4. Select **`Secure Boot Mode`**:
   - **Standard**: locking variables (PK/KEK/db/dbx) to the platform defaults
     provisioned automatically on first boot via `SecureBootDefaultKeysDxe` +
     `SecureBootProvisioningDxe`.
   - **Custom**: opens per-database (PK/KEK/db/dbx/dbt) "Enroll from file" /
     "Delete" forms, allowing the user to swap any key in.
5. Pick **`Reset to defaults`** to re-apply the embedded Microsoft keys

   (this is the same operation `SecureBootProvisioningDxe` already performs
   automatically on a clean variable store, so you only need to click it
   manually if you've cleared or replaced the keys and want the defaults
   back).

Once you (or the auto-provisioning driver) install a Platform Key (PK), the
driver becomes a **no-op on every subsequent boot**, so re-enrolling your own
custom keys from the menu always wins.

If you want to build a firmware image without Secure Boot support at all, pass
the flag explicitly:

```bash
./build.sh -D SECURE_BOOT_ENABLE=FALSE
```

Building **Release** vs **Debug** is independent of Secure Boot:

```bash
EDK2_TARGET=DEBUG ./build.sh                       # DEBUG + Secure Boot (default)
EDK2_TARGET=DEBUG ./build.sh -DSECURE_BOOT_ENABLE=FALSE   # DEBUG without Secure Boot
```

## Running

```bash
/apex/com.android.virt/bin/crosvm run \
    --mem 4096 \
    --cpus 1 \
    --protected-vm-without-firmware \
    --no-balloon \
    --disable-sandbox \
    --block disk.img,lock=false \
    edk2-gunyah.fd
```

> `edk2-gunyah.fd` is the firmware that crosvm loads as the kernel.
>
> There is a separate **non-volatile variable store** image,
> `edk2-gunyah.vars.fd` (768 KB = 3 × 256 KB, generated in the build output);
> it's the place where UEFI variables (BootOrder, boot entries, the Secure Boot
> PK/KEK/db/dbx you enroll from the Setup menu, BootNext, etc.) would normally
> live.
>
> **On the current Droid-VM / crosvm fork running on AArch64, the variable store
> is emulated in RAM**, not persisted to a file. Although the fork accepts a
> `--pflash path=edk2-gunyah.vars.fd,block_size=4096` CLI flag, on AArch64 that
> flag is **silently dropped** — `components.pflash_image` is only consumed by
> the x86_64 build path (`x86_64/src/lib.rs::setup_pflash`), never by
> `aarch64/src/lib.rs::build_vm`, so no MMIO window and no DT `cfi-flash` node
> are emitted. With the firmware's `VirtNorFlashDxe` left without any NOR flash
> instance, no FVB protocol is installed and `VariableRuntimeDxe` (and the DXE
> drivers that depend on the Variable arch protocol — Bds, Capsule,
> MonotonicCounter, RTC) can never be dispatched, which trips
> `DxeMain.c(578) ASSERT_EFI_ERROR (Not Found)`.
>
> So the firmware is therefore built with `PcdEmuVariableNvModeEnable = TRUE`
> by default (`DEFINE EMU_VARIABLE_NV_MODE = TRUE` in `GunyahKernel.dsc`):
> variables work during a single boot but are **not persisted across reboots**.
> The `--pflash` flag in the crosvm invocation below is therefore optional —
> it's still listed for forward compatibility, since a future crosvm fork that
> actually wires AArch64 pflash support would let you switch the build back
> with `-D EMU_VARIABLE_NV_MODE=FALSE` and get real persistent variables:
>
> ```bash
> EDK2_TARGET=DEBUG ./build.sh -D EMU_VARIABLE_NV_MODE=FALSE
> ```
>
> Because the in-RAM store is reset to the freshly-built `GUNYAH_VARS.fd`
> content on each cold boot, and because `SecureBootProvisioningDxe` runs on
> every clean store, **Secure Boot is reprovisioned ON by default on every
> boot** — that's why no manual menu action is needed (see the "Secure Boot"
> section above).
>
> To explicitly reset the UEFI variables (incl. Secure Boot keys) — relevant
> only in persistent `EMU_VARIABLE_NV_MODE=FALSE` mode — replace
> `edk2-gunyah.vars.fd` with a freshly built one:
>
> ```bash
> cp build/Build/GunyahKernel-AARCH64/${EDK2_TARGET}_GCC/FV/GUNYAH_VARS.fd \
>    edk2-gunyah.vars.fd
> ```
> The next boot re-provisions `PKDefault`/`KEKDefault`/`dbDefault`/`dbxDefault`
> via `SecureBootDefaultKeysDxe`, and `SecureBootProvisioningDxe` then rolls
> those into the real PK/KEK/db/dbx/dbt and flips `Attempt Secure Boot` to ON
> — exactly as on the very first boot.

## Tested platforms

### SoC

- **Qualcomm Snapdragon 8 Elite Gen 5** (SM8850)
- **Qualcomm Snapdragon 8 Elite** (SM8750)

### Devices

- **Xiaomi Redmi K90 Pro Max** (codename "myron", with SM8850)
- **Lenovo Y700 Gen4** (codename "tb322fc", with SM8750)

## Differents from ArmVirt

This repo is based on ArmVirtPkg, with some patch for crosvm + gunyah.

- ARM64 boot header

  crosvm requires text_offset to be 0 KiB (0x0), ArmVirtQemuKernel uses 512 KiB (0x80000).

- Memory Base

  ArmVirt uses 0x0, crosvm + gunyah uses 0x80000000.

- NS16550A Serial UART

  ArmVirt uses the PL011, crosvm uses NS16550A, `PatchedSerialPortLib16550` removed PCI supports.

- ~~PCIe generic CAM~~

  ~~ArmVirt uses `pci-host-ecam-generic`, crosvm uses `pci-host-cam-generic`.~~

  Custom [crosvm](http://github.com/Droid-VM/crosvm) patch to support ECAM mode, your can use `FA` flag to build this firmware with PCIe ECAM mode

- PL030 Real Time Clock

  ArmVirt uses pl031, crosvm uses pl030.

- VirtioLib

  ArmVirt's VirtioLib lack of swiotlb supports.

- VirtioGpu

  crosvm's VirtioGpu wants 2 queues.

- VirtioInput

  crosvm's VirtioInput wants 2 queues.

- VirtioScsi

  crosvm's VirtioScsi need initialize all queues (controlq, eventq, requestq) before setting DRIVER_OK.

  must limit LUNs to 1 (default to 8) to avoid crosvm returning duplicate disk handles for the same target.

- VirtioFs

  add missing highpri ring

  fix fuse read MaxWrite handle

- SMBIOS

  ArmVirt get SMBIOS table from QEMU, crosvm doesn't have SMBIOS.

- PlatformBootManagerLibLight

  Add missing VirtioInput supports.

- FdtHwInfoParserLib

  Hack for disable PCIe IOMMU (unavailable)

## License

See the repository `LICENSE` file at the project root for licensing details.
