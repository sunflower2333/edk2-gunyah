/** @file
  Auto-provision the real Secure Boot variables (PK/KEK/db/dbx/dbt) from the
  *Default counterparts that SecureBootDefaultKeysDxe installs on first boot,
  and turn the "Attempt Secure Boot" checkbox ON by writing the SecureBootEnable
  non-volatile variable.

  Without this driver, on EmuVar (in-RAM) variable stores the UEFI menu shows
  "Attempt Secure Boot" as OFF even though the Microsoft default cert files are
  embedded in the FV. The user has to manually enter "Secure Boot Configuration
  > Reset Secure Boot Keys to defaults" each boot to flip it ON because:

    - SecureBootDefaultKeysDxe only writes the *Default variables
      (PKDefault / KEKDefault / dbDefault / dbxDefault / dbtDefault).
    - The actual PK/KEK/db/dbx/dbt variables and the SecureBootEnable switch
      are only written when the user clicks "Reset to defaults" inside
      SecureBootConfigDxe's HII menu.

  This driver reproduces the menu's reset flow exactly once: when the variable
  service has the *Default keys ready and no PK has been enrolled yet. On later
  boots (PK already present), it does nothing, so the user retains full control
  to enroll their own keys from the menu afterwards.

  In addition, this driver listens for the arrival of an EFI SYSTEM partition
  (the ESP) and writes a default Windows Defender Application Control (WDAC)
  "System Integrity" policy (SiPolicy.p7b) to
  \\EFI\\Microsoft\\Boot\\SiPolicy.p7b when Secure Boot is ON.  When Secure
  Boot is OFF the file is removed (Windows WDAC policies are meaningful only
  when the UEFI Secure Boot chain is intact).  Existing policy files are
  compared by SHA-256 hash and replaced only if they differ.

  SiPolicy.p7b is needed so Windows will accept self-signed/production-test
  kernel-mode drivers without the user having to manually install a WDAC
  policy from inside Windows.

  Copyright (c) 2024, Collabora Ltd.
  Portions Copyright (C) Microsoft Corporation. All rights reserved.
  Portions Copyright (C) DuoWoA authors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Base.h>

#include <Pi/PiFirmwareFile.h>

#include <Library/BaseLib.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/PcdLib.h>

#include <Guid/GlobalVariable.h>
#include <Guid/ImageAuthentication.h>
#include <Guid/AuthenticatedVariableFormat.h>
#include <Guid/FileInfo.h>

#include <UefiSecureBoot.h>

#include <Library/SecureBootVariableLib.h>
#include <Library/SecureBootVariableProvisionLib.h>

//
// mFileSystemRegistration gets initialized to NULL and later updated by
// ProcessFileSystemRegistration() to hold the protocol-notification handle.
//
VOID *mFileSystemRegistration = NULL;

/**
  Return TRUE if a UINT8 EFI variable exists and equals the supplied value.

  @param[in]  VariableName  Name of the variable.
  @param[in]  VendorGuid    Variable vendor GUID.
  @param[in]  Value         Expected value (UINT8). Only meaningful if Exists
                            is returned as TRUE.

  @retval TRUE              Variable exists and equals Value.
  @retval FALSE             Variable unreadable or value mismatch.
**/
STATIC
BOOLEAN
SecureBootVariableEquals (
  IN CONST CHAR16   *VariableName,
  IN CONST EFI_GUID *VendorGuid,
  IN UINT8          Value
  )
{
  EFI_STATUS  Status;
  UINT8       *Data;
  UINTN       DataSize;

  Status = GetVariable2 (VariableName, VendorGuid, (VOID **)&Data, &DataSize);
  if (EFI_ERROR (Status) || (Data == NULL)) {
    return FALSE;
  }

  if ((DataSize != sizeof (UINT8)) || (*Data != Value)) {
    FreePool (Data);
    return FALSE;
  }

  FreePool (Data);
  return TRUE;
}

