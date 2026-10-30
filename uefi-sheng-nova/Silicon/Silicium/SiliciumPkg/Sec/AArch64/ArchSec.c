/**
  Copyright (c) 2012-2013, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>

#include "Sec.h"

VOID
SecCommonExceptionEntry (
  IN UINT32 Type,
  IN UINTN  LR)
{
  CHAR8 Buffer[100];
  UINTN CharCount;

  // Set Exception Message
  switch (Type) {
    case EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "Synchronous Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_IRQ:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "IRQ Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_FIQ:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "FIQ Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_SERROR:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "SError/Abort Exception at 0x%llx\n", LR);
      break;

    default:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "Unknown Exception at 0x%llx\n", LR);
      break;
  }

  // Show Exception Message
  SerialPortWrite ((UINT8 *)Buffer, CharCount);

  // Diagnostic: the fault registers say what actually happened (undefined instruction,
  // permission fault, translation fault) and at which EL it was taken.
  {
    STATIC volatile UINTN  InHandler;
    UINT64                 CurrentEl;
    UINT64                 Esr;
    UINT64                 Far;
    UINT64                 Spsr;
    UINT64                 Sctlr;
    UINT64                 Ttbr0;
    UINT64                 Sp;
    UINT32                 Ec;

    if (InHandler != 0) {
      // A fault while printing the first fault: keep the second ESR on screen and stop.
      InHandler = 2;
    } else {
      InHandler = 1;
    }

    __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
    if (((CurrentEl >> 2) & 3) == 2) {
      __asm__ volatile ("mrs %0, esr_el2" : "=r" (Esr));
      __asm__ volatile ("mrs %0, far_el2" : "=r" (Far));
      __asm__ volatile ("mrs %0, spsr_el2" : "=r" (Spsr));
      __asm__ volatile ("mrs %0, sctlr_el2" : "=r" (Sctlr));
      __asm__ volatile ("mrs %0, ttbr0_el2" : "=r" (Ttbr0));
    } else {
      __asm__ volatile ("mrs %0, esr_el1" : "=r" (Esr));
      __asm__ volatile ("mrs %0, far_el1" : "=r" (Far));
      __asm__ volatile ("mrs %0, spsr_el1" : "=r" (Spsr));
      __asm__ volatile ("mrs %0, sctlr_el1" : "=r" (Sctlr));
      __asm__ volatile ("mrs %0, ttbr0_el1" : "=r" (Ttbr0));
    }

    Ec = (UINT32)((Esr >> 26) & 0x3F);
    __asm__ volatile ("mov %0, sp" : "=r" (Sp));
    CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "  EL=%u SCTLR=0x%llx M=%u C=%u I=%u\n",
                             (UINT32)((CurrentEl >> 2) & 3), Sctlr,
                             (UINT32)(Sctlr & 1), (UINT32)((Sctlr >> 2) & 1), (UINT32)((Sctlr >> 12) & 1));
    SerialPortWrite ((UINT8 *)Buffer, CharCount);
    CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "  ESR=0x%llx EC=0x%x ISS=0x%x\n",
                             Esr, Ec, (UINT32)(Esr & 0x1FFFFFF));
    SerialPortWrite ((UINT8 *)Buffer, CharCount);
    CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "  FAR=0x%llx SPSR=0x%llx TTBR0=0x%llx\n",
                             Far, Spsr, Ttbr0);
    SerialPortWrite ((UINT8 *)Buffer, CharCount);
    CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "  SP=0x%llx\n", Sp);
    SerialPortWrite ((UINT8 *)Buffer, CharCount);

    // Show what is in memory at the reported address. If this page was handed to the
    // page allocator it now holds translation descriptors instead of our code.
    if (InHandler == 1) {
      volatile UINT32 *Code = (volatile UINT32 *)(LR & ~(UINTN)3);
      UINT32          W[8];
      UINTN           Index;

      for (Index = 0; Index < 8; Index++) {
        W[Index] = Code[Index];
      }

      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "  CODE %08x %08x %08x %08x\n",
                               W[0], W[1], W[2], W[3]);
      SerialPortWrite ((UINT8 *)Buffer, CharCount);
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "       %08x %08x %08x %08x\n",
                               W[4], W[5], W[6], W[7]);
      SerialPortWrite ((UINT8 *)Buffer, CharCount);
    }
  }

  // CPU Dead Loop
  CpuDeadLoop ();
}
