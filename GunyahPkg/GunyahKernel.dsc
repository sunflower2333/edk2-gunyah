#
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
#

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = Gunyah
  PLATFORM_GUID                  = 37d7e986-f7e9-45c2-8067-e371421a626c
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/GunyahKernel-$(ARCH)
  SUPPORTED_ARCHITECTURES        = AARCH64|ARM
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = GunyahPkg/GunyahKernel.fdf

  #
  # Defines for default states.  These can be changed on the command line.
  # -D FLAG=VALUE
  #
  DEFINE TTY_TERMINAL            = TRUE
  DEFINE QEMU_PV_VARS            = FALSE

  #
  # Non-volatile variable store backing:
  #   TRUE (default)  - use the *emulated* in-RAM variable store. The
  #                     variables work during a single boot but are **never
  #                     persisted** across reboots; on the next power cycle the
  #                     UEFI variable space is reset to the freshly-built
  #                     GUNYAH_VARS.fd content (i.e. an empty boot-order and
  #                     the freshly-reprovisioned Microsoft default keys).
  #
  #                     This is the only mode that actually works on the
  #                     Droid-VM/crosvm fork today: although that fork accepts
  #                     a `--pflash path=...,block_size=...` CLI flag, on
  #                     aarch64 the `components.pflash_image` field is never
  #                     consumed by `aarch64/src/lib.rs::build_vm`, so no MMIO
  #                     window is created and no DT `cfi-flash` node is emitted
  #                     (verified by diffing the dumped DTB with/without the
  #                     flag). With `VirtNorFlashDxe` left without any NOR
  #                     flash instance, no FVB protocol is installed and
  #                     `VariableRuntimeDxe` (and the DXE drivers that depend
  #                     on the Variable arch protocol - Bds, Capsule,
  #                     MonotonicCounter, RTC) can never be dispatched, which
  #                     trips `DxeMain.c(578) ASSERT_EFI_ERROR (Not Found)`.
  #
  #                     Choose this default until the crosvm fork is patched
  #                     to (a) wire `Pflash::new()` + `mmio_bus.insert()` from
  #                     `aarch64/src/lib.rs` and (b) emit a `compatible =
  #                     "cfi-flash"` DT node describing the window, so that
  #                     `FdtNorFlashQemuLib` can claim it (and the Pflash
  #                     device is taught the minimal CFI query commands, since
  #                     `devices/src/pflash.rs` currently ships *no* CFI
  #                     tables).
  #
  #   FALSE           - use the *real* NOR flash backed variable store. The
  #                     firmware reaches `VirtNorFlashDxe` + FTW through the
  #                     DT `cfi-flash` node, so variable writes persist across
  #                     guest power cycles. Only use this with a VMM that
  #                     actually exposes a `cfi-flash` DT node (QEMU with
  #                     `-pflash ...`, or crosvm once the aarch64 pflash
  #                     support is landed).
  #                         ./build.sh -D EMU_VARIABLE_NV_MODE=FALSE
  #
  DEFINE EMU_VARIABLE_NV_MODE   = TRUE

  #
  # Secure Boot:
  #   TRUE (default)  - builds authenticated variable services, hooks
  #                     DxeImageVerificationLib into SecurityStubDxe, adds the
  #                     SecureBootConfigDxe setup menu (Secure Boot on/off switch
  #                     + PK/KEK/db/dbx/dbt key management) and embeds the
  #                     Microsoft default keys (PK/KEK/db/dbx) which
  #                     SecureBootDefaultKeysDxe provisions on first boot.
  #
  #                     The actual Secure Boot enforcement state remains under
  #                     the user's control at boot time via the UEFI setup menu
  #                     entry "Secure Boot Configuration > Attempt Secure Boot":
  #                     it is OFF by default, and toggling it requires an explicit
  #                     "Attempt Secure Boot" checkbox, a "Secure Boot Mode"
  #                     oneof (Standard/Custom), per-database Enroll/Delete under
  #                     Custom mode, and a "Reset to defaults" entry. The
  #                     Microsoft default keys can be re-applied at any time via
  #                     "Reset to defaults".
  #
  #   FALSE           - rebuild without Secure Boot support entirely (smaller
  #                     FV footprint, no setup menu entry), e.g.
  #                     `./build.sh -D SECURE_BOOT_ENABLE=FALSE`.
  #
  DEFINE SECURE_BOOT_ENABLE      = TRUE

  #
  # Network definition
  #
  DEFINE NETWORK_IP6_ENABLE              = FALSE
  DEFINE NETWORK_HTTP_BOOT_ENABLE        = FALSE
  DEFINE NETWORK_SNP_ENABLE              = FALSE
  DEFINE NETWORK_TLS_ENABLE              = FALSE
  DEFINE NETWORK_ALLOW_HTTP_CONNECTIONS  = TRUE
  DEFINE NETWORK_ISCSI_ENABLE            = FALSE
  DEFINE NETWORK_PXE_BOOT_ENABLE         = TRUE

  DEFINE PCI_CAM_MODE                    = TRUE

