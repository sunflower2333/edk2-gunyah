#
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014-2016, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2017, Intel Corporation. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#

################################################################################
#
# FV Section
#
# [FV] section is used to define what components or modules are placed within a flash
# device file.  This section also defines order the components and modules are positioned
# within the image.  The [FV] section consists of define statements, set statements and
# module statements.
#
################################################################################

[FV.FvMain]
FvNameGuid         = 64074afe-340a-4be6-94ba-91b5b4d0f71e
BlockSize          = 0x40
NumBlocks          = 0         # This FV gets compressed so make it just big enough
FvAlignment        = 16        # FV alignment and FV attributes setting.
ERASE_POLARITY     = 1
MEMORY_MAPPED      = TRUE
STICKY_WRITE       = TRUE
LOCK_CAP           = TRUE
LOCK_STATUS        = TRUE
WRITE_DISABLED_CAP = TRUE
WRITE_ENABLED_CAP  = TRUE
WRITE_STATUS       = TRUE
WRITE_LOCK_CAP     = TRUE
WRITE_LOCK_STATUS  = TRUE
READ_DISABLED_CAP  = TRUE
READ_ENABLED_CAP   = TRUE
READ_STATUS        = TRUE
READ_LOCK_CAP      = TRUE
READ_LOCK_STATUS   = TRUE

  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF OvmfPkg/Fdt/VirtioFdtDxe/VirtioFdtDxe.inf
  INF EmbeddedPkg/Drivers/FdtClientDxe/FdtClientDxe.inf
  INF OvmfPkg/Fdt/HighMemDxe/HighMemDxe.inf
  INF GunyahPkg/Drivers/GunyahIoMmuDxe/GunyahIoMmuDxe.inf

  #
  # PI DXE Drivers producing Architectural Protocols (EFI Services)
  #
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/FaultTolerantWriteDxe/FaultTolerantWriteDxe.inf
!if $(QEMU_PV_VARS) == TRUE
  INF OvmfPkg/VirtMmCommunicationDxe/VirtMmCommunication.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableSmmRuntimeDxe.inf
