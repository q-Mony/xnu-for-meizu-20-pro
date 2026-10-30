/** @file
  Switch the Qualcomm A/B slot to Android (slot b) and reset.

  This runs from the UniXNU menu (slot a). Qualcomm stores the A/B state in
  the GPT partition attributes, byte 54 of each entry:
    bit 2 = SLOT_ACTIVE, bit 6 = BOOT_SUCCESSFUL, bit 7 = UNBOOTABLE.
  Every partition whose name ends in _a/_b is rewritten in both the primary
  and the backup GPT; CRCs are recomputed and verified. Only when both tables
  verify does the application reset the machine.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#pragma pack(1)
typedef struct {
  UINT64 Signature;
  UINT32 Revision;
  UINT32 HeaderSize;
  UINT32 HeaderCRC32;
  UINT32 Reserved;
  UINT64 MyLBA;
  UINT64 AlternateLBA;
  UINT64 FirstUsableLBA;
  UINT64 LastUsableLBA;
  EFI_GUID DiskGUID;
  UINT64 PartitionEntryLBA;
  UINT32 NumberOfPartitionEntries;
  UINT32 SizeOfPartitionEntry;
  UINT32 PartitionEntryArrayCRC32;
} GPT_HEADER;

typedef struct {
  EFI_GUID TypeGUID;
  EFI_GUID UniqueGUID;
  UINT64 StartingLBA;
  UINT64 EndingLBA;
  UINT64 Attributes;
  CHAR16 Name[36];
} GPT_ENTRY;
#pragma pack()

#define GPT_SIGNATURE 0x5452415020494645ULL /* "EFI PART" */
#define ATTR_BYTE     54                     /* 48 + 6 */
#define SLOT_ACTIVE   0x04
#define SLOT_SUCCESS  0x40
#define SLOT_UNBOOTABLE 0x80

//
// The menu prints the status this application returns, so each failure kind
// gets its own code: "No Media" = no block device at all, "Not Found" = block
// devices but none carries a GPT, "Write Protected" = GPT found but read-only,
// "Already Started" = the tables were already on slot b.
//
static UINTN  mGptDisks;
static UINTN  mReadOnlyGpt;
static UINTN  mWriteFailed;

static UINT32
Crc32 (
  IN CONST VOID  *Data,
  IN UINTN        Length
  )
{
  CONST UINT8  *P = Data;
  UINT32       Crc = 0xFFFFFFFF;
  UINTN        I;
  UINTN        K;

  for (I = 0; I < Length; I++) {
    Crc ^= P[I];
    for (K = 0; K < 8; K++) {
      Crc = (Crc >> 1) ^ (0xEDB88320U & (0U - (Crc & 1)));
    }
  }
  return ~Crc;
}

static BOOLEAN
NameIsSlot (
  IN CONST GPT_ENTRY  *Entry,
  OUT CHAR8           *Suffix
  )
{
  UINTN  N = 0;

  while (N < 36 && Entry->Name[N] != 0) {
    N++;
  }
  if (N < 2 || Entry->Name[N - 2] != L'_') {
    return FALSE;
  }
  if (Entry->Name[N - 1] == L'a' || Entry->Name[N - 1] == L'b') {
    *Suffix = (CHAR8)Entry->Name[N - 1];
    return TRUE;
  }
  return FALSE;
}

