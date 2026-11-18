/* All writes in this test go to MockSend, never a disk or a real UEFI service. */
#include "AtaPassUtil.h"
#include <Protocol/BlockIo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

EFI_GUID gEfiBlockIoProtocolGuid = { 1, 0, 0, { 0 } };
EFI_GUID gEfiStorageSecurityCommandProtocolGuid = { 2, 0, 0, { 0 } };
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
static EFI_BLOCK_IO_MEDIA Media;
static EFI_BLOCK_IO_PROTOCOL BlockIo;
static EFI_STORAGE_SECURITY_COMMAND_PROTOCOL Security;
static UINT8 State[16];
static UINT8 LastData[36];
static UINT16 LastOperation;
static UINTN LastSize;
static UINT64 LastTimeout;
static unsigned Sends;
static EFI_STATUS SendResult;
static EFI_STATUS ReceiveResult;
static BOOLEAN HasEf;
static BOOLEAN ShortStatus;

VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Size) { return memset (Buffer, 0, Size); }
VOID *EFIAPI CopyMem (VOID *To, CONST VOID *From, UINTN Size) { return memcpy (To, From, Size); }
UINT16 EFIAPI SwapBytes16 (UINT16 Value) { return (UINT16)((Value >> 8) | (Value << 8)); }

VOID *EFIAPI
AllocateAlignedPages (UINTN Pages, UINTN Alignment)
{
  UINTN Bytes = (Pages * EFI_PAGE_SIZE + Alignment - 1) & ~(Alignment - 1);
  VOID *Buffer = aligned_alloc (Alignment, Bytes);
  assert (Buffer != NULL);
  memset (Buffer, 0xA5, Bytes);
  return Buffer;
}

VOID EFIAPI
FreeAlignedPages (VOID *Buffer, UINTN Pages)
{
  UINTN Index;
  /* Every transport buffer, including password packets, must be wiped. */
  for (Index = 0; Index < Pages * EFI_PAGE_SIZE; Index++) {
    assert (((UINT8 *)Buffer)[Index] == 0);
  }
  free (Buffer);
}

