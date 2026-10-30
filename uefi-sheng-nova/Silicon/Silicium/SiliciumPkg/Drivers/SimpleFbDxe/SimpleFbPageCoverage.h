#ifndef SIMPLE_FB_PAGE_COVERAGE_H_
#define SIMPLE_FB_PAGE_COVERAGE_H_

#include <Uefi.h>
#include <Library/MemoryMapLib.h>

// A GOP mapping may include its final partial page only when both the named
// platform reservation and a live firmware descriptor independently cover it.
STATIC
BOOLEAN
SimpleFbPageCoverage (
  IN CONST EFI_MEMORY_REGION_DESCRIPTOR *Region,
  IN CONST EFI_MEMORY_DESCRIPTOR        *Map,
  IN UINTN                               MapSize,
  IN UINTN                               Stride,
  IN EFI_PHYSICAL_ADDRESS                Base,
  IN UINT64                              VisibleBytes,
  OUT UINT64                             *RoundedBytes
  )
{
  UINT64 Bytes;
  UINT64 End;
  UINTN  Offset;

  if ((Region == NULL) || (Map == NULL) || (RoundedBytes == NULL) ||
      (Stride < sizeof (EFI_MEMORY_DESCRIPTOR)) || (Base & EFI_PAGE_MASK) ||
      (VisibleBytes == 0) || (VisibleBytes > MAX_UINT64 - EFI_PAGE_MASK) ||
      (Region->MemoryType != EfiReservedMemoryType) ||
      (Region->ResourceType != EFI_RESOURCE_MEMORY_RESERVED) ||
      (Region->Address != Base)) {
    return FALSE;
  }
  Bytes = (VisibleBytes + EFI_PAGE_MASK) & ~(UINT64)EFI_PAGE_MASK;
  if ((Base > MAX_UINT64 - Bytes) || (Region->Length < Bytes)) {
    return FALSE;
  }
  End = Base + Bytes;
  for (Offset = 0; Offset < MapSize && MapSize - Offset >= Stride; Offset += Stride) {
    CONST EFI_MEMORY_DESCRIPTOR *Descriptor = (CONST EFI_MEMORY_DESCRIPTOR *)((CONST UINT8 *)Map + Offset);
    if ((Descriptor->Type == EfiReservedMemoryType) &&
        (Descriptor->PhysicalStart <= Base) &&
        (Descriptor->NumberOfPages <= (MAX_UINT64 - Descriptor->PhysicalStart) / EFI_PAGE_SIZE) &&
        (Descriptor->PhysicalStart + EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages) >= End)) {
      *RoundedBytes = Bytes;
      return TRUE;
    }
  }
  return FALSE;
}

#endif