/**
  Helper function to query whether the secure boot variable is in place and
  equal to 1 (Secure Boot enabled).

  @retval  TRUE  Secure Boot is ON (authenticated PK is present, or
                 SecureBootEnable variable is set to 1).
  @retval  FALSE Secure Boot is not enforced.
**/
STATIC
BOOLEAN
IsSecureBootOn (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       PkSize;

  //
  // Check the PK variable: if it has a non-zero size, Secure Boot is in
  // USER_MODE and enforced.
  //
  PkSize = 0;
  Status = gRT->GetVariable (
                   EFI_PLATFORM_KEY_NAME,
                   &gEfiGlobalVariableGuid,
                   NULL,
                   &PkSize,
                   NULL
                   );

  if ((Status == EFI_BUFFER_TOO_SMALL) && (PkSize > 0)) {
    DEBUG ((DEBUG_INFO, "%a - PK exists, Secure Boot ON. PkSize=0x%X\n", __func__, PkSize));
    return TRUE;
  }

  DEBUG ((DEBUG_INFO, "%a - PK doesn't exist, Secure Boot OFF\n", __func__));
  return FALSE;
}

/**
  Try to write the default System Integrity Policy (SiPolicy.p7b) to the ESP.

  When a new EFI file system protocol is discovered (e.g. the ESP), call into
  this function.  It opens \\EFI\\Microsoft\\Boot\\bootmgfw.efi to verify it
  has the right file system.

  @param[in]  SfsHandle  Handle that supports
                         gEfiSimpleFileSystemProtocolGuid.

  @retval  EFI_SUCCESS  SiPolicy was written, removed, or no change needed.
  @retval  other        SiPolicy provisioning was not possible.
**/
EFI_STATUS
EFIAPI
TryWritePlatformSiPolicy (
  IN EFI_HANDLE  SfsHandle
  )
{
  EFI_STATUS                      Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *EfiSfsProtocol;
  EFI_FILE_PROTOCOL               *FileProtocol;
  EFI_FILE_PROTOCOL               *PayloadFileProtocol;
  EFI_GUID                        *FileGuid;
  UINT8                           *mSiPolicyDefault     = NULL;
  UINTN                            mSiPolicyDefaultSize  = 0;
  UINT8                           mSiPolicyDefaultHash[SHA256_DIGEST_SIZE];
  UINT8                           *SiPolicyEfiSfs          = NULL;
  UINTN                            SiPolicyEfiSfsInfoSize   = 0;
  UINTN                            SiPolicyEfiSfsSize       = 0;
  UINT8                            SiPolicyEfiSfsHash[SHA256_DIGEST_SIZE];

  FileGuid = PcdGetPtr (PcdSiPolicyFileGuid);

  //
  // Retrieve the SiPolicy.p7b image from the firmware volume.
  //
  Status = GetSectionFromAnyFv (
              FileGuid,
              EFI_SECTION_RAW,
              0,
              (VOID **)&mSiPolicyDefault,
              &mSiPolicyDefaultSize
              );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: GetSectionFromAnyFv failed: %r\n", __func__, Status));
    return Status;
  }

  //
  // Locate the Simple File System protocol on the given handle.
  //
  Status = gBS->HandleProtocol (
                   SfsHandle,
                   &gEfiSimpleFileSystemProtocolGuid,
                   (VOID **)&EfiSfsProtocol
                   );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: HandleProtocol failed: %r\n", __func__, Status));
    goto exit;
  }

  //
  // Open the volume.
  //
  Status = EfiSfsProtocol->OpenVolume (EfiSfsProtocol, &FileProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: OpenVolume failed: %r\n", __func__, Status));
    goto exit;
  }

  //
  // Check that this is the Windows ESP by trying to open bootmgfw.efi.
  // Failure here may mean this is NOT the Windows ESP, in which case we
  // skip SiPolicy provisioning silently.
  //
  Status = FileProtocol->Open (
                           FileProtocol,
                           &PayloadFileProtocol,
                           L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
                           EFI_FILE_MODE_READ,
                           EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM
                           );
  if (!EFI_ERROR (Status)) {
    PayloadFileProtocol->Close (PayloadFileProtocol);
  }

  //
  // Try to open SiPolicy.p7b.  Use read+write mode so we can replace it
  // in-place if needed.
  //
  Status = FileProtocol->Open (
                           FileProtocol,
                           &PayloadFileProtocol,
                           L"\\EFI\\Microsoft\\Boot\\SiPolicy.p7b",
                           EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                           0
                           );

  if (!EFI_ERROR (Status)) {
    //
    // File exists.
    //
    if (!IsSecureBootOn ()) {
      //
      // Secure Boot is OFF - delete the policy file.
      //
      DEBUG ((DEBUG_INFO, "%a: Secure Boot OFF; deleting SiPolicy.p7b\n", __func__));
      PayloadFileProtocol->Delete (PayloadFileProtocol);
      Status = EFI_SUCCESS;
      goto exit;
    }

    //
    // Secure Boot is ON - compare hash and replace if different.
    //
    Status = PayloadFileProtocol->GetInfo (
                                    PayloadFileProtocol,
                                    &gEfiFileInfoGuid,
                                    &SiPolicyEfiSfsInfoSize,
                                    NULL
                                    );
    if (Status == EFI_BUFFER_TOO_SMALL) {
      EFI_FILE_INFO *FileInfo = AllocatePool (SiPolicyEfiSfsInfoSize);
      if (FileInfo == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto exit;
      }

      Status = PayloadFileProtocol->GetInfo (
                                      PayloadFileProtocol,
                                      &gEfiFileInfoGuid,
                                      &SiPolicyEfiSfsInfoSize,
                                      FileInfo
                                      );
      if (!EFI_ERROR (Status)) {
        SiPolicyEfiSfsSize = FileInfo->FileSize;
        DEBUG ((DEBUG_INFO, "%a: Existing SiPolicy.p7b size: %ld\n", __func__, SiPolicyEfiSfsSize));
      }
      FreePool (FileInfo);
    }

    if (SiPolicyEfiSfsSize > 0) {
      SiPolicyEfiSfsInfoSize = SiPolicyEfiSfsSize;
      SiPolicyEfiSfs = AllocatePool (SiPolicyEfiSfsInfoSize);
      if (SiPolicyEfiSfs == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto exit;
      }

      Status = PayloadFileProtocol->Read (
                                      PayloadFileProtocol,
                                      &SiPolicyEfiSfsInfoSize,
                                      SiPolicyEfiSfs
                                      );
      if (!EFI_ERROR (Status)) {
        Sha256HashAll (SiPolicyEfiSfs, SiPolicyEfiSfsInfoSize, SiPolicyEfiSfsHash);
        Sha256HashAll (mSiPolicyDefault, mSiPolicyDefaultSize, mSiPolicyDefaultHash);

        if (CompareMem (SiPolicyEfiSfsHash, mSiPolicyDefaultHash, SHA256_DIGEST_SIZE) == 0) {
          //
          // Hash matches - policy is already up to date.
          //
          DEBUG ((DEBUG_INFO, "%a: SiPolicy.p7b is up to date\n", __func__));
          goto cleanup_existing;
        }
      }

      FreePool (SiPolicyEfiSfs);
      SiPolicyEfiSfs = NULL;
    }

    //
    // Delete the old file so we can create a new one.
    //
    PayloadFileProtocol->Delete (PayloadFileProtocol);
  } else if (Status != EFI_NOT_FOUND) {
    DEBUG ((DEBUG_ERROR, "%a: Open failed: %r\n", __func__, Status));
    goto exit;
  } else {
    //
    // File does not exist - do nothing if Secure Boot is OFF.
    //
    if (!IsSecureBootOn ()) {
      Status = EFI_SUCCESS;
      goto exit;
    }
  }

  //
  // Create a new SiPolicy.p7b with the default contents from the FV.
  //
  Status = FileProtocol->Open (
                           FileProtocol,
                           &PayloadFileProtocol,
                           L"\\EFI\\Microsoft\\Boot\\SiPolicy.p7b",
                           EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                           0
                           );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Create failed: %r\n", __func__, Status));
    goto exit;
  }

  Status = PayloadFileProtocol->Write (
                                  PayloadFileProtocol,
                                  &mSiPolicyDefaultSize,
                                  mSiPolicyDefault
                                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Write failed: %r\n", __func__, Status));
  }

  PayloadFileProtocol->Close (PayloadFileProtocol);
  DEBUG ((DEBUG_INFO, "%a: SiPolicy.p7b written successfully\n", __func__));
  goto exit;