# This comes at the beginning of includes to pick all relevant defines early on.
!include ArmVirtPkg/ArmVirtStackCookies.dsc.inc

!if $(NETWORK_SNP_ENABLE) == TRUE
  !error "NETWORK_SNP_ENABLE is IA32/X64/EBC only"
!endif

!include NetworkPkg/NetworkDefines.dsc.inc

!include DynamicTablesPkg/DynamicTables.dsc.inc

!include MdePkg/MdeLibs.dsc.inc

# This comes at the end of includes to pick all relevant components without any
# unintentional overrides.
!include GunyahPkg/Gunyah.dsc.inc

[LibraryClasses.common]
  ArmLib|ArmPkg/Library/ArmLib/ArmBaseLib.inf
  ArmMmuLib|UefiCpuPkg/Library/ArmMmuLib/ArmMmuBaseLib.inf

  # Virtio Support
  VirtioLib|OvmfPkg/Library/VirtioLib/VirtioLib.inf
  VirtioMmioDeviceLib|OvmfPkg/Library/VirtioMmioDeviceLib/VirtioMmioDeviceLib.inf
  QemuFwCfgLib|OvmfPkg/Library/QemuFwCfgLib/QemuFwCfgMmioDxeLib.inf
  QemuFwCfgS3Lib|OvmfPkg/Library/QemuFwCfgS3Lib/BaseQemuFwCfgS3LibNull.inf
  QemuFwCfgSimpleParserLib|OvmfPkg/Library/QemuFwCfgSimpleParserLib/QemuFwCfgSimpleParserLib.inf
  QemuLoadImageLib|OvmfPkg/Library/GenericQemuLoadImageLib/GenericQemuLoadImageLib.inf

  GunyahMemInfoLib|GunyahPkg/Library/GunyahMemInfoLib/GunyahMemInfoLib.inf

  TimerLib|ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.inf
  VirtNorFlashDeviceLib|OvmfPkg/Library/VirtNorFlashDeviceLib/VirtNorFlashDeviceLib.inf
  VirtNorFlashPlatformLib|OvmfPkg/Library/FdtNorFlashQemuLib/FdtNorFlashQemuLib.inf

  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf
  PlatformBootManagerLib|GunyahPkg/Library/PlatformBootManagerLibLight/PlatformBootManagerLib.inf
  PlatformBmPrintScLib|OvmfPkg/Library/PlatformBmPrintScLib/PlatformBmPrintScLib.inf
  CustomizedDisplayLib|MdeModulePkg/Library/CustomizedDisplayLib/CustomizedDisplayLib.inf
  FrameBufferBltLib|MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.inf
  QemuBootOrderLib|OvmfPkg/Library/QemuBootOrderLib/QemuBootOrderLib.inf
  PlatformBootManagerCommonLib|OvmfPkg/Library/PlatformBootManagerCommonLib/PlatformBootManagerCommonLib.inf
  FileExplorerLib|MdeModulePkg/Library/FileExplorerLib/FileExplorerLib.inf
  PciSegmentLib|MdePkg/Library/BasePciSegmentLibPci/BasePciSegmentLibPci.inf
  PciHostBridgeLib|GunyahPkg/Library/FdtPciHostBridgeLib/FdtPciHostBridgeLib.inf
