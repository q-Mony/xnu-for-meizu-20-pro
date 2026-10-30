/** @file
  Reboot into the stock bootloader's fastboot mode.

  The firmware in this tree has no fastboot application of its own, and the
  vendor USB stack is only the device side. What the stock ABL does honour is
  the AOSP bootloader message in the misc partition: on the way up it reads
  that block and acts on the command it finds.

  So the request is a write plus a reset, which is the same thing
  `adb reboot bootloader` does from a running Android.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Internal.h"

#include <Protocol/BlockIo.h>

//
// struct bootloader_message from AOSP bootloader.h: two 32-byte command slots,
// then a 64-byte status slot, then recovery fields.
//
#define BL_MSG_COMMAND_BYTES   32
#define BL_MSG_STATUS_BYTES    32
#define BL_MSG_SLOT_BYTES      32
#define BL_MSG_MAGIC           "bootonce-bootloader"

typedef struct {
  CHAR8    Command[BL_MSG_COMMAND_BYTES];
  CHAR8    Status[BL_MSG_STATUS_BYTES];
  CHAR8    Recovery[768];
} BOOTLOADER_MESSAGE;

STATIC
VOID
ReportFatal (
  IN CONST CHAR8  *Text
  )
{
  UniXNULog ("fastboot: %a", Text);
  if (gST->ConOut != NULL) {
    Print (L"\r\nUniXNU fastboot: %a\r\nPress any key.\r\n", Text);
  }

  if (gST->ConIn != NULL) {
    EFI_INPUT_KEY  Key;
    UINTN          Index;

    if (!EFI_ERROR (gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index))) {
      gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    }
  }
}

EFI_STATUS
UniXNURequestFastboot (
  VOID
  )
{
  EFI_STATUS             Status;
  EFI_BLOCK_IO_PROTOCOL  *Disk;
  UINT64                 FirstBlock;
  UINT64                 LastBlock;
  UINTN                  BlockSize;
  UINTN                  Bytes;
  BOOTLOADER_MESSAGE     *Message;
  CHAR8                  Text[160];

  Disk       = NULL;
  FirstBlock = 0;
  LastBlock  = 0;

  UniXNULog ("fastboot: looking for the misc partition");

  Status = UniXNUGptFindAnyDisk ("misc", &Disk, &FirstBlock, &LastBlock);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text),
                 "no misc partition (%r); the firmware cannot ask the "
                 "bootloader for fastboot", Status);
    ReportFatal (Text);
    return Status;
  }

  BlockSize = Disk->Media->BlockSize;

  //
  // The message occupies at least one block; keep the write inside the
  // partition in case it is smaller than the struct.
  //
  Bytes = sizeof (BOOTLOADER_MESSAGE);
  if (Bytes > (LastBlock - FirstBlock + 1) * BlockSize) {
    Bytes = (UINTN)((LastBlock - FirstBlock + 1) * BlockSize);
  }

  if (Bytes < BL_MSG_COMMAND_BYTES) {
    ReportFatal ("misc partition too small for a bootloader message");
    return EFI_DEVICE_ERROR;
  }

  Message = AllocateZeroPool (Bytes);
  if (Message == NULL) {
    ReportFatal ("out of memory");
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Command slot only: leaving the status and recovery fields zero is what
  // makes the bootloader clear the message once it has acted on it.
  //
  AsciiStrCpyS (Message->Command, BL_MSG_COMMAND_BYTES, BL_MSG_MAGIC);

  Status = UniXNUBlockWriteAt (Disk, FirstBlock * (UINT64)BlockSize, Bytes, Message);
  FreePool (Message);

  if (EFI_ERROR (Status)) {
    AsciiSPrint (Text, sizeof (Text),
                 "cannot write the bootloader message to misc (%r)", Status);
    ReportFatal (Text);
    return Status;
  }

  if (gST->ConOut != NULL) {
    Print (L"\r\nRestarting into fastboot...\r\n");
  }

  UniXNULog ("fastboot: bootloader message written, resetting");

  gBS->Stall (200000);
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

  //
  // ResetSystem only returns on failure.
  //
  ReportFatal ("reset returned; reboot the phone by hand");
  return EFI_DEVICE_ERROR;
}