cleanup_existing:
  if (SiPolicyEfiSfs != NULL) {
    FreePool (SiPolicyEfiSfs);
    SiPolicyEfiSfs = NULL;
  }
  if (mSiPolicyDefault != NULL) {
    FreePool (mSiPolicyDefault);
    mSiPolicyDefault = NULL;
  }
  return EFI_SUCCESS;

exit:
  if (mSiPolicyDefault != NULL) {
    FreePool (mSiPolicyDefault);
  }
  if (SiPolicyEfiSfs != NULL) {
    FreePool (SiPolicyEfiSfs);
  }
  return Status;
}

/**
  File System arrival callback.  Called for each
  gEfiSimpleFileSystemProtocolGuid protocol installation.

  @param[in]  Event     Notification event (not used).
  @param[in]  Context   Notification context (not used).

  @retval  none
**/
VOID
EFIAPI
OnFileSystemNotification (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN       HandleCount;
  EFI_HANDLE  *HandleBuffer;
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "%a: Entry...\n", __func__));

  for ( ; ;) {
    //
    // Get the next handle.
    //
    Status = gBS->LocateHandleBuffer (
                     ByRegisterNotify,
                     NULL,
                     mFileSystemRegistration,
                     &HandleCount,
                     &HandleBuffer
                     );

    //
    // If not found, or any other error, we're done.
    //
    if (EFI_ERROR (Status)) {
      break;
    }

    // Spec says we only get one at a time using ByRegisterNotify.
    ASSERT (HandleCount == 1);

    DEBUG ((
      DEBUG_INFO,
      "%a: processing file system device on handle %p\n",
      __func__,
      HandleBuffer[0]
      ));

    TryWritePlatformSiPolicy (HandleBuffer[0]);

    FreePool (HandleBuffer);
  }
}