!if $(PCI_CAM_MODE) == TRUE
  PciPcdProducerLib|GunyahPkg/Library/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!else
  PciPcdProducerLib|OvmfPkg/Fdt/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!endif
  PciHostBridgeUtilityLib|OvmfPkg/Library/PciHostBridgeUtilityLib/PciHostBridgeUtilityLib.inf
  TpmMeasurementLib|MdeModulePkg/Library/TpmMeasurementLibNull/TpmMeasurementLibNull.inf
  TpmPlatformHierarchyLib|SecurityPkg/Library/PeiDxeTpmPlatformHierarchyLibNull/PeiDxeTpmPlatformHierarchyLib.inf

  ArmMonitorLib|ArmVirtPkg/Library/ArmVirtMonitorLib/ArmVirtMonitorLib.inf
  ArmTrngLib|ArmPkg/Library/ArmTrngLib/ArmTrngLib.inf
  PL011UartLib|ArmPlatformPkg/Library/PL011UartLib/PL011UartLib.inf
  HwInfoParserLib|DynamicTablesPkg/Library/FdtHwInfoParserLib/FdtHwInfoParserLib.inf
  DynamicPlatRepoLib|DynamicTablesPkg/Library/Common/DynamicPlatRepoLib/DynamicPlatRepoLib.inf

[LibraryClasses.common.DXE_DRIVER]
  AcpiPlatformLib|OvmfPkg/Library/AcpiPlatformLib/DxeAcpiPlatformLib.inf
  ReportStatusCodeLib|MdeModulePkg/Library/DxeReportStatusCodeLib/DxeReportStatusCodeLib.inf

[LibraryClasses.common.UEFI_DRIVER]
  UefiScsiLib|MdePkg/Library/UefiScsiLib/UefiScsiLib.inf

[BuildOptions]
!include NetworkPkg/NetworkBuildOptions.dsc.inc

  #
  # We need to avoid jump tables in SEC modules, so that the PE/COFF
  # self-relocation code itself is guaranteed to be position independent.
  #
  GCC:*_*_*_CC_FLAGS = -fno-jump-tables

################################################################################
#
# Pcd Section - list of all EDK II PCD Entries defined by this Platform
#
################################################################################

[PcdsFeatureFlag.common]
  gUefiOvmfPkgTokenSpaceGuid.PcdQemuBootOrderPciTranslation|TRUE
  gUefiOvmfPkgTokenSpaceGuid.PcdQemuBootOrderMmioTranslation|TRUE

  ## If TRUE, Graphics Output Protocol will be installed on virtual handle created by ConsplitterDxe.
  #  It could be set FALSE to save size.
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutGopSupport|TRUE

  gEfiMdeModulePkgTokenSpaceGuid.PcdTurnOffUsbLegacySupport|TRUE

!if $(QEMU_PV_VARS) == TRUE
  gUefiOvmfPkgTokenSpaceGuid.PcdQemuVarsRequire|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdEnableVariableRuntimeCache|FALSE
!endif

[PcdsFixedAtBuild.common]
  gUefiOvmfPkgTokenSpaceGuid.PcdOvmfFdBaseAddress|0x80000000
  gUefiOvmfPkgTokenSpaceGuid.PcdOvmfFirmwareFdSize|0x400000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x4000
  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxVariableSize|0x2000
  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxAuthVariableSize|0x2800
!if $(NETWORK_TLS_ENABLE) == TRUE
  #
  # The cumulative and individual VOLATILE variable size limits should be set
  # high enough for accommodating several and/or large CA certificates.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdVariableStoreSize|0x80000
  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxVolatileVariableSize|0x40000
