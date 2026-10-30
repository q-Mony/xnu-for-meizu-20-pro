/** @file
  Minimal GPT reader.

  The vendor storage driver on this board publishes the disk but no partitions
  appear on top of it, so both the log reader and the fastboot action have to
  find their partition in the GPT themselves.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Internal.h"

#include <Protocol/BlockIo.h>

#define GPT_SIGNATURE      "EFI PART"
#define GPT_MAX_PARTITIONS 128

/**
  Read Len bytes at a byte offset of a block device. Offset must be a multiple
  of the device block size.
**/
EFI_STATUS
UniXNUBlockReadAt (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  UINT64                 Offset,
  IN  UINTN                  Len,
  OUT VOID                   *Buffer
  )
{
  UINTN  BlockSize;

  if ((BlockIo == NULL) || (BlockIo->Media == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BlockSize = BlockIo->Media->BlockSize;
  if (BlockSize == 0) {
    return EFI_UNSUPPORTED;
  }

  if ((Offset % BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  return BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                              Offset / BlockSize,
                              ((Len + BlockSize - 1) / BlockSize) * BlockSize,
                              Buffer);
}

/**
  Write Len bytes at a byte offset of a block device, in whole blocks.
**/
EFI_STATUS
UniXNUBlockWriteAt (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN UINT64                 Offset,
  IN UINTN                  Len,
  IN CONST VOID             *Buffer
  )
{
  EFI_STATUS  Status;
  UINTN       BlockSize;
  UINTN       Blocks;
  VOID        *Copy;

  if ((BlockIo == NULL) || (BlockIo->Media == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BlockSize = BlockIo->Media->BlockSize;
  if (BlockSize == 0) {
    return EFI_UNSUPPORTED;
  }

  if ((Offset % BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (BlockIo->Media->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  //
  // A block write must move whole blocks, so stage a padded copy unless the
  // caller already handed over a block multiple in a large enough buffer.
  //
  Blocks = (Len + BlockSize - 1) / BlockSize;
  if ((Len % BlockSize) == 0) {
    Copy = (VOID *)Buffer;
  } else {
    Copy = AllocateZeroPool (Blocks * BlockSize);
    if (Copy == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    CopyMem (Copy, Buffer, Len);
  }

  Status = BlockIo->WriteBlocks (BlockIo, BlockIo->Media->MediaId,
                                 Offset / BlockSize,
                                 Blocks * BlockSize,
                                 Copy);

  if (Copy != Buffer) {
    FreePool (Copy);
  }

  return Status;
}

//
// Compare a GPT entry name (UTF-16LE, fixed field) with a narrow ASCII name.
//
STATIC
BOOLEAN
NameMatches (
  IN CONST UINT8   *Field,
  IN UINTN         FieldBytes,
  IN CONST CHAR8   *Name
  )
{
  UINTN  Index;
  UINTN  Len;

  Len = AsciiStrLen (Name);

  for (Index = 0; Index < Len; Index++) {
    UINT16  Unit;

    if ((Index + 1) * 2 > FieldBytes) {
      return FALSE;
    }

    CopyMem (&Unit, Field + Index * 2, sizeof (UINT16));
    if (Unit != (UINT16)(UINT8)Name[Index]) {
      return FALSE;
    }
  }

  if (Len * 2 >= FieldBytes) {
    return TRUE;
  }

  {
    UINT16  Term;
    CopyMem (&Term, Field + Len * 2, sizeof (UINT16));
    return Term == 0;
  }
}

//
// Walk one GPT header's entry table looking for Name.
//
STATIC
EFI_STATUS
FindInGpt (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  UINT64                 HeaderByte,
  IN  CONST CHAR8            *Name,
  OUT UINT64                 *FirstBlock,
  OUT UINT64                 *LastBlock
  )
{
  EFI_STATUS  Status;
  UINT8       *Header;
  UINT8       *Entries;
  UINT64      EntriesByte;
  UINT32      EntryCount;
  UINT32      EntrySize;
  UINTN       BlockSize;
  UINTN       Bytes;
  UINT32      Index;
  BOOLEAN     Found;

  BlockSize = BlockIo->Media->BlockSize;
  Header    = AllocatePool (BlockSize);
  if (Header == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = UniXNUBlockReadAt (BlockIo, HeaderByte, BlockSize, Header);
  if (EFI_ERROR (Status)) {
    FreePool (Header);
    return Status;
  }

  if (CompareMem (Header, GPT_SIGNATURE, 8) != 0) {
    FreePool (Header);
    return EFI_NOT_FOUND;
  }

  CopyMem (&EntriesByte, Header + 72, sizeof (UINT64));
  CopyMem (&EntryCount, Header + 80, sizeof (UINT32));
  CopyMem (&EntrySize, Header + 84, sizeof (UINT32));

  if ((EntryCount == 0) || (EntrySize < 128) || (EntryCount > GPT_MAX_PARTITIONS)) {
    FreePool (Header);
    return EFI_NOT_FOUND;
  }

  Bytes   = EntryCount * EntrySize;
  Entries = AllocatePool (Bytes);
  if (Entries == NULL) {
    FreePool (Header);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = UniXNUBlockReadAt (BlockIo, EntriesByte * BlockSize, Bytes, Entries);
  if (EFI_ERROR (Status)) {
    FreePool (Entries);
    FreePool (Header);
    return Status;
  }

  Found = FALSE;
  for (Index = 0; Index < EntryCount; Index++) {
    UINT8  *Entry  = Entries + (UINTN)Index * EntrySize;
    UINTN  Type;

    for (Type = 0; Type < 16; Type++) {
      if (Entry[Type] != 0) {
        break;
      }
    }

    if (Type == 16) {
      continue;   // Unused entry.
    }

    if (NameMatches (Entry + 56, 72, Name)) {
      CopyMem (FirstBlock, Entry + 32, sizeof (UINT64));
      CopyMem (LastBlock, Entry + 40, sizeof (UINT64));
      Found = TRUE;
      break;
    }
  }

  FreePool (Entries);
  FreePool (Header);

  return Found ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/**
  Find a partition by GPT name on one block device.

  Tries the primary GPT at LBA 1, then the backup in the final block, so a
  damaged primary header does not hide the partition.
**/
EFI_STATUS
UniXNUGptFind (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  CONST CHAR8            *Name,
  OUT UINT64                 *FirstBlock,
  OUT UINT64                 *LastBlock
  )
{
  EFI_STATUS  Status;
  UINTN       BlockSize;

  if ((BlockIo == NULL) || (BlockIo->Media == NULL) ||
      (Name == NULL) || (FirstBlock == NULL) || (LastBlock == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  if (!BlockIo->Media->MediaPresent) {
    return EFI_NO_MEDIA;
  }

  BlockSize = BlockIo->Media->BlockSize;
  if (BlockSize == 0) {
    return EFI_UNSUPPORTED;
  }

  Status = FindInGpt (BlockIo, (UINT64)BlockSize, Name, FirstBlock, LastBlock);
  if (!EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  return FindInGpt (BlockIo,
                    (UINT64)BlockIo->Media->LastBlock * BlockSize,
                    Name, FirstBlock, LastBlock);
}

/**
  Find a named partition on any whole disk in the system.
**/
EFI_STATUS
UniXNUGptFindAnyDisk (
  IN  CONST CHAR8  *Name,
  OUT EFI_BLOCK_IO_PROTOCOL  **Disk,
  OUT UINT64       *FirstBlock,
  OUT UINT64       *LastBlock
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  EFI_STATUS  Status;

  if ((Name == NULL) || (Disk == NULL) || (FirstBlock == NULL) || (LastBlock == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Handles = NULL;
  Count   = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || (Count == 0)) {
    return EFI_NOT_FOUND;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL  *BlockIo;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid,
                                        (VOID **)&BlockIo)) ||
        (BlockIo == NULL) || (BlockIo->Media == NULL))
    {
      continue;
    }

    //
    // Whole disks only; a logical partition has no GPT of its own.
    //
    if (BlockIo->Media->LogicalPartition || !BlockIo->Media->MediaPresent) {
      continue;
    }

    if (!EFI_ERROR (UniXNUGptFind (BlockIo, Name, FirstBlock, LastBlock))) {
      *Disk  = BlockIo;
      Status = EFI_SUCCESS;
      break;
    }
  }

  gBS->FreePool (Handles);
  return Status;
}