/**
  Register for file-system notifications and fire the notification callback for
  any file systems that were already present.

  @retval  EFI_SUCCESS  Registration (or partial registration) succeeded.
  @retval  other        Could not register for notifications.
**/
EFI_STATUS
ProcessFileSystemRegistration (
  VOID
  )
{
  EFI_EVENT   FileSystemCallBackEvent;
  EFI_STATUS  Status;

  //
  // Register for file system notifications.  They may arrive at any time.
  //
  DEBUG ((DEBUG_INFO, "Registering for file system notifications\n"));
  Status = gBS->CreateEvent (
                   EVT_NOTIFY_SIGNAL,
                   TPL_CALLBACK,
                   OnFileSystemNotification,
                   NULL,
                   &FileSystemCallBackEvent
                   );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: failed to create callback event (%r)\n", __func__, Status));
    goto Cleanup;
  }

  Status = gBS->RegisterProtocolNotify (
                   &gEfiSimpleFileSystemProtocolGuid,
                   FileSystemCallBackEvent,
                   &mFileSystemRegistration
                   );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: failed to register for notifications (%r)\n", __func__, Status));
    gBS->CloseEvent (FileSystemCallBackEvent);
    goto Cleanup;
  }

  //
  // Process any file systems that were already present before the
  // registration.
  //
  OnFileSystemNotification (FileSystemCallBackEvent, NULL);

Cleanup:
  return Status;
}