!else
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
!endif
!if $(SECURE_BOOT_ENABLE) == TRUE
  INF SecurityPkg/VariableAuthenticated/SecureBootConfigDxe/SecureBootConfigDxe.inf
  INF SecurityPkg/VariableAuthenticated/SecureBootDefaultKeysDxe/SecureBootDefaultKeysDxe.inf
  INF GunyahPkg/Drivers/SecureBootProvisioningDxe/SecureBootProvisioningDxe.inf

  #
  # Microsoft default Secure Boot keys, embedded into the FV as FREEFORM
  # FFS files. The GUIDs are gDefaultPKFileGuid / gDefaultKEKFileGuid /
  # gDefaultdbFileGuid / gDefaultdbxFileGuid defined in SecurityPkg.dec.
  # SecureBootDefaultKeysDxe reads these RAW sections and provisions the
  # corresponding PKDefault/KEKDefault/dbDefault/dbxDefault variables on
  # first boot. The user can later override via the Secure Boot Configuration
  # menu (Custom mode > PK/KEK/db/dbx > Enroll from file).
  #
  #   PK   - RiscVVirt test self-signed Platform Key (replace with your own
  #          PK for production key interchange).
  #   KEK  - Microsoft Corporation KEK CA 2011 + Microsoft Corporation KEK
  #          2K CA 2023. Keeping both permits legacy and current Microsoft
  #          signature database updates during the certificate transition.
  #   db   - Legacy compatibility: Microsoft Windows Production PCA 2011 and
  #          Microsoft Corporation UEFI CA 2011.
  #          Current trust: Windows UEFI CA 2023, Microsoft UEFI CA 2023, and
  #          Microsoft Option ROM UEFI CA 2023.
  #   dbx  - UEFI revocation list (revoked hashes/certs).
  #
  FILE FREEFORM = 85254ea7-4759-4fc4-82d4-5eed5fb0a4a0 {
    SECTION RAW = GunyahPkg/SecureBootKeys/PK/PK.cer
  }

  FILE FREEFORM = 6f64916e-9f7a-4c35-b952-cd041efb05a3 {
    SECTION RAW = GunyahPkg/SecureBootKeys/KEK/MicCorKEKCA2011_2011-06-24.crt
    SECTION RAW = GunyahPkg/SecureBootKeys/KEK/microsoft_corporation_kek_2k_ca_2023.der
  }

  FILE FREEFORM = c491d352-7623-4843-accc-2791a7574421 {
    SECTION RAW = GunyahPkg/SecureBootKeys/db/MicWinProPCA2011_2011-10-19.crt
    SECTION RAW = GunyahPkg/SecureBootKeys/db/windows_uefi_ca_2023.der
    SECTION RAW = GunyahPkg/SecureBootKeys/db/MicCorUEFCA2011_2011-06-27.crt
    SECTION RAW = GunyahPkg/SecureBootKeys/db/microsoft_uefi_ca_2023.der
    SECTION RAW = GunyahPkg/SecureBootKeys/db/microsoft_option_rom_uefi_ca_2023.der
  }

  FILE FREEFORM = 5740766a-718e-4dc0-9935-c36f7d3f884f {
    SECTION RAW = GunyahPkg/SecureBootKeys/dbx/dbxupdate_x64.bin
  }

  #
  # Default Windows WDAC System Integrity Policy (SiPolicy.p7b).
  # SecureBootProvisioningDxe reads this FREEFORM RAW section from the FV
  # and writes it to \EFI\Microsoft\Boot\SiPolicy.p7b on the ESP when
  # Secure Boot is ON. This enables Windows self-signed drivers.
  #
  FILE FREEFORM = PCD(gGunyahTokenSpaceGuid.PcdSiPolicyFileGuid) {
    SECTION RAW = GunyahPkg/Include/Resources/SecureBoot/SystemIntegrity/SiPolicy.p7b
    SECTION UI = "System Integrity Policy"
  }
!endif
  INF MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf

  #
  # Multiple Console IO support
  #
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  INF MdeModulePkg/Universal/SerialDxe/SerialDxe.inf

  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  INF OvmfPkg/VirtNorFlashDxe/VirtNorFlashDxe.inf
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF SecurityPkg/RandomNumberGenerator/RngDxe/RngDxe.inf

  #
  # FAT filesystem + GPT/MBR partitioning + UDF filesystem + virtio-fs
  #
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  INF MdeModulePkg/Universal/Disk/UdfDxe/UdfDxe.inf
  INF GunyahPkg/Drivers/GunyahVirtioFsDxe/GunyahVirtioFsDxe.inf

  #
  # Status Code Routing
  #
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf

  #
  # Platform Driver
  #
  INF OvmfPkg/VirtioBlkDxe/VirtioBlk.inf
  INF OvmfPkg/VirtioNetDxe/VirtioNet.inf
  INF GunyahPkg/Drivers/GunyahVirtioScsiDxe/GunyahVirtioScsiDxe.inf
  INF OvmfPkg/VirtioRngDxe/VirtioRng.inf
  INF OvmfPkg/VirtioSerialDxe/VirtioSerial.inf
  INF GunyahPkg/Drivers/GunyahVirtioKeyboardDxe/GunyahVirtioKeyboard.inf

#
# Shell + dynamic commands.
#
# Upstream OvmfPkg's ShellDxe.fdf.inc gates Shell on
#   $(BUILD_SHELL) == TRUE && $(SECURE_BOOT_ENABLE) == FALSE
# which intentionally drops Shell from production Secure Boot firmware
# (Shell can load arbitrary UEFI binaries).  For this development / test
# platform we want Shell available even with Secure Boot enabled, so we
# inline the same INF list here and drop the SECURE_BOOT_ENABLE condition,
# keeping only BUILD_SHELL as the gate.  The DSC side
# (OvmfPkg/Include/Dsc/ShellComponents.dsc.inc) already builds Shell
# unconditionally on BUILD_SHELL, so the binary exists regardless; this
# block controls only whether it gets packaged into FVMAIN.
#
!if $(BUILD_SHELL) == TRUE
!if $(TOOL_CHAIN_TAG) != "XCODE5"
!if $(NETWORK_ENABLE) == TRUE
  INF  ShellPkg/DynamicCommand/TftpDynamicCommand/TftpDynamicCommand.inf
  INF  ShellPkg/DynamicCommand/HttpDynamicCommand/HttpDynamicCommand.inf