!endif

  # Size of the region used by UEFI in permanent memory (Reserved 64MB)
  gArmPlatformTokenSpaceGuid.PcdSystemMemoryUefiRegionSize|0x04000000

  #
  # ARM PrimeCell
  #

  ## PL011 - Serial Terminal
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|38400

  ## Default Terminal Type
  ## 0-PCANSI, 1-VT100, 2-VT00+, 3-UTF8, 4-TTYTERM
!if $(TTY_TERMINAL) == TRUE
  gEfiMdePkgTokenSpaceGuid.PcdDefaultTerminalType|4
  # Set terminal type to TtyTerm, the value encoded is EFI_TTY_TERM_GUID
  gUefiOvmfPkgTokenSpaceGuid.PcdTerminalTypeGuidBuffer|{0x80, 0x6d, 0x91, 0x7d, 0xb1, 0x5b, 0x8c, 0x45, 0xa4, 0x8f, 0xe2, 0x5f, 0xdd, 0x51, 0xef, 0x94}
!else
  gEfiMdePkgTokenSpaceGuid.PcdDefaultTerminalType|1
!endif

  #
  # Network Pcds
  #
!include NetworkPkg/NetworkFixedPcds.dsc.inc

  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x21, 0xaa, 0x2c, 0x46, 0x14, 0x76, 0x03, 0x45, 0x83, 0x6e, 0x8a, 0xb6, 0xf4, 0x66, 0x23, 0x31 }

  # virtio-scsi: each target has exactly 1 LUN (LUN 0).
  # Default PcdVirtioScsiMaxLunLimit=7 causes crosvm to return the same disk
  # for LUN 0-7 on the same target, resulting in 8 duplicate disk handles.
  gUefiOvmfPkgTokenSpaceGuid.PcdVirtioScsiMaxLunLimit|0

  # Point to the MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0xdc, 0x5b, 0xc2, 0xee, 0xf2, 0x67, 0x95, 0x4d, 0xb1, 0xd5, 0xf8, 0x1b, 0x20, 0x39, 0xd1, 0x1d }

  #
  # The maximum physical I/O addressability of the processor, set with
  # BuildCpuHob().
  #
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|16

  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|3
  gEfiShellPkgTokenSpaceGuid.PcdShellFileOperationSize|0x20000

  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageVariableSize   | 0x40000
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwSpareSize   | 0x40000
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwWorkingSize | 0x40000

  #
  # Tie VariableRuntimeDxe's emulated-vs-real NVRAM mode to the
  # EMU_VARIABLE_NV_MODE define above. When TRUE (the default, since the
  # Droid-VM/crosvm fork doesn't currently expose a `cfi-flash` DT node on
  # aarch64), VariableRuntimeDxe initializes an in-RAM emulated NVRAM and the
  # fault-tolerant write layer is a no-op - variables work within a single
  # boot but the store resets to GUNYAH_VARS.fd on the next cold boot. When
  # FALSE, VariableRuntimeDxe uses the real NOR flash discovered via the
  # DT `cfi-flash` node, so variables persist across reboots. Only switch to
  # FALSE if your VMM actually exposes a `cfi-flash` node.
  #
!if $(EMU_VARIABLE_NV_MODE) == TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
!else
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|FALSE
!endif

[PcdsPatchableInModule.common]
  # we need to provide a resolution for this PCD that supports PcdSet64()
  # being called from ArmVirtPkg/Library/PlatformPeiLib/PlatformPeiLib.c,
  # even though that call will be compiled out on this platform as it does
  # not (and cannot) support the TPM2 driver stack
  gEfiSecurityPkgTokenSpaceGuid.PcdTpmBaseAddress|0x0

  # NS16550A UART
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x3f8
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseMmio|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialPciDeviceInfo|{0xFF}

  #
  # This will be overridden in the code
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x0

  #
  # Define a default initial address for the device tree.
  # Ignored if x0 != 0 at entry.
  #
  gUefiOvmfPkgTokenSpaceGuid.PcdDeviceTreeInitialBaseAddress|0x40000000

  gArmTokenSpaceGuid.PcdFdBaseAddress|0x80000000
  gArmTokenSpaceGuid.PcdFvBaseAddress|0x80000000