static EFI_STATUS EFIAPI
MockHandleProtocol (EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface)
{
  assert (Handle == (EFI_HANDLE)1);
  if (Guid == &gEfiBlockIoProtocolGuid) {
    *Interface = &BlockIo;
  } else if (Guid == &gEfiStorageSecurityCommandProtocolGuid) {
    *Interface = &Security;
  } else {
    return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
MockReceive (
  EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *This, UINT32 MediaId, UINT64 Timeout,
  UINT8 Protocol, UINT16 Specific, UINTN Size, VOID *Buffer, UINTN *Transferred
  )
{
  assert (This == &Security && MediaId == 17 && Timeout != 0 && Specific == 0);
  assert (((UINTN)Buffer & (EFI_PAGE_SIZE - 1)) == 0);
  if (ReceiveResult != EFI_SUCCESS) {
    *Transferred = 0;
    return ReceiveResult;
  }
  if (Protocol == 0) {
    assert (Size == 512);
    ((UINT8 *)Buffer)[7] = 2;
    ((UINT8 *)Buffer)[8] = 0;
    ((UINT8 *)Buffer)[9] = HasEf ? 0xEF : 1;
    *Transferred = 10; /* A valid short actual transfer, not a padded 512. */
  } else {
    assert (Protocol == 0xEF && Size == 16);
    memcpy (Buffer, State, 16);
    *Transferred = ShortStatus ? 15 : 16;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
MockSend (
  EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *This, UINT32 MediaId, UINT64 Timeout,
  UINT8 Protocol, UINT16 Specific, UINTN Size, VOID *Buffer
  )
{
  assert (This == &Security && MediaId == 17 && Protocol == 0xEF);
  Sends++;
  LastOperation = Specific;
  LastSize = Size;
  LastTimeout = Timeout;
  memset (LastData, 0, sizeof (LastData));
  if (Size != 0) {
    assert (Size == 36 && Buffer != NULL);
    assert (((UINTN)Buffer & (EFI_PAGE_SIZE - 1)) == 0);
    memcpy (LastData, Buffer, Size);
  } else {
    assert (Buffer == NULL);
  }
  return SendResult;
}

int main (void)
{
  ATA_PASS_DEVICE Device;
  ATA_PASSWORD_STATUS Info;
  UINT8 Password[32];
  unsigned Before;
  UINTN Index;
  const UINT64 LongTimeout = 3600ULL * 10000000;

  Services.HandleProtocol = MockHandleProtocol;
  Security.ReceiveData = MockReceive;
  Security.SendData = MockSend;
  Media.MediaId = 17;
  Media.MediaPresent = TRUE;
  BlockIo.Media = &Media;
  HasEf = TRUE;
  State[1] = 14;
  State[9] = 1;
  for (Index = 0; Index < 32; Index++) { Password[Index] = (UINT8)(Index + 1); }

  assert (AtaPassOpen ((EFI_HANDLE)1, &Device) == EFI_SUCCESS);
  assert (!Device.WritesEnabled && Sends == 0);
  assert (AtaPassGetStatus (&Device, &Info) == EFI_SUCCESS && Info.Supported);
  assert (AtaPassSetUserPassword (&Device, Password, AtaPassHigh) == EFI_WRITE_PROTECTED);
  assert (AtaPassFreeze (&Device) == EFI_WRITE_PROTECTED && Sends == 0);
  assert (AtaPassEnableWrites (&Device, TRUE) == EFI_SUCCESS);

  assert (AtaPassSetUserPassword (&Device, Password, AtaPassMaximum) == EFI_SUCCESS);
  assert (LastOperation == 0x0100 && LastSize == 36);
  assert (LastData[0] == 1 && LastData[1] == 0 && LastData[34] == 0 && LastData[35] == 0);
  assert (memcmp (LastData + 2, Password, 32) == 0);
  assert (AtaPassSetMasterPassword (&Device, Password, 0x1234) == EFI_SUCCESS);
  assert (LastData[0] == 0 && LastData[1] == 1 && LastData[34] == 0x12 && LastData[35] == 0x34);

  Before = Sends;
  State[9] = 1 | 8; /* frozen */
  assert (AtaPassSetUserPassword (&Device, Password, AtaPassHigh) == EFI_ACCESS_DENIED);
  assert (Sends == Before);
  State[9] = 1 | 2 | 4; /* enabled + locked */
  assert (AtaPassUnlock (&Device, AtaPassUser, Password) == EFI_SUCCESS && LastOperation == 0x0200);
  State[8] = 1;
  Before = Sends;
  assert (AtaPassUnlock (&Device, AtaPassMaster, Password) == EFI_ACCESS_DENIED && Sends == Before);
  State[8] = 0;
  State[9] |= 16;
  assert (AtaPassUnlock (&Device, AtaPassUser, Password) == EFI_ACCESS_DENIED && Sends == Before);

  State[9] = 1 | 2;
  assert (AtaPassDisablePassword (&Device, AtaPassUser, Password) == EFI_SUCCESS);
  assert (LastOperation == 0x0600);
  assert (AtaPassFreeze (&Device) == EFI_SUCCESS && LastOperation == 0x0500 && LastSize == 0);

  Before = Sends;
  assert (AtaPassErasePrepare (&Device, AtaPassDoNotErase) == EFI_ACCESS_DENIED && Sends == Before);
  assert (AtaPassEraseUnit (&Device, AtaPassUser, Password, FALSE, LongTimeout,
    AtaPassConfirmPermanentDataLoss) == EFI_NOT_READY && Sends == Before);
  assert (AtaPassErasePrepare (&Device, AtaPassConfirmPermanentDataLoss) == EFI_SUCCESS);
  assert (LastOperation == 0x0300 && LastSize == 0 && Device.ErasePrepared);
  assert (AtaPassGetStatus (&Device, &Info) == EFI_SUCCESS && !Device.ErasePrepared);
  Before = Sends;
  assert (AtaPassEraseUnit (&Device, AtaPassUser, Password, FALSE, LongTimeout,
    AtaPassConfirmPermanentDataLoss) == EFI_NOT_READY && Sends == Before);

  assert (AtaPassErasePrepare (&Device, AtaPassConfirmPermanentDataLoss) == EFI_SUCCESS);
  Before = Sends;
  assert (AtaPassEraseUnit (&Device, AtaPassUser, Password, TRUE, LongTimeout,
    AtaPassConfirmPermanentDataLoss) == EFI_UNSUPPORTED && Sends == Before);
  State[9] |= 32;
  assert (AtaPassErasePrepare (&Device, AtaPassConfirmPermanentDataLoss) == EFI_SUCCESS);
  assert (AtaPassEraseUnit (&Device, AtaPassMaster, Password, TRUE, LongTimeout,
    AtaPassConfirmPermanentDataLoss) == EFI_SUCCESS);
  assert (LastOperation == 0x0400 && LastData[0] == 1 && LastData[1] == 1);
  assert (LastTimeout == LongTimeout && !Device.ErasePrepared);

  SendResult = EFI_TIMEOUT;
  Before = Sends;
  assert (AtaPassDisablePassword (&Device, AtaPassUser, Password) == EFI_TIMEOUT && Sends == Before + 1);
  SendResult = EFI_SUCCESS;
  ShortStatus = TRUE;
  Before = Sends;
  assert (AtaPassDisablePassword (&Device, AtaPassUser, Password) == EFI_COMPROMISED_DATA && Sends == Before);
  ShortStatus = FALSE;
  ReceiveResult = EFI_DEVICE_ERROR;
  assert (AtaPassDisablePassword (&Device, AtaPassUser, Password) == EFI_DEVICE_ERROR && Sends == Before);
  ReceiveResult = EFI_SUCCESS;
  assert (AtaPassEnableWrites (&Device, FALSE) == EFI_SUCCESS);
  assert (AtaPassDisablePassword (&Device, AtaPassUser, Password) == EFI_WRITE_PROTECTED && Sends == Before);

  AtaPassClose (&Device);
  assert (Device.Signature == 0 && !Device.WritesEnabled);
  HasEf = FALSE;
  assert (AtaPassOpen ((EFI_HANDLE)1, &Device) == EFI_UNSUPPORTED && Device.Signature == 0);
  HasEf = TRUE;
  Media.LogicalPartition = TRUE;
  assert (AtaPassOpen ((EFI_HANDLE)1, &Device) == EFI_UNSUPPORTED);
  AtaPassWipe (Password, sizeof (Password));
  for (Index = 0; Index < 32; Index++) { assert (Password[Index] == 0); }

  puts ("PASS: mocked EF API operations, wire packets, write/erase guards, errors, and buffer wiping.");
  puts ("No physical device was accessed; no real password or erase operation was executed.");
  return 0;
}
