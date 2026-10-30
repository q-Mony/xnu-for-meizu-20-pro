/** @file
  Show the kernel console log without booting into the OS.

  NativeUfs keeps a ring of console text and republishes it into the tail of
  its GPT partition: a MEIZULOG header page followed by the ring text. When the
  kernel stalls before any process can read that back, the firmware is the only
  thing left that can show it, so this reads the window straight off the block
  device and pages it on the console.

  The window sits a fixed distance from the end of the partition, so this finds
  it from the partition's own size and needs no configuration.

  The GPT is walked here rather than relying on PartitionDxe having produced
  child block handles: on this board the vendor storage driver publishes the
  disk but no partitions appear on top of it, and the log lives inside one.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Internal.h"

#include <Protocol/BlockIo.h>

//
// Must match drivers/NativeUfs/NativeUfs.cpp, which owns the writer side.
//
#define MEIZU_LOG_RESERVE_BYTES   (8192ULL * 4096ULL)   ///< kLogReserveBlocks * 4 KiB.
#define MEIZU_LOG_BLOCK_SIZE      4096ULL               ///< NativeUfs writes 4 KiB units.
#define MEIZU_LOG_MAGIC           "MEIZULOG"

#define LOG_LINE_MAX              480       ///< Longest console line rendered.
#define LOG_LINES_MAX             8192      ///< Ring lines indexed for paging.

#define GPT_SIGNATURE             "EFI PART"
#define GPT_MAX_PARTITIONS        128

typedef struct {
  CHAR8    Magic[8];
  UINT64   Total;
  UINT64   Bytes;
  UINT32   Dropped;
  UINT32   Reserved;
} MEIZU_LOG_HEADER;

STATIC CHAR8   *mLogWindow;      ///< Whole window: header page then ring text.
STATIC UINTN   mLogUsed;         ///< Bytes of the window that carry text.
STATIC UINT64  mLogTotal;        ///< Ring writes the kernel reports.
STATIC UINT32  mLogDropped;      ///< Ring overwrites the kernel reports.
STATIC CHAR8   mDiag[512];       ///< What the search saw, for the failure screen.
STATIC UINTN   mDiagLen;

STATIC
VOID
Diag (
  IN CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Args;
  UINTN    Room;

  if (mDiagLen + 1 >= sizeof (mDiag)) {
    return;
  }

  Room = sizeof (mDiag) - mDiagLen;

  VA_START (Args, Format);
  AsciiVSPrint (mDiag + mDiagLen, Room, Format, Args);
  VA_END (Args);

  mDiagLen = AsciiStrLen (mDiag);
}

//
// Read the window that sits at the end of one extent and described by
// [FirstBlock, LastBlock] in device blocks. Loads mLogWindow on success.
//
STATIC
EFI_STATUS
LoadWindow (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  UINT64                 FirstBlock,
  IN  UINT64                 LastBlock,
  OUT CHAR8                  *Why,
  IN  UINTN                  WhySize
  )
{
  EFI_STATUS        Status;
  UINTN             BlockSize;
  UINT64             EndByte;
  UINT64             WindowByte;
  UINT64             Want;
  MEIZU_LOG_HEADER  *Header;
  UINTN             Offset;

  BlockSize = BlockIo->Media->BlockSize;

  if (BlockSize == 0) {
    AsciiSPrint (Why, WhySize, "zero block size");
    return EFI_UNSUPPORTED;
  }

  if (LastBlock < FirstBlock) {
    AsciiSPrint (Why, WhySize, "empty extent");
    return EFI_NOT_FOUND;
  }

  //
  // ReadAt works in whole device blocks, and the caller sized mLogWindow for
  // 4 KiB blocks. Anything coarser would overrun it.
  //
  if (BlockSize > MEIZU_LOG_BLOCK_SIZE) {
    AsciiSPrint (Why, WhySize, "block size %u too coarse", (UINT32)BlockSize);
    return EFI_UNSUPPORTED;
  }

  EndByte = (LastBlock + 1) * (UINT64)BlockSize;

  if (EndByte < MEIZU_LOG_RESERVE_BYTES) {
    AsciiSPrint (Why, WhySize, "too small");
    return EFI_NOT_FOUND;
  }

  WindowByte = EndByte - MEIZU_LOG_RESERVE_BYTES;
  WindowByte -= WindowByte % BlockSize;

  if (WindowByte < FirstBlock * (UINT64)BlockSize) {
    AsciiSPrint (Why, WhySize, "window before extent");
    return EFI_NOT_FOUND;
  }

  //
  // Header page first: it says whether this is a log at all, and how much of
  // the ring is real.
  //
  Status = UniXNUBlockReadAt (BlockIo, WindowByte, MEIZU_LOG_BLOCK_SIZE, mLogWindow);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Why, WhySize, "header read: %r", Status);
    return Status;
  }

  Header = (MEIZU_LOG_HEADER *)mLogWindow;
  if (CompareMem (Header->Magic, MEIZU_LOG_MAGIC, 8) != 0) {
    AsciiSPrint (Why, WhySize, "not a log window");
    return EFI_NOT_FOUND;
  }

  mLogTotal   = Header->Total;
  mLogDropped = Header->Dropped;

  Want = MEIZU_LOG_BLOCK_SIZE + Header->Bytes;
  if (Want > MEIZU_LOG_RESERVE_BYTES) {
    Want = MEIZU_LOG_RESERVE_BYTES;
  }

  Want = (Want + MEIZU_LOG_BLOCK_SIZE - 1) & ~(MEIZU_LOG_BLOCK_SIZE - 1);

  //
  // Pull the rest in bounded chunks; a single huge request is what a flaky
  // transfer cannot survive.
  //
  Offset = MEIZU_LOG_BLOCK_SIZE;
  while (Offset < (UINTN)Want) {
    UINTN  Chunk = (UINTN)Want - Offset;

    if (Chunk > 128 * MEIZU_LOG_BLOCK_SIZE) {
      Chunk = 128 * MEIZU_LOG_BLOCK_SIZE;
    }

    Status = UniXNUBlockReadAt (BlockIo, WindowByte + Offset, Chunk, mLogWindow + Offset);
    if (EFI_ERROR (Status)) {
      AsciiSPrint (Why, WhySize, "ring read at +%u: %r", (UINT32)Offset, Status);
      return Status;
    }

    Offset += Chunk;
  }

  mLogUsed = Offset;

  //
  // Blank the header page so the text reads as console output, not a struct.
  //
  SetMem (mLogWindow, MEIZU_LOG_BLOCK_SIZE, ' ');

  return EFI_SUCCESS;
}

//
// Walk the GPT of one disk and try every entry, using the shared reader.
//
STATIC
BOOLEAN
TryGptOfDisk (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo
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
  UINT64      GptHeaderByte;

  BlockSize = BlockIo->Media->BlockSize;
  Header    = AllocatePool (BlockSize);
  if (Header == NULL) {
    return FALSE;
  }

  GptHeaderByte = (UINT64)BlockSize;
  Status        = UniXNUBlockReadAt (BlockIo, GptHeaderByte, BlockSize, Header);
  if (EFI_ERROR (Status)) {
    Diag ("gpt header read: %r. ", Status);
    FreePool (Header);
    return FALSE;
  }

  if (CompareMem (Header, GPT_SIGNATURE, 8) != 0) {
    Diag ("no gpt signature. ");
    FreePool (Header);
    return FALSE;
  }

  CopyMem (&EntriesByte, Header + 72, sizeof (UINT64));
  CopyMem (&EntryCount, Header + 80, sizeof (UINT32));
  CopyMem (&EntrySize, Header + 84, sizeof (UINT32));

  if ((EntryCount == 0) || (EntrySize < 128) || (EntryCount > GPT_MAX_PARTITIONS)) {
    Diag ("gpt entries %u x %u unusable. ", EntryCount, EntrySize);
    FreePool (Header);
    return FALSE;
  }

  Bytes   = EntryCount * EntrySize;
  Entries = AllocatePool (Bytes);
  if (Entries == NULL) {
    FreePool (Header);
    return FALSE;
  }

  Status = UniXNUBlockReadAt (BlockIo, EntriesByte * BlockSize, Bytes, Entries);
  if (EFI_ERROR (Status)) {
    Diag ("gpt table read: %r. ", Status);
    FreePool (Entries);
    FreePool (Header);
    return FALSE;
  }

  Diag ("gpt ok, %u entries. ", EntryCount);

  for (Index = 0; Index < EntryCount; Index++) {
    UINT8   *Entry = Entries + (UINTN)Index * EntrySize;
    UINT64  Start;
    UINT64  Last;
    CHAR8   Why[64];
    UINTN   Type;

    for (Type = 0; Type < 16; Type++) {
      if (Entry[Type] != 0) {
        break;
      }
    }

    if (Type == 16) {
      continue;
    }

    CopyMem (&Start, Entry + 32, sizeof (UINT64));
    CopyMem (&Last, Entry + 40, sizeof (UINT64));

    Why[0] = '\0';
    if (!EFI_ERROR (LoadWindow (BlockIo, Start, Last, Why, sizeof (Why)))) {
      Diag ("window in entry %u. ", Index + 1);
      FreePool (Entries);
      FreePool (Header);
      return TRUE;
    }
  }

  FreePool (Entries);
  FreePool (Header);
  return FALSE;
}

//
// Try one disk: its GPT, then the disk tail as a last resort.
//
STATIC
BOOLEAN
TryDisk (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo
  )
{
  CHAR8  Why[64];

  if (TryGptOfDisk (BlockIo)) {
    return TRUE;
  }

  //
  // Last resort: treat the whole disk as the extent. Correct only when the
  // kernel's partition runs to the end of the disk.
  //
  Why[0] = '\0';
  if (!EFI_ERROR (LoadWindow (BlockIo, 0, BlockIo->Media->LastBlock, Why, sizeof (Why)))) {
    Diag ("window at disk tail. ");
    return TRUE;
  }

  return FALSE;
}

//
// Try every block device: partitions first, then whole disks.
//
STATIC
EFI_STATUS
ReadFromPartitions (
  OUT CHAR8  *Why,
  IN  UINTN  WhySize
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  EFI_STATUS  Status;
  UINTN       Logical;
  UINTN       Disks;

  Handles   = NULL;
  Count     = 0;
  Logical   = 0;
  Disks     = 0;
  mDiagLen  = 0;
  mDiag[0]  = '\0';

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || (Count == 0)) {
    AsciiSPrint (Why, WhySize, "no block devices (%r)", Status);
    return EFI_NOT_FOUND;
  }

  Diag ("%u block devices. ", (UINT32)Count);

  //
  // Pass 1: handles PartitionDxe already produced.
  //
  for (Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL  *BlockIo;
    CHAR8                  LocalWhy[64];

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid,
                                        (VOID **)&BlockIo)) ||
        (BlockIo == NULL) || (BlockIo->Media == NULL))
    {
      continue;
    }

    if (!BlockIo->Media->LogicalPartition) {
      continue;
    }

    Logical++;

    LocalWhy[0] = '\0';
    if (!EFI_ERROR (LoadWindow (BlockIo, 0, BlockIo->Media->LastBlock,
                                LocalWhy, sizeof (LocalWhy))))
    {
      gBS->FreePool (Handles);
      AsciiSPrint (Why, WhySize, "ok");
      return EFI_SUCCESS;
    }
  }

  //
  // Pass 2: whole disks. The vendor storage driver publishes the disk without
  // any partitions on top, so the GPT has to be walked by hand.
  //
  for (Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL  *BlockIo;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid,
                                        (VOID **)&BlockIo)) ||
        (BlockIo == NULL) || (BlockIo->Media == NULL))
    {
      continue;
    }

    if (BlockIo->Media->LogicalPartition || !BlockIo->Media->MediaPresent) {
      continue;
    }

    Disks++;
    Diag ("disk%u bs=%u last=%lu. ",
          (UINT32)Disks,
          (UINT32)BlockIo->Media->BlockSize,
          (UINT64)BlockIo->Media->LastBlock);

    if (TryDisk (BlockIo)) {
      gBS->FreePool (Handles);
      AsciiSPrint (Why, WhySize, "ok");
      return EFI_SUCCESS;
    }
  }

  gBS->FreePool (Handles);

  AsciiSPrint (Why, WhySize, "%u partitions, %u disks; no %a window. %a",
               (UINT32)Logical, (UINT32)Disks, MEIZU_LOG_MAGIC, mDiag);
  return EFI_NOT_FOUND;
}

//
// Record where each console line starts.
//
STATIC
UINTN
IndexLines (
  OUT UINTN  *Starts,
  IN  UINTN  MaxLines
  )
{
  UINTN  Count;
  UINTN  Pos;

  if (MaxLines == 0) {
    return 0;
  }

  Count     = 1;
  Starts[0] = 0;
  Pos       = 0;

  while (Pos < mLogUsed) {
    if (mLogWindow[Pos] == '\n') {
      if ((Count < MaxLines) && ((Pos + 1) < mLogUsed)) {
        Starts[Count++] = Pos + 1;
      }
    }

    Pos++;
  }

  if ((Count > 1) && (Starts[Count - 1] >= mLogUsed)) {
    Count--;
  }

  return Count;
}

//
// Copy one line out as printable ASCII.
//
STATIC
VOID
ExtractLine (
  IN  UINTN  Start,
  IN  UINTN  End,
  OUT CHAR8  *Out,
  IN  UINTN  OutSize
  )
{
  UINTN  Pos;
  UINTN  Written;

  Written = 0;
  for (Pos = Start; (Pos < End) && ((Written + 1) < OutSize); Pos++) {
    CHAR8  C = mLogWindow[Pos];

    if ((C == '\n') || (C == '\r')) {
      break;
    }

    //
    // Some kernel paths emit a literal backslash-n; show one space instead.
    //
    if ((C == '\\') && ((Pos + 1) < End) && (mLogWindow[Pos + 1] == 'n')) {
      Out[Written++] = ' ';
      Pos++;
      continue;
    }

    if (((UINT8)C < 0x20) || (C == 0x7F)) {
      C = ' ';
    }

    Out[Written++] = C;
  }

  Out[Written] = '\0';
}

//
// Wait for the next key.
//
STATIC
EFI_STATUS
WaitKey (
  OUT EFI_INPUT_KEY  *Key
  )
{
  UINTN  Index;

  Key->UnicodeChar = 0;
  Key->ScanCode    = 0;

  if (gST->ConIn == NULL) {
    return EFI_NOT_READY;
  }

  if (EFI_ERROR (gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index))) {
    return EFI_NOT_READY;
  }

  return gST->ConIn->ReadKeyStroke (gST->ConIn, Key);
}

STATIC
VOID
ShowPage (
  IN UINTN  *Starts,
  IN UINTN  LineCount,
  IN UINTN  FirstLine
  )
{
  UINTN   Row;
  CHAR8   Line[LOG_LINE_MAX];
  CHAR16  Wide[LOG_LINE_MAX];

  if (gST->ConOut == NULL) {
    return;
  }

  gST->ConOut->SetCursorPosition (gST->ConOut, 0, 0);
  gST->ConOut->ClearScreen (gST->ConOut);

  Print (L"UniXNU kernel console  |  lines %u-%u of %u  |  %lu bytes  |  %u dropped\r\n\r\n",
         (UINT32)(FirstLine + 1),
         (UINT32)MIN (FirstLine + 20, LineCount),
         (UINT32)LineCount,
         (UINT64)mLogTotal,
         (UINT32)mLogDropped);

  for (Row = 0; (Row < 20) && ((FirstLine + Row) < LineCount); Row++) {
    UINTN  LineNo = FirstLine + Row;
    UINTN  Start  = Starts[LineNo];
    UINTN  End    = ((LineNo + 1) < LineCount) ? Starts[LineNo + 1] : mLogUsed;

    ExtractLine (Start, End, Line, sizeof (Line));
    AsciiStrToUnicodeStrS (Line, Wide, ARRAY_SIZE (Wide));
    Print (L"%s\r\n", Wide);
  }
}

STATIC
EFI_STATUS
ShowLog (
  VOID
  )
{
  EFI_STATUS  Status;
  CHAR8       Why[192];
  UINTN       *Starts;
  UINTN       LineCount;
  UINTN       FirstLine;
  UINTN       Page;

  if (mLogWindow == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Why[0] = '\0';
  Status = ReadFromPartitions (Why, sizeof (Why));
  if (EFI_ERROR (Status)) {
    UniXNULog ("log: %a", Why);

    if (gST->ConOut != NULL) {
      gST->ConOut->SetCursorPosition (gST->ConOut, 0, 0);
      gST->ConOut->ClearScreen (gST->ConOut);
      Print (L"UniXNU log: no window found\r\n\r\n%a\r\n\r\nPress any key.\r\n", Why);
    }

    {
      EFI_INPUT_KEY  Ignored;
      WaitKey (&Ignored);
    }

    return Status;
  }

  Starts = AllocateZeroPool (LOG_LINES_MAX * sizeof (UINTN));
  if (Starts == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  LineCount = IndexLines (Starts, LOG_LINES_MAX);
  if (LineCount == 0) {
    gBS->FreePool (Starts);
    UniXNULog ("log: window holds no text");
    return EFI_NOT_FOUND;
  }

  Page      = 20;
  FirstLine = (LineCount > Page) ? (LineCount - Page) : 0;

  for (;;) {
    EFI_INPUT_KEY  Key;

    ShowPage (Starts, LineCount, FirstLine);

    if (EFI_ERROR (WaitKey (&Key))) {
      break;
    }

    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key.ScanCode == SCAN_ESC)) {
      break;
    }

    if ((Key.ScanCode == SCAN_UP) || (Key.UnicodeChar == L'k')) {
      FirstLine = (FirstLine > Page) ? (FirstLine - Page) : 0;
    } else if ((Key.ScanCode == SCAN_DOWN) || (Key.UnicodeChar == L'j')) {
      if ((FirstLine + Page) < LineCount) {
        FirstLine += Page;
        if ((FirstLine + Page) > LineCount) {
          FirstLine = LineCount - Page;
        }
      }
    } else if (Key.ScanCode == SCAN_HOME) {
      FirstLine = 0;
    } else if (Key.ScanCode == SCAN_END) {
      FirstLine = (LineCount > Page) ? (LineCount - Page) : 0;
    }
  }

  gBS->FreePool (Starts);
  return EFI_SUCCESS;
}

EFI_STATUS
UniXNULogShow (
  VOID
  )
{
  UINTN  Bytes;

  if (mLogWindow == NULL) {
    Bytes      = (UINTN)MEIZU_LOG_RESERVE_BYTES;
    mLogWindow = AllocatePages (EFI_SIZE_TO_PAGES (Bytes));
    if (mLogWindow == NULL) {
      UniXNULog ("log: no memory for a %u byte window", (UINT32)Bytes);
      return EFI_OUT_OF_RESOURCES;
    }

    mLogUsed = Bytes;
  }

  return ShowLog ();
}