[PcdsDynamicDefault.common]
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|1

  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwSpareBase     | 0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwSpareBase64   | 0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageVariableBase64   | 0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageVariableBase     | 0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwWorkingBase   | 0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwWorkingBase64 | 0

  ## If TRUE, OvmfPkg/AcpiPlatformDxe will not wait for PCI
  #  enumeration to complete before installing ACPI tables.
  gEfiMdeModulePkgTokenSpaceGuid.PcdPciDisableBusEnumeration|TRUE

  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|0x0
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|0x0
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|0x0
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|0x0
  gArmTokenSpaceGuid.PcdArmArchTimerHypVirtIntrNum|0x0

  #
  # ARM General Interrupt Controller
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x0
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x0
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x0

  ## PL030 RealTimeClock
  gGunyahTokenSpaceGuid.PcdPL030RtcBase|0x0

  # set PcdPciExpressBaseAddress to MAX_UINT64, which signifies that this
  # PCD and PcdPciDisableBusEnumeration above have not been assigned yet
  gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseAddress|0xFFFFFFFFFFFFFFFF

  gEfiMdePkgTokenSpaceGuid.PcdPciIoTranslation|0x0

  #
  # Set video resolution for boot options and for text setup.
  # PlatformDxe can set the former at runtime.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|1280
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|800
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|640
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|480
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|31
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|100

  #
  # SMBIOS entry point version
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion|0x0300
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosDocRev|0x0

  ## This PCD tracks where PcdVideo{Horizontal,Vertical}Resolution
  #  values are coming from.
  #    0 - unset (defaults from platform dsc)
  #    1 - set from PlatformConfig
  #    2 - set by GOP Driver.
  gUefiOvmfPkgTokenSpaceGuid.PcdVideoResolutionSource|2

!include NetworkPkg/NetworkDynamicPcds.dsc.inc

################################################################################
#
# Components Section - list of all EDK II Modules needed by this Platform
#
################################################################################
[Components.common]
  #
  # PEI Phase modules
  #
  GunyahPkg/PrePi/GunyahPrePiUniCoreRelocatable.inf {
    <LibraryClasses>
      ExtractGuidedSectionLib|EmbeddedPkg/Library/PrePiExtractGuidedSectionLib/PrePiExtractGuidedSectionLib.inf
      NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
      PrePiLib|EmbeddedPkg/Library/PrePiLib/PrePiLib.inf
      HobLib|EmbeddedPkg/Library/PrePiHobLib/PrePiHobLib.inf
      PrePiHobListPointerLib|ArmPlatformPkg/Library/PrePiHobListPointerLib/PrePiHobListPointerLib.inf
      MemoryAllocationLib|EmbeddedPkg/Library/PrePiMemoryAllocationLib/PrePiMemoryAllocationLib.inf
  }

  #
  # DXE
  #
  MdeModulePkg/Core/Dxe/DxeMain.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DxeCrc32GuidedSectionExtractLib/DxeCrc32GuidedSectionExtractLib.inf
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  }
  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  }

  #
  # Architectural Protocols
  #
  ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
!if $(QEMU_PV_VARS) == TRUE
  OvmfPkg/VirtMmCommunicationDxe/VirtMmCommunication.inf
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableSmmRuntimeDxe.inf
!else
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/VarCheckUefiLib/VarCheckUefiLib.inf
      NULL|EmbeddedPkg/Library/NvVarStoreFormattedLib/NvVarStoreFormattedLib.inf
      # don't use unaligned CopyMem () on the UEFI varstore NOR flash region
      BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  }
