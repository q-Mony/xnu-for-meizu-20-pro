/** @file
  Meizu 20 Pro glue for the DEPEX of this phone's own DisplayDxe.

  The unchanged DisplayDxe from this phone's uefi partition requires nine
  protocol GUIDs. Seven are installed by the other vendor drivers from the
  same image. The remaining two GUIDs appear nowhere else in that image and
  are not referenced by any code inside DisplayDxe itself; they are vestigial
  dependencies of the stock build's dispatcher environment. This board driver
  installs empty flag protocols for them so the genuine EDK2 dispatcher can
  satisfy the original, unmodified DEPEX. No vendor byte is changed and no
  display state is touched here.

  Loaded only with USE_CUSTOM_DISPLAY_DRIVER = 1 (see meizu20pro.dsc).
**/

#include <Library/UefiBootServicesTableLib.h>
#include <Uefi.h>

//
// A966CC4F-EE6C-424D-A762-8ED3B5A4A501
// Orphan GUID pushed by the stock DisplayDxe DEPEX.
//
STATIC CONST EFI_GUID mMeizuDisplayDepexProtocol1Guid = {
  0xA966CC4F, 0xEE6C, 0x424D, { 0xA7, 0x62, 0x8E, 0xD3, 0xB5, 0xA4, 0xA5, 0x01 }
};

//
// 47861DFF-556F-4269-9B28-062EC3ECB30D
// Orphan GUID pushed by the stock DisplayDxe DEPEX.
//
STATIC CONST EFI_GUID mMeizuDisplayDepexProtocol2Guid = {
  0x47861DFF, 0x556F, 0x4269, { 0x9B, 0x28, 0x06, 0x2E, 0xC3, 0xEC, 0xB3, 0x0D }
};

EFI_STATUS
EFIAPI
MeizuDisplayDepexDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle;

  Handle = NULL;
  gBS->InstallMultipleProtocolInterfaces (
         &Handle,
         &mMeizuDisplayDepexProtocol1Guid,
         NULL,
         &mMeizuDisplayDepexProtocol2Guid,
         NULL,
         NULL
         );

  return EFI_SUCCESS;
}