/* Rewrite one table pair: entries at EntryLba, header at HeaderLba. */
static EFI_STATUS
CommitTable (
  IN EFI_BLOCK_IO_PROTOCOL  *Io,
  IN UINT64                  HeaderLba,
  IN UINT64                  EntryLba,
  IN UINT32                  EntryCount,
  IN UINT32                  EntrySize,
  IN GPT_ENTRY              *Entries
  )
{
  EFI_STATUS  Status;
  UINT8      *Block;
  GPT_HEADER *Header;
  UINT32      EntriesBytes = EntryCount * EntrySize;
  UINT32      Crc;

  Status = Io->WriteBlocks (Io, Io->Media->MediaId, EntryLba, EntriesBytes, Entries);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Block = AllocateZeroPool (Io->Media->BlockSize);
  if (Block == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Status = Io->ReadBlocks (Io, Io->Media->MediaId, HeaderLba, Io->Media->BlockSize, Block);
  if (EFI_ERROR (Status)) {
    FreePool (Block);
    return Status;
  }
  Header = (GPT_HEADER *)Block;
  if (Header->Signature != GPT_SIGNATURE || Header->HeaderSize < sizeof (GPT_HEADER)) {
    FreePool (Block);
    return EFI_VOLUME_CORRUPTED;
  }
  Header->PartitionEntryArrayCRC32 = Crc32 (Entries, EntriesBytes);
  Header->HeaderCRC32 = 0;
  Crc = Crc32 (Header, Header->HeaderSize);
  Header->HeaderCRC32 = Crc;
  Status = Io->WriteBlocks (Io, Io->Media->MediaId, HeaderLba, Io->Media->BlockSize, Block);
  FreePool (Block);
  return Status;
}

/* Verify the entries CRC of one table; returns TRUE when it matches. */
static BOOLEAN
VerifyTable (
  IN EFI_BLOCK_IO_PROTOCOL  *Io,
  IN UINT64                  HeaderLba,
  IN UINT32                  EntryCount,
  IN UINT32                  EntrySize
  )
{
  EFI_STATUS  Status;
  UINT8      *Block;
  GPT_HEADER *Header;
  UINT8      *Entries;
  UINT32      Bytes = EntryCount * EntrySize;
  BOOLEAN     Ok = FALSE;

  Block = AllocatePool (Io->Media->BlockSize);
  if (Block == NULL) {
    return FALSE;
  }
  Status = Io->ReadBlocks (Io, Io->Media->MediaId, HeaderLba, Io->Media->BlockSize, Block);
  if (!EFI_ERROR (Status)) {
    Header = (GPT_HEADER *)Block;
    if (Header->Signature == GPT_SIGNATURE) {
      Entries = AllocatePool (Bytes);
      if (Entries != NULL) {
        Status = Io->ReadBlocks (Io, Io->Media->MediaId, Header->PartitionEntryLBA, Bytes, Entries);
        if (!EFI_ERROR (Status) && Crc32 (Entries, Bytes) == Header->PartitionEntryArrayCRC32) {
          Ok = TRUE;
        }
        FreePool (Entries);
      }
    }
  }
  FreePool (Block);
  return Ok;
}

static UINTN
SwitchDisk (
  IN EFI_BLOCK_IO_PROTOCOL  *Io
  )
{
  EFI_STATUS  Status;
  UINT8      *Probe;
  UINTN       ProbeSize = 8192;
  UINTN       HeaderOff = 0;
  GPT_HEADER *Header;
  GPT_HEADER *Backup;
  GPT_ENTRY  *Entries;
  UINT32      EntryBytes;
  UINTN       I;
  UINTN       Changed = 0;
  CHAR8       Suffix;

  if (!Io->Media->MediaPresent) {
    return 0;
  }
  if (Io->Media->BlockSize == 0 || ProbeSize < Io->Media->BlockSize * 2) {
    return 0;
  }
  Probe = AllocatePool (ProbeSize);
  if (Probe == NULL) {
    return 0;
  }
  Status = Io->ReadBlocks (Io, Io->Media->MediaId, 0, ProbeSize, Probe);
  if (EFI_ERROR (Status)) {
    FreePool (Probe);
    return 0;
  }
  if (*(UINT64 *)(Probe + 512) == GPT_SIGNATURE) {
    HeaderOff = 512;
  } else if (*(UINT64 *)(Probe + 4096) == GPT_SIGNATURE) {
    HeaderOff = 4096;
  }
  if (HeaderOff == 0 || (HeaderOff % Io->Media->BlockSize) != 0) {
    FreePool (Probe);
    return 0;
  }
  Header = (GPT_HEADER *)(Probe + HeaderOff);
  if (Header->HeaderSize < sizeof (GPT_HEADER) ||
      Header->SizeOfPartitionEntry < sizeof (GPT_ENTRY) ||
      Header->NumberOfPartitionEntries == 0 ||
      Header->NumberOfPartitionEntries > 256) {
    FreePool (Probe);
    return 0;
  }
  EntryBytes = Header->NumberOfPartitionEntries * Header->SizeOfPartitionEntry;
  if ((EntryBytes % Io->Media->BlockSize) != 0) {
    FreePool (Probe);
    return 0;
  }
  Entries = AllocatePool (EntryBytes);
  if (Entries == NULL) {
    FreePool (Probe);
    return 0;
  }
  Status = Io->ReadBlocks (Io, Io->Media->MediaId, Header->PartitionEntryLBA, EntryBytes, Entries);
  if (EFI_ERROR (Status)) {
    FreePool (Entries);
    FreePool (Probe);
    return 0;
  }

  if (Io->Media->ReadOnly) {
    mReadOnlyGpt++;
    FreePool (Entries);
    FreePool (Probe);
    return 0;
  }
  mGptDisks++;

  //
  // Validate the backup table before touching either one: a half-written pair
  // is worse than no switch at all.
  //
  Backup = AllocatePool (Io->Media->BlockSize);
  if ((Header->AlternateLBA == 0) || (Header->AlternateLBA == Header->MyLBA) || (Backup == NULL)) {
    FreePool (Entries);
    FreePool (Probe);
    if (Backup != NULL) {
      FreePool (Backup);
    }
    return 0;
  }
  Status = Io->ReadBlocks (Io, Io->Media->MediaId, Header->AlternateLBA, Io->Media->BlockSize, Backup);
  if (EFI_ERROR (Status) || (Backup->Signature != GPT_SIGNATURE) ||
      (Backup->HeaderSize < sizeof (GPT_HEADER)) ||
      (Backup->NumberOfPartitionEntries != Header->NumberOfPartitionEntries) ||
      (Backup->SizeOfPartitionEntry != Header->SizeOfPartitionEntry) ||
      (Backup->PartitionEntryLBA == 0) ||
      (((UINT64)Backup->NumberOfPartitionEntries * Backup->SizeOfPartitionEntry) % Io->Media->BlockSize) != 0) {
    FreePool (Backup);
    FreePool (Entries);
    FreePool (Probe);
    return 0;
  }
  FreePool (Backup);

  for (I = 0; I < Header->NumberOfPartitionEntries; I++) {
    GPT_ENTRY *Entry = (GPT_ENTRY *)((UINT8 *)Entries + I * Header->SizeOfPartitionEntry);
    UINT8     *A;

    if (Entry->TypeGUID.Data1 == 0 && Entry->TypeGUID.Data2 == 0 &&
        Entry->TypeGUID.Data3 == 0 && *(UINT64 *)Entry->TypeGUID.Data4 == 0) {
      continue;
    }
    if (!NameIsSlot (Entry, &Suffix)) {
      continue;
    }
    A = (UINT8 *)Entry + ATTR_BYTE;
    if (Suffix == 'b') {
      *A = (UINT8)((*A & ~SLOT_UNBOOTABLE) | SLOT_SUCCESS | SLOT_ACTIVE);
    } else {
      // Same edit bootctl/fastboot make: give up the active flag, clear a
      // stale unbootable mark, and leave the success flag as it was.
      *A = (UINT8)(*A & ~(SLOT_ACTIVE | SLOT_UNBOOTABLE));
    }
    Changed++;
  }

  if (Changed != 0) {
    Status = CommitTable (Io, Header->MyLBA, Header->PartitionEntryLBA,
                          Header->NumberOfPartitionEntries, Header->SizeOfPartitionEntry, Entries);
    if (!EFI_ERROR (Status)) {
      Status = Io->ReadBlocks (Io, Io->Media->MediaId, Header->AlternateLBA, Io->Media->BlockSize, Backup);
      if (!EFI_ERROR (Status) && Backup->Signature == GPT_SIGNATURE) {
        Status = CommitTable (Io, Header->AlternateLBA, Backup->PartitionEntryLBA,
                              Backup->NumberOfPartitionEntries, Backup->SizeOfPartitionEntry, Entries);
        if (!EFI_ERROR (Status)) {
          if (!VerifyTable (Io, Header->MyLBA, Header->NumberOfPartitionEntries, Header->SizeOfPartitionEntry) ||
              !VerifyTable (Io, Header->AlternateLBA, Backup->NumberOfPartitionEntries, Backup->SizeOfPartitionEntry)) {
            Status = EFI_VOLUME_CORRUPTED;
          }
        }
      }
    }
    if (EFI_ERROR (Status)) {
      Print (L"BootAndroid: GPT write failed: %r\n", Status);
      mWriteFailed++;
      Changed = 0; /* do not reset on a bad table */
    } else {
      Print (L"BootAndroid: slot b active, %u entries\n", (UINT32)Changed);
    }
  }

  FreePool (Backup);
  FreePool (Entries);
  FreePool (Probe);
  return Changed;
}

EFI_STATUS
EFIAPI
BootAndroidMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE *Handles = NULL;
  UINTN       Count = 0;
  UINTN       I;
  UINTN       Total = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Print (L"BootAndroid: no block io: %r\n", Status);
    return EFI_NO_MEDIA;
  }
  for (I = 0; I < Count; I++) {
    EFI_BLOCK_IO_PROTOCOL *Io;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Io))) {
      continue;
    }
    Total += SwitchDisk (Io);
  }
  FreePool (Handles);

  if (Total == 0) {
    Print (L"BootAndroid: %u block handles, %u GPT disks, %u read-only GPT\n",
           (UINT32)Count, (UINT32)mGptDisks, (UINT32)mReadOnlyGpt);
    if (mWriteFailed != 0) {
      return EFI_DEVICE_ERROR;
    }
    if (mGptDisks != 0) {
      return EFI_ALREADY_STARTED;
    }
    if (mReadOnlyGpt != 0) {
      return EFI_WRITE_PROTECTED;
    }
    return EFI_NOT_FOUND;
  }

  Print (L"BootAndroid: resetting to Android\n");
  gBS->Stall (200000);
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
