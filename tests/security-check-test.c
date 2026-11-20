/* Shared security query regression; mocked firmware only, never host disks. */
#include "SecurityCheck.h"
#include <Protocol/BlockIo.h>
#include <Protocol/StorageSecurityCommand.h>
#include <Protocol/DevicePath.h>
#include <IndustryStandard/TcgStorageCore.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EFI_GUID gEfiBlockIoProtocolGuid = { 1, 0, 0, {0} };
EFI_GUID gEfiDevicePathProtocolGuid = { 2, 0, 0, {0} };
EFI_GUID gEfiStorageSecurityCommandProtocolGuid = { 3, 0, 0, {0} };
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
static EFI_BLOCK_IO_MEDIA Media;
static EFI_BLOCK_IO_PROTOCOL BlockIo;
static EFI_STORAGE_SECURITY_COMMAND_PROTOCOL Security;
static unsigned Mode;
static unsigned EfReads;
static unsigned OpalReads;
static unsigned Allocations;

VOID *EFIAPI ZeroMem (VOID *Data, UINTN Size) { return memset (Data, 0, Size); }
VOID *EFIAPI CopyMem (VOID *To, CONST VOID *From, UINTN Size) { return memcpy (To, From, Size); }
UINT16 EFIAPI SwapBytes16 (UINT16 Value) { return (UINT16)((Value << 8) | (Value >> 8)); }
UINTN EFIAPI Print (CONST CHAR16 *Format, ...) { (void)Format; return 0; }
CHAR16 *EFIAPI ConvertDevicePathToText (CONST EFI_DEVICE_PATH_PROTOCOL *Path, BOOLEAN DisplayOnly, BOOLEAN AllowShortcuts) {
  (void)Path; (void)DisplayOnly; (void)AllowShortcuts; return NULL;
}
VOID *EFIAPI AllocateAlignedPages (UINTN Pages, UINTN Alignment) {
  VOID *Memory = aligned_alloc (Alignment, Pages * EFI_PAGE_SIZE);
  assert (Memory != NULL); Allocations++; return Memory;
}
VOID EFIAPI FreeAlignedPages (VOID *Memory, UINTN Pages) { (void)Pages; Allocations--; free (Memory); }
VOID EFIAPI FreePool (VOID *Memory) { free (Memory); }

static EFI_STATUS EFIAPI HandleProtocol (EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface) {
  (void)Handle;
  if (Guid == &gEfiBlockIoProtocolGuid) { *Interface = &BlockIo; return EFI_SUCCESS; }
  if (Guid == &gEfiStorageSecurityCommandProtocolGuid && Mode != 4) { *Interface = &Security; return EFI_SUCCESS; }
  return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI Receive (
  EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *This, UINT32 MediaId, UINT64 Timeout,
  UINT8 Protocol, UINT16 Specific, UINTN Size, VOID *Buffer, UINTN *Transferred
  )
{
  UINT8 *Data = Buffer;
  (void)This;
  assert (MediaId == 42 && Timeout != 0 && ((UINTN)Buffer % EFI_PAGE_SIZE) == 0);
  *Transferred = 0;
  if (Protocol == 0) {
    assert (Size == 512 && Specific == 0);
    if (Mode == 5) { return EFI_DEVICE_ERROR; }
    Data[7] = 2; Data[8] = 0; Data[9] = 0xEF; *Transferred = 512;
    if (Mode == 6) { Data[6] = 0xFF; }
    return EFI_SUCCESS;
  }
  if (Protocol == 0xEF) {
    static CONST UINT8 Frozen[] = {0,14,0,1,0,1,0xFF,0xFE,0,0x29,0,0,0,0,0,0};
    assert (Size == 16 && Specific == 0);
    memcpy (Data, Frozen, 16); *Transferred = 16; EfReads++;
    if (Mode == 1) { Data[9] &= (UINT8)~BIT3; }
    if (Mode == 2) { *Transferred = 15; }
    if (Mode == 3) { Data[1] = 13; }
    return EFI_SUCCESS;
  }
  assert (Protocol == 1 && Specific == SwapBytes16 (TCG_SP_SPECIFIC_PROTOCOL_LEVEL0_DISCOVERY));
  OpalReads++;
  if (Mode != 7) { return EFI_UNSUPPORTED; }
  // A valid minimal Discovery with one Locking feature, no OPAL SSC claim.
  *Transferred = sizeof (TCG_LEVEL0_DISCOVERY_HEADER) + 16;
  Data[3] = (UINT8)(*Transferred - 4);
  Data[sizeof (TCG_LEVEL0_DISCOVERY_HEADER) + 1] = 2;
  Data[sizeof (TCG_LEVEL0_DISCOVERY_HEADER) + 3] = 12;
  Data[sizeof (TCG_LEVEL0_DISCOVERY_HEADER) + 4] = 0x0F;
  return EFI_SUCCESS;
}

int main (void)
{
  SECURITY_CHECK_RESULT Result;
  EFI_STATUS Status;
  Services.HandleProtocol = HandleProtocol;
  Media.MediaId = 42; BlockIo.Media = &Media; Security.ReceiveData = Receive;
  assert (CheckStorageSecurity (NULL, NULL) == EFI_INVALID_PARAMETER);
  for (Mode = 0; Mode <= 7; Mode++) {
    memset (&Result, 0xA5, sizeof (Result)); EfReads = OpalReads = 0;
    Status = CheckStorageSecurity (NULL, &Result);
    assert (Allocations == 0);
    assert (Result.AtaValid == (Mode == 0 || Mode == 1 || Mode == 7));
    if (Result.AtaValid) {
      assert (Result.Ata.Supported && !Result.Ata.Enabled && !Result.Ata.Locked);
      assert (Result.Ata.Frozen == (Mode != 1));
    }
    assert (EfReads == (Mode <= 3 || Mode == 7 ? 1U : 0U));
    assert (OpalReads == (Mode == 4 ? 0U : 1U));
    assert (Result.DiscoveryValid == (Mode == 7));
    if (Mode == 7) {
      assert (Status == EFI_SUCCESS && Result.Discovery.HasLocking && Result.Discovery.LockingFlags == 0x0F);
    } else { assert (EFI_ERROR (Status)); }
  }
  puts ("PASS: shared EF/TCG query, frozen/unfrozen fixtures, EF retained despite OPAL failure.");
  puts ("PASS: malformed/short EF and unavailable/truncated protocol lists remain Unknown; buffers freed.");
  return 0;
}
