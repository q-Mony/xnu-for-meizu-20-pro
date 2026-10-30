// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef DARWIN_BOOT_GOP_RESERVED_PAGES_H_
#define DARWIN_BOOT_GOP_RESERVED_PAGES_H_

#include <Uefi.h>

// A single final-map reserved or MMIO descriptor must cover the full GOP
// mapping outside the XNU-managed boot-args window.
STATIC
BOOLEAN
GopReservedPages (EFI_MEMORY_DESCRIPTOR *Map, UINTN Size, UINTN Stride, UINT64 Base, UINT64 Bytes, UINT64 KernelBase, UINT64 KernelBytes, UINT64 *ReservedBase, UINT64 *ReservedBytes)
{
  UINTN Offset;
  UINT64 End;
  if ((Map == NULL) || (ReservedBase == NULL) || (ReservedBytes == NULL) ||
      (Stride < sizeof (EFI_MEMORY_DESCRIPTOR)) || (Base & EFI_PAGE_MASK) ||
      (Bytes == 0) || (Bytes > MAX_UINT64 - EFI_PAGE_MASK) ||
      (Base > MAX_UINT64 - ALIGN_VALUE (Bytes, EFI_PAGE_SIZE)) ||
      (KernelBase > MAX_UINT64 - KernelBytes)) { return FALSE; }
  End = Base + ALIGN_VALUE (Bytes, EFI_PAGE_SIZE);
  if ((Base < KernelBase + KernelBytes) && (KernelBase < End)) { return FALSE; }
  for (Offset = 0; Offset < Size && Size - Offset >= Stride; Offset += Stride) {
    EFI_MEMORY_DESCRIPTOR *D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    if (((D->Type == EfiMemoryMappedIO) || (D->Type == EfiReservedMemoryType)) && (D->NumberOfPages != 0) &&
        (D->NumberOfPages <= (MAX_UINT64 - D->PhysicalStart) / EFI_PAGE_SIZE) &&
        (D->PhysicalStart <= Base) &&
        (D->PhysicalStart + EFI_PAGES_TO_SIZE (D->NumberOfPages) >= End)) {
      *ReservedBase = D->PhysicalStart;
      *ReservedBytes = EFI_PAGES_TO_SIZE (D->NumberOfPages);
      return TRUE;
    }
  }
  return FALSE;
}

#endif