!endif
!if $(SECURE_BOOT_ENABLE) == TRUE
  #
  # Hook DxeImageVerificationLib into SecurityStubDxe so that every PE/COFF
  # image loaded during DXE is verified against the Secure Boot signature
  # databases (db/dbx/KEK/PK) when Secure Boot is active.
  #
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf {
    <LibraryClasses>
      NULL|SecurityPkg/Library/DxeImageVerificationLib/DxeImageVerificationLib.inf
  }
  #
  # SecureBootConfigDxe provides the UEFI setup menu entry "Secure Boot
  # Configuration": the Attempt Secure Boot on/off checkbox, Standard/Custom
  # mode oneof, and per-database (PK/KEK/db/dbx/dbt) "enroll from file" forms.
  #
  SecurityPkg/VariableAuthenticated/SecureBootConfigDxe/SecureBootConfigDxe.inf
  #
  # SecureBootDefaultKeysDxe provisions the Microsoft default keys (PK/KEK/db/
  # dbx) into the corresponding *Default variables on first boot, when those
  # variables do not yet exist. The actual certificate payloads are embedded
  # into the firmware volume via FFS FREEFORM files (see GunyahFvMain.fdf.inc).
  #
  SecurityPkg/VariableAuthenticated/SecureBootDefaultKeysDxe/SecureBootDefaultKeysDxe.inf
  #
  # SecureBootProvisioningDxe auto-rolls the *Default keys above into the
  # real PK/KEK/db/dbx/dbt authenticated variables and flips the SecureBoot
  # menu "Attempt Secure Boot" checkbox to ON, on the first boot only (when
  # no PK exists yet). This reproduces what the menu's "Reset to default
  # keys" button does so the user does not have to enter the setup menu and
  # trigger it manually each time the EmuVar store is recreated. On later
  # boots (PK already present), it's a no-op and the user keeps full control
  # to enroll their own keys from the menu.
  #
  GunyahPkg/Drivers/SecureBootProvisioningDxe/SecureBootProvisioningDxe.inf
!else
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
!endif
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/FaultTolerantWriteDxe/FaultTolerantWriteDxe.inf {
    <LibraryClasses>
      NULL|EmbeddedPkg/Library/NvVarStoreFormattedLib/NvVarStoreFormattedLib.inf
  }
  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf {
    <LibraryClasses>
      NULL|GunyahPkg/Library/ArmVirtPL030FdtClientLib/ArmVirtPL030FdtClientLib.inf
  }
  EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf

  MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf

  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf

  ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf {
    <LibraryClasses>
      NULL|ArmVirtPkg/Library/ArmVirtGicArchLib/ArmVirtGicArchLib.inf
  }
  ArmPkg/Drivers/TimerDxe/TimerDxe.inf {
    <LibraryClasses>
      NULL|ArmVirtPkg/Library/ArmVirtTimerFdtClientLib/ArmVirtTimerFdtClientLib.inf
  }
  OvmfPkg/VirtNorFlashDxe/VirtNorFlashDxe.inf {
    <LibraryClasses>
      # don't use unaligned CopyMem () on the UEFI varstore NOR flash region
      BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  }
  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  SecurityPkg/RandomNumberGenerator/RngDxe/RngDxe.inf

  #
  # Status Code Routing
  #
  MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf

  #
  # Platform Driver
  #
  OvmfPkg/Fdt/VirtioFdtDxe/VirtioFdtDxe.inf
  EmbeddedPkg/Drivers/FdtClientDxe/FdtClientDxe.inf
  OvmfPkg/Fdt/HighMemDxe/HighMemDxe.inf
  GunyahPkg/Drivers/GunyahIoMmuDxe/GunyahIoMmuDxe.inf
  OvmfPkg/VirtioBlkDxe/VirtioBlk.inf
  GunyahPkg/Drivers/GunyahVirtioScsiDxe/GunyahVirtioScsiDxe.inf
  OvmfPkg/VirtioNetDxe/VirtioNet.inf
  OvmfPkg/VirtioRngDxe/VirtioRng.inf
  OvmfPkg/VirtioSerialDxe/VirtioSerial.inf
  GunyahPkg/Drivers/GunyahVirtioKeyboardDxe/GunyahVirtioKeyboard.inf

  #
  # FAT filesystem + GPT/MBR partitioning + UDF filesystem + virtio-fs
  #
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf
  MdeModulePkg/Universal/Disk/UdfDxe/UdfDxe.inf
  GunyahPkg/Drivers/GunyahVirtioFsDxe/GunyahVirtioFsDxe.inf

  #
  # Bds
  #
  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf {
    <LibraryClasses>
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  }
  MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  MdeModulePkg/Universal/DriverHealthManagerDxe/DriverHealthManagerDxe.inf
  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  MdeModulePkg/Logo/LogoDxe.inf
  MdeModulePkg/Application/UiApp/UiApp.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DeviceManagerUiLib/DeviceManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootManagerUiLib/BootManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerUiLib.inf
  }
  MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf
  OvmfPkg/QemuKernelLoaderFsDxe/QemuKernelLoaderFsDxe.inf {
    <LibraryClasses>
      NULL|OvmfPkg/Library/BlobVerifierLibNull/BlobVerifierLibNull.inf
  }

  #
  # Networking stack
  #
