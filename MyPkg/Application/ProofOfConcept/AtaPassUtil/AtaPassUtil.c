/** @file
  EFh transport using SAT password packet layouts, never raw ATA opcodes.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "AtaPassUtil.h"
#include <Protocol/BlockIo.h>
#include <IndustryStandard/TcgStorageCore.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define ATA_PASS_TIMEOUT  (5ULL * 1000 * 1000 * 10)
#define EF_SET_PASSWORD  1
#define EF_UNLOCK        2
#define EF_ERASE_PREPARE 3
#define EF_ERASE_UNIT    4
#define EF_FREEZE        5
#define EF_DISABLE       6

VOID
AtaPassWipe (IN OUT VOID *Buffer, IN UINTN Size)
{
  volatile UINT8 *Byte = (volatile UINT8 *)Buffer;
  if (Byte != NULL) {
    while (Size-- > 0) {
      *Byte++ = 0;
    }
  }
}

STATIC
BOOLEAN
ValidDevice (IN CONST ATA_PASS_DEVICE *Device)
{
  return (Device != NULL) && (Device->Signature == ATA_PASS_CONTEXT_SIGNATURE) &&
         (Device->Security != NULL) && (Device->Timeout100ns != 0) &&
         (Device->Alignment >= EFI_PAGE_SIZE) &&
         ((Device->Alignment & (Device->Alignment - 1)) == 0);
}

STATIC
EFI_STATUS
Receive (
  IN OUT ATA_PASS_DEVICE *Device, IN UINT8 Protocol, IN UINTN Size, OUT UINT8 *Output,
  OUT UINTN *OutputSize
  )
{
  EFI_STATUS Status;
  UINT8      *Buffer;
  UINTN      Transferred = 0;

  if (!ValidDevice (Device) || (Output == NULL) || (OutputSize == NULL) || (Size > EFI_PAGE_SIZE)) {
    return EFI_INVALID_PARAMETER;
  }

  *OutputSize = 0;
  ZeroMem (Output, Size);
  Device->ErasePrepared = FALSE;
  Buffer = AllocateAlignedPages (1, Device->Alignment);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Buffer, EFI_PAGE_SIZE);
  Status = Device->Security->ReceiveData (
              Device->Security, Device->MediaId, Device->Timeout100ns,
              Protocol, 0, Size, Buffer, &Transferred
              );
  if ((Status == EFI_SUCCESS) && (Transferred <= Size)) {
    CopyMem (Output, Buffer, Transferred);
    *OutputSize = Transferred;
  } else if (!EFI_ERROR (Status)) {
    Status = EFI_COMPROMISED_DATA;
  }

  AtaPassWipe (Buffer, EFI_PAGE_SIZE);
  FreeAlignedPages (Buffer, 1);
  return Status;
}

EFI_STATUS
AtaPassOpen (IN EFI_HANDLE Handle, OUT ATA_PASS_DEVICE *Device)
{
  EFI_STATUS            Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  UINT8                 Protocols[512];
  UINTN                 Count;
  UINTN                 Index;
  UINTN                 Received;

  if ((Device == NULL) || (Handle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Device, sizeof (*Device));
  Status = gBS->HandleProtocol (Handle, &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((BlockIo->Media == NULL) || BlockIo->Media->LogicalPartition || !BlockIo->Media->MediaPresent) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->HandleProtocol (Handle, &gEfiStorageSecurityCommandProtocolGuid, (VOID **)&Device->Security);
  if (EFI_ERROR (Status)) {
    ZeroMem (Device, sizeof (*Device));
    return Status;
  }

  Device->Signature    = ATA_PASS_CONTEXT_SIGNATURE;
  Device->Handle       = Handle;
  Device->MediaId      = BlockIo->Media->MediaId;
  Device->Alignment    = MAX ((UINTN)BlockIo->Media->IoAlign, (UINTN)EFI_PAGE_SIZE);
  Device->Timeout100ns = ATA_PASS_TIMEOUT;
  Status = Receive (Device, 0, sizeof (Protocols), Protocols, &Received);
  if (!EFI_ERROR (Status)) {
    Count = ((UINTN)Protocols[6] << 8) | Protocols[7];
    if ((Received < 8) || (Count > Received - 8)) {
      Status = EFI_COMPROMISED_DATA;
    } else {
      Status = EFI_UNSUPPORTED;
      for (Index = 0; Index < Count; Index++) {
        if (Protocols[8 + Index] == TCG_SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASS) {
          Status = EFI_SUCCESS;
          break;
        }
      }
    }
  }

  if (EFI_ERROR (Status)) {
    AtaPassClose (Device);
  }

  return Status;
}

VOID
AtaPassClose (IN OUT ATA_PASS_DEVICE *Device)
{
  if (Device != NULL) {
    AtaPassWipe (Device, sizeof (*Device));
  }
}

EFI_STATUS
AtaPassEnableWrites (IN OUT ATA_PASS_DEVICE *Device, IN BOOLEAN Enable)
{
  if (!ValidDevice (Device)) {
    return EFI_INVALID_PARAMETER;
  }

  Device->ErasePrepared = FALSE;
  Device->WritesEnabled = Enable;
  return EFI_SUCCESS;
}

EFI_STATUS
AtaPassGetStatus (IN OUT ATA_PASS_DEVICE *Device, OUT ATA_PASSWORD_STATUS *Status)
{
  EFI_STATUS Result;
  UINT8      Buffer[16];
  UINTN      Received;

  if (Status == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Status, sizeof (*Status));
  Result = Receive (Device, TCG_SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASS, sizeof (Buffer), Buffer, &Received);
  if (!EFI_ERROR (Result) && ((Received != sizeof (Buffer)) ||
      !ParseAtaPasswordStatus (Buffer, Received, Status)))
  {
    Result = EFI_COMPROMISED_DATA;
  }

  return Result;
}

STATIC
EFI_STATUS
Preflight (IN OUT ATA_PASS_DEVICE *Device, OUT ATA_PASSWORD_STATUS *State)
{
  EFI_STATUS Status;
  if (!ValidDevice (Device)) {
    return EFI_INVALID_PARAMETER;
  }

  Device->ErasePrepared = FALSE;
  if (!Device->WritesEnabled) {
    return EFI_WRITE_PROTECTED;
  }

  Status = AtaPassGetStatus (Device, State);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return State->Supported ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
Send (
  IN OUT ATA_PASS_DEVICE *Device, IN UINT16 Operation, IN ATA_PASS_AUTHORITY Authority,
  IN CONST UINT8 *Password32 OPTIONAL, IN BOOLEAN Flag, IN UINT16 Identifier,
  IN UINT64 Timeout100ns
  )
{
  EFI_STATUS Status;
  UINT8      *Buffer = NULL;
  UINTN      Size = 0;
  UINTN      Index;

  Device->ErasePrepared = FALSE;
  if ((Password32 != NULL) && (Operation != EF_FREEZE) && (Operation != EF_ERASE_PREPARE)) {
    Buffer = AllocateAlignedPages (1, Device->Alignment);
    if (Buffer == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    ZeroMem (Buffer, EFI_PAGE_SIZE);
    // SAT EF password parameters: flag, authority, 32 password bytes, BE ID.
    Buffer[0] = Flag ? 1 : 0;
    Buffer[1] = (UINT8)Authority;
    for (Index = 0; Index < ATA_PASS_PASSWORD_SIZE; Index++) {
      Buffer[2 + Index] = Password32[Index];
    }

    Buffer[34] = (UINT8)(Identifier >> 8);
    Buffer[35] = (UINT8)Identifier;
    Size = 36;
  }

  Status = Device->Security->SendData (
              Device->Security, Device->MediaId, Timeout100ns,
              TCG_SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASS, SwapBytes16 (Operation), Size, Buffer
              );
  if (Buffer != NULL) {
    AtaPassWipe (Buffer, EFI_PAGE_SIZE);
    FreeAlignedPages (Buffer, 1);
  }

  // No retries: a timeout/error does not establish that no state change occurred.
  return Status;
}

STATIC
EFI_STATUS
SetPassword (
  IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_AUTHORITY Authority,
  IN CONST UINT8 *Password32, IN BOOLEAN Maximum, IN UINT16 Identifier
  )
{
  EFI_STATUS          Status;
  ATA_PASSWORD_STATUS State;

  if (Password32 == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Preflight (Device, &State);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (State.Frozen || State.Locked) {
    return EFI_ACCESS_DENIED;
  }

  return Send (Device, EF_SET_PASSWORD, Authority, Password32, Maximum, Identifier, Device->Timeout100ns);
}

EFI_STATUS
AtaPassSetUserPassword (
  IN OUT ATA_PASS_DEVICE *Device, IN CONST UINT8 *Password32, IN ATA_PASS_SECURITY_LEVEL Level
  )
{
  if ((Level != AtaPassHigh) && (Level != AtaPassMaximum)) {
    return EFI_INVALID_PARAMETER;
  }

  return SetPassword (Device, AtaPassUser, Password32, Level == AtaPassMaximum, 0);
}

EFI_STATUS
AtaPassSetMasterPassword (
  IN OUT ATA_PASS_DEVICE *Device, IN CONST UINT8 *Password32, IN UINT16 Identifier
  )
{
  // FFFEh is the factory identifier; FFFFh is reserved, not a new user identifier.
  if (Identifier >= 0xFFFE) {
    return EFI_INVALID_PARAMETER;
  }

  return SetPassword (Device, AtaPassMaster, Password32, FALSE, Identifier);
}

STATIC
EFI_STATUS
PasswordOperation (
  IN OUT ATA_PASS_DEVICE *Device, IN UINT16 Operation, IN ATA_PASS_AUTHORITY Authority,
  IN CONST UINT8 *Password32
  )
{
  EFI_STATUS          Status;
  ATA_PASSWORD_STATUS State;

  if ((Password32 == NULL) || ((Authority != AtaPassUser) && (Authority != AtaPassMaster))) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Preflight (Device, &State);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!State.Enabled) {
    return EFI_NOT_READY;
  }

  if (State.AttemptsExceeded || ((Authority == AtaPassMaster) && State.MaximumSecurity)) {
    return EFI_ACCESS_DENIED;
  }

  if (Operation == EF_UNLOCK) {
    if (!State.Locked) {
      return EFI_ALREADY_STARTED;
    }
  } else if (State.Locked || State.Frozen) {
    return EFI_ACCESS_DENIED;
  }

  return Send (Device, Operation, Authority, Password32, FALSE, 0, Device->Timeout100ns);
}

EFI_STATUS
AtaPassUnlock (IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_AUTHORITY Authority, IN CONST UINT8 *Password32)
{
  return PasswordOperation (Device, EF_UNLOCK, Authority, Password32);
}

EFI_STATUS
AtaPassDisablePassword (IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_AUTHORITY Authority, IN CONST UINT8 *Password32)
{
  return PasswordOperation (Device, EF_DISABLE, Authority, Password32);
}

EFI_STATUS
AtaPassFreeze (IN OUT ATA_PASS_DEVICE *Device)
{
  EFI_STATUS          Status;
  ATA_PASSWORD_STATUS State;

  Status = Preflight (Device, &State);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (State.Locked || State.Frozen) {
    return EFI_ACCESS_DENIED;
  }

  return Send (Device, EF_FREEZE, AtaPassUser, NULL, FALSE, 0, Device->Timeout100ns);
}

EFI_STATUS
AtaPassErasePrepare (IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_ERASE_CONFIRMATION Confirmation)
{
  EFI_STATUS          Status;
  ATA_PASSWORD_STATUS State;

  if (Confirmation != AtaPassConfirmPermanentDataLoss) {
    if (ValidDevice (Device)) {
      Device->ErasePrepared = FALSE;
    }

    return EFI_ACCESS_DENIED;
  }

  Status = Preflight (Device, &State);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (State.Frozen || State.AttemptsExceeded || !State.Enabled) {
    return EFI_ACCESS_DENIED;
  }

  Status = Send (Device, EF_ERASE_PREPARE, AtaPassUser, NULL, FALSE, 0, Device->Timeout100ns);
  Device->ErasePrepared = (Status == EFI_SUCCESS);
  Device->EnhancedEraseAvailable = State.EnhancedEraseSupported;
  return Status;
}

EFI_STATUS
AtaPassEraseUnit (
  IN OUT ATA_PASS_DEVICE *Device, IN ATA_PASS_AUTHORITY Authority,
  IN CONST UINT8 *Password32, IN BOOLEAN Enhanced, IN UINT64 Timeout100ns,
  IN ATA_PASS_ERASE_CONFIRMATION Confirmation
  )
{
  BOOLEAN Prepared;
  if (!ValidDevice (Device)) {
    return EFI_INVALID_PARAMETER;
  }

  Prepared = Device->ErasePrepared;
  Device->ErasePrepared = FALSE;
  if (!Device->WritesEnabled || (Confirmation != AtaPassConfirmPermanentDataLoss)) {
    return EFI_ACCESS_DENIED;
  }

  if ((Password32 == NULL) || (Timeout100ns == 0) ||
      ((Authority != AtaPassUser) && (Authority != AtaPassMaster)))
  {
    return EFI_INVALID_PARAMETER;
  }

  if (!Prepared) {
    return EFI_NOT_READY;
  }

  if (Enhanced && !Device->EnhancedEraseAvailable) {
    return EFI_UNSUPPORTED;
  }

  return Send (Device, EF_ERASE_UNIT, Authority, Password32, Enhanced, 0, Timeout100ns);
}