!endif
  INF  ShellPkg/DynamicCommand/VariablePolicyDynamicCommand/VariablePolicyDynamicCommand.inf
  INF  OvmfPkg/LinuxInitrdDynamicShellCommand/LinuxInitrdDynamicShellCommand.inf
!endif
  INF  ShellPkg/Application/Shell/Shell.inf
!endif

  #
  # Bds
  #
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  INF MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  INF MdeModulePkg/Universal/DriverHealthManagerDxe/DriverHealthManagerDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  INF MdeModulePkg/Application/UiApp/UiApp.inf
  INF MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf
  INF OvmfPkg/QemuKernelLoaderFsDxe/QemuKernelLoaderFsDxe.inf

  #
  # Networking stack
  #
!include NetworkPkg/Network.fdf.inc


  #
  # SCSI Bus and Disk Driver
  #
  INF MdeModulePkg/Bus/Scsi/ScsiBusDxe/ScsiBusDxe.inf
  INF MdeModulePkg/Bus/Scsi/ScsiDiskDxe/ScsiDiskDxe.inf

  #
  # NVME Driver
  #
  INF MdeModulePkg/Bus/Pci/NvmExpressDxe/NvmExpressDxe.inf

  #
  # SMBIOS Support
  #
  INF MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
  INF GunyahPkg/Drivers/SmbiosPlatformDxe/SmbiosPlatformDxe.inf

  #
  # ACPI Support
  #
  INF OvmfPkg/PlatformHasAcpiDtDxe/PlatformHasAcpiDtDxe.inf
!if $(ARCH) == AARCH64
  INF MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
  !include DynamicTablesPkg/DynamicTables.fdf.inc
  INF ArmVirtPkg/KvmtoolCfgMgrDxe/ConfigurationManagerDxe.inf
  INF GunyahPkg/Drivers/GunyahRestrictedDmaPoolAcpiDxe/GunyahRestrictedDmaPoolAcpiDxe.inf
  INF MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/BootGraphicsResourceTableDxe.inf
  INF OvmfPkg/AcpiPlatformDxe/AcpiPlatformDxe.inf
!endif

  #
  # PCI support
  #
  INF UefiCpuPkg/CpuMmio2Dxe/CpuMmio2Dxe.inf
  INF MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf
  INF MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf
  INF OvmfPkg/PciHotPlugInitDxe/PciHotPlugInit.inf
  INF OvmfPkg/VirtioPciDeviceDxe/VirtioPciDeviceDxe.inf
  INF OvmfPkg/Virtio10Dxe/Virtio10.inf

  #
  # Video support
  #
  INF OvmfPkg/QemuRamfbDxe/QemuRamfbDxe.inf
  INF GunyahPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
  INF GunyahPkg/Drivers/GunyahVirtioGpuDxe/GunyahVirtioGpu.inf
  INF OvmfPkg/PlatformDxe/Platform.inf

  #
  # USB Support
  #
  INF MdeModulePkg/Bus/Pci/UhciDxe/UhciDxe.inf
  INF MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf
  INF MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMouseAbsolutePointerDxe/UsbMouseAbsolutePointerDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  #
  # TianoCore logo (splash screen)
  #
  INF MdeModulePkg/Logo/LogoDxe.inf

  #
  # Ramdisk support
  #
  INF MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskDxe.inf

  INF GunyahPkg/Drivers/GunyahPlatformDxe/GunyahPlatformDxe.inf