!include NetworkPkg/NetworkComponents.dsc.inc
!include OvmfPkg/Include/Dsc/NetworkComponents.dsc.inc

  #
  # SCSI Bus and Disk Driver
  #
  MdeModulePkg/Bus/Scsi/ScsiBusDxe/ScsiBusDxe.inf
  MdeModulePkg/Bus/Scsi/ScsiDiskDxe/ScsiDiskDxe.inf

  #
  # NVME Driver
  #
  MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf

  #
  # SMBIOS Support
  #
  MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
  GunyahPkg/Drivers/SmbiosPlatformDxe/SmbiosPlatformDxe.inf

  #
  # PCI support
  #
  UefiCpuPkg/CpuMmio2Dxe/CpuMmio2Dxe.inf {
    <LibraryClasses>
!if $(PCI_CAM_MODE) == TRUE
      NULL|GunyahPkg/Library/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!else
      NULL|OvmfPkg/Fdt/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!endif
  }
  MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf {
    <LibraryClasses>
!if $(PCI_CAM_MODE) == TRUE
      NULL|GunyahPkg/Library/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!else
      NULL|OvmfPkg/Fdt/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!endif
  }
  MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf {
    <LibraryClasses>
!if $(PCI_CAM_MODE) == TRUE
      NULL|GunyahPkg/Library/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!else
      NULL|OvmfPkg/Fdt/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!endif
  }
  OvmfPkg/PciHotPlugInitDxe/PciHotPlugInit.inf
  OvmfPkg/VirtioPciDeviceDxe/VirtioPciDeviceDxe.inf
  OvmfPkg/Virtio10Dxe/Virtio10.inf

  #
  # Video support
  #
  OvmfPkg/QemuRamfbDxe/QemuRamfbDxe.inf
  GunyahPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf {
    <LibraryClasses>
    NULL|GunyahPkg/Library/SimpleFbFdtClientLib/SimpleFbFdtClientLib.inf
  }
  GunyahPkg/Drivers/GunyahVirtioGpuDxe/GunyahVirtioGpu.inf
  OvmfPkg/PlatformDxe/Platform.inf

  #
  # USB support
  #
  !include OvmfPkg/Include/Dsc/UsbComponents.dsc.inc
  MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
  MdeModulePkg/Bus/Usb/UsbMouseAbsolutePointerDxe/UsbMouseAbsolutePointerDxe.inf

  #
  # ACPI Support
  #
  OvmfPkg/PlatformHasAcpiDtDxe/PlatformHasAcpiDtDxe.inf
[Components.AARCH64]
  MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/BootGraphicsResourceTableDxe.inf
  OvmfPkg/AcpiPlatformDxe/AcpiPlatformDxe.inf {
    <LibraryClasses>
!if $(PCI_CAM_MODE) == TRUE
      NULL|GunyahPkg/Library/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!else
      NULL|OvmfPkg/Fdt/FdtPciPcdProducerLib/FdtPciPcdProducerLib.inf
!endif
  }
  ArmVirtPkg/KvmtoolCfgMgrDxe/ConfigurationManagerDxe.inf

  GunyahPkg/Drivers/GunyahPlatformDxe/GunyahPlatformDxe.inf