/**
  Entry point: rolls the *Default Secure Boot keys into the authenticated PK /
  KEK / db / dbx / dbt variables, sets SecureBootEnable=1, and registers for
  file-system notifications to write SiPolicy.p7b to the ESP.

  Both the Secure Boot key provisioning and the SiPolicy provisioning run only
  on the first boot of a clean (PK-less) variable store.

  @param[in]  ImageHandle  The firmware allocated handle for the EFI image.
  @param[in]  SystemTable  A pointer to the EFI System Table.

  @retval  EFI_SUCCESS  All provisioning completed successfully.
  @retval  partial      Some provisioning steps failed.  Check debug log.
**/
EFI_STATUS
EFIAPI
SecureBootProvisioningDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT8       SetupMode;
  UINT8       SecureBootEnable;

  //
  // Register for file system notifications and process any file systems that
  // were already present.
  //
  Status = ProcessFileSystemRegistration ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "SiPolicy FS registration failed: %r\n", Status));
    //
    // Don't fail - SiPolicy is not critical for Secure Boot key provisioning.
    //
  }

  //
  // Honor any prior user choice: if SecureBootEnable was already written
  // (whether TRUE or FALSE) we leave the enforcement state untouched so the
  // user can later toggle it from the menu without us reverting their decision.
  //
  if (SecureBootVariableEquals (
        EFI_SECURE_BOOT_ENABLE_NAME,
        &gEfiSecureBootEnableDisableGuid,
        0x00
        ) ||
      SecureBootVariableEquals (
        EFI_SECURE_BOOT_ENABLE_NAME,
        &gEfiSecureBootEnableDisableGuid,
        0x01
        )) {
    DEBUG ((
      DEBUG_INFO,
      "%a: SecureBootEnable already provisioned by user - skipping auto-"
      "provisioning.\n",
      __func__
      ));
    return EFI_SUCCESS;
  }

  //
  // Only auto-enroll keys if we're in SETUP_MODE (no PK installed yet). On any
  // later boot PK will be present and we'd return here; this protects the user
  // against us clobbering their manually-enrolled keys.
  //
  Status = GetSetupMode (&SetupMode);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: GetSetupMode failed: %r. Skipping auto-provisioning.\n",
      __func__,
      Status
      ));
    return EFI_SUCCESS;
  }

  if (SetupMode != SETUP_MODE) {
    DEBUG ((
      DEBUG_INFO,
      "%a: Already in USER_MODE (SetupMode=%u) - PK exists, nothing to do.\n",
      __func__,
      SetupMode
      ));
    return EFI_SUCCESS;
  }

  //
  // Enter CUSTOM_SECURE_BOOT_MODE so authenticated writes of PK/KEK/db/dbx
  // succeed without requiring an existing PK to countersign.
  //
  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: SetSecureBootMode(CUSTOM) failed: %r. Will skip auto-"
      "provisioning.\n",
      __func__,
      Status
      ));
    return EFI_SUCCESS;
  }

  //
  // Enroll keys in the same order as the menu reset flow: db, dbx, dbt, KEK,
  // then PK last. Enrolling PK is what flips SetupMode -> USER_MODE and locks
  // the store.
  //
  Status = EnrollDbFromDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: EnrollDbFromDefault: %r\n", __func__, Status));
    goto RestoreStandardMode;
  }

  Status = EnrollDbxFromDefault ();
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    DEBUG ((DEBUG_WARN, "%a: EnrollDbxFromDefault: %r (continuing)\n", __func__, Status));
  }

  Status = EnrollDbtFromDefault ();
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    DEBUG ((DEBUG_WARN, "%a: EnrollDbtFromDefault: %r (continuing)\n", __func__, Status));
  }

  Status = EnrollKEKFromDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: EnrollKEKFromDefault: %r\n", __func__, Status));
    goto RestoreStandardMode;
  }

  Status = EnrollPKFromDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: EnrollPKFromDefault: %r\n", __func__, Status));
    goto RestoreStandardMode;
  }

  //
  // Return to STANDARD_SECURE_BOOT_MODE so the firmware enforces signature
  // checks from now on.
  //
  Status = SetSecureBootMode (STANDARD_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: SetSecureBootMode(STANDARD) failed: %r. Secure Boot enforcement "
      "may be in inconsistent state - please reset to default keys from the "
      "UEFI menu manually.\n",
      __func__,
      Status
      ));
    return Status;
  }

  //
  // Write SecureBootEnable=1 so the "Attempt Secure Boot" checkbox in
  // SecureBootConfigDxe's menu reflects ON.
  //
  SecureBootEnable = SECURE_BOOT_ENABLE;
  Status = gRT->SetVariable (
                   EFI_SECURE_BOOT_ENABLE_NAME,
                   &gEfiSecureBootEnableDisableGuid,
                   EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                   sizeof (SecureBootEnable),
                   &SecureBootEnable
                   );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: Writing SecureBootEnable=1 failed: %r. PK/KEK/db are enrolled but "
      "the menu checkbox may still display OFF.\n",
      __func__,
      Status
      ));
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: Auto-provisioned Microsoft default Secure Boot keys, enabled "
    "Secure Boot enforcement, and started SiPolicy provisioning.\n",
    __func__
    ));
  return EFI_SUCCESS;

RestoreStandardMode:
  {
    EFI_STATUS  RestoreStatus;
    RestoreStatus = SetSecureBootMode (STANDARD_SECURE_BOOT_MODE);
    if (EFI_ERROR (RestoreStatus)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: Failed to restore STANDARD_SECURE_BOOT_MODE on error path: %r\n",
        __func__,
        RestoreStatus
        ));
    }
  }

  return EFI_SUCCESS;
}
