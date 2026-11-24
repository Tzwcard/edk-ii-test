/** @file
  TCG Core 2.01 + Opal 2.30 explicit operations; no authority guessing/retries.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "OpalOps.h"
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

VOID
OpalTestWipe (IN OUT VOID *Data, IN UINTN Size)
{
  volatile UINT8 *Byte = Data;
  if (Byte != NULL) { while (Size-- != 0) { *Byte++ = 0; } }
}

STATIC EFI_STATUS
ResultStatus (IN CONST CHAR16 *Name, IN TCG_RESULT Result, IN UINT8 MethodStatus)
{
  Print (L"%s: TCG_RESULT=%u, MethodStatus=0x%02x\r\n", Name, (UINT32)Result, (UINT32)MethodStatus);
  return Result == TcgResultSuccess && MethodStatus == TCG_METHOD_STATUS_CODE_SUCCESS ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS
Start (IN OUT OPAL_TEST_DEVICE *Device, IN TCG_UID Authority, IN CONST UINT8 *Pin, IN UINT32 Size, IN BOOLEAN RequireWrites)
{
  UINT8 MethodStatus = 0xFF;
  TCG_RESULT Result;
  if (Device == NULL || Pin == NULL || Size == 0 || Size > OPAL_TEST_PIN_MAX) { return EFI_INVALID_PARAMETER; }
  if (Device->Session.Sscp == NULL || Device->Session.Sscp->SendData == NULL ||
      Device->Session.Sscp->ReceiveData == NULL || Device->Session.OpalBaseComId == 0) { return EFI_INVALID_PARAMETER; }
  if (RequireWrites && !Device->AllowWrites) { return EFI_WRITE_PROTECTED; }
  // Opal 2.30 section 4.1.1.2: Write=TRUE is mandatory; FALSE is optional.
  // Session capability is not operation permission: setters still require
  // AllowWrites, while verify/Get never issue a modifying method.
  Result = OpalStartSession (&Device->Session,
    Authority == OPAL_ADMIN_SP_SID_AUTHORITY ? OPAL_UID_ADMIN_SP : OPAL_UID_LOCKING_SP,
    TRUE, Size, Pin, Authority, &MethodStatus);
  return ResultStatus (L"StartSession (one attempt)", Result, MethodStatus);
}

STATIC EFI_STATUS
End (IN OUT OPAL_TEST_DEVICE *Device, IN EFI_STATUS Status)
{
  TCG_RESULT Result = OpalEndSession (&Device->Session);
  if (Result != TcgResultSuccess) {
    Print (L"EndSession failed; preceding change may already have taken effect.\r\n");
    if (!EFI_ERROR (Status)) { return EFI_DEVICE_ERROR; }
  }
  return Status;
}

// Small method exchange for public Core builders (Authority.Enabled / ACE / Get).
// The CLI supplies an aligned, bounded SSCP adapter. Poll Receive only; NEVER
// replay Send or retry authorization on transport/method failure.
STATIC TCG_RESULT
Exchange (IN OPAL_SESSION *Session, IN OUT UINT8 *Buffer, IN UINT32 SendSize, OUT TCG_PARSE_STRUCT *Parse, IN BOOLEAN Quiet)
{
  EFI_STATUS Status;
  UINTN Transferred;
  UINTN Attempt;
  TCG_COM_PACKET *Header = (TCG_COM_PACKET *)Buffer;
  UINT32 Length;
  UINT8 MethodStatus = 0xFF;
  if (SendSize > 512) { return TcgResultFailureBufferTooSmall; }
  ZeroMem (Buffer + SendSize, 512 - SendSize);
  Status = Session->Sscp->SendData (Session->Sscp, Session->MediaId, 50000000,
    TCG_OPAL_SECURITY_PROTOCOL_1, SwapBytes16 (Session->OpalBaseComId), 512, Buffer);
  if (Status != EFI_SUCCESS) { return TcgResultFailure; }
  for (Attempt = 0; Attempt < 500; Attempt++) {
    ZeroMem (Buffer, 512);
    Transferred = 0;
    Status = Session->Sscp->ReceiveData (Session->Sscp, Session->MediaId, 50000000,
      TCG_OPAL_SECURITY_PROTOCOL_1, SwapBytes16 (Session->OpalBaseComId), 512, Buffer, &Transferred);
    if (Status != EFI_SUCCESS || Transferred < sizeof (*Header) || Transferred > 512) { return TcgResultFailure; }
    Length = SwapBytes32 (Header->LengthBE);
    if (Length > Transferred - sizeof (*Header)) { return TcgResultFailureBufferTooSmall; }
    if (Length != 0) {
      if (SwapBytes32 (Header->OutstandingDataBE) != 0) { return TcgResultFailureBufferTooSmall; }
      ERROR_CHECK (TcgInitTcgParseStruct (Parse, Buffer, (UINT32)Transferred));
      ERROR_CHECK (TcgCheckComIds (Parse, Session->OpalBaseComId, Session->ComIdExtension));
      if (SwapBytes32 (Parse->CurPacket->TperSessionNumberBE) != Session->TperSessionId ||
          SwapBytes32 (Parse->CurPacket->HostSessionNumberBE) != Session->HostSessionId) { return TcgResultFailure; }
      ERROR_CHECK (TcgGetMethodStatus (Parse, &MethodStatus));
      if (!Quiet || MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS) { Print (L"MethodStatus=0x%02x\r\n", (UINT32)MethodStatus); }
      return MethodStatus == TCG_METHOD_STATUS_CODE_SUCCESS ? TcgResultSuccess : TcgResultFailure;
    }
    if (SwapBytes32 (Header->OutstandingDataBE) == 0) { return TcgResultFailure; }
    if (EFI_ERROR (gBS->Stall (10000))) { return TcgResultFailure; }
  }
  return TcgResultFailure;
}

STATIC TCG_RESULT
UserEnabled (IN OPAL_SESSION *Session, IN BOOLEAN Enabled)
{
  UINT8 Buffer[512];
  UINT32 Size;
  TCG_CREATE_STRUCT Create;
  TCG_PARSE_STRUCT Parse;
  ERROR_CHECK (TcgInitTcgCreateStruct (&Create, Buffer, sizeof (Buffer)));
  ERROR_CHECK (TcgSetAuthorityEnabled (&Create, &Size, Session->OpalBaseComId, Session->ComIdExtension,
    Session->TperSessionId, Session->HostSessionId, OPAL_LOCKING_SP_USER1_AUTHORITY, Enabled));
  return Exchange (Session, Buffer, Size, &Parse, FALSE);
}

STATIC TCG_RESULT
UserAce (IN OPAL_SESSION *Session, IN TCG_UID Ace)
{
  UINT8 Buffer[512];
  UINT32 Size;
  TCG_CREATE_STRUCT Create;
  TCG_PARSE_STRUCT Parse;
  ERROR_CHECK (TcgInitTcgCreateStruct (&Create, Buffer, sizeof (Buffer)));
  ERROR_CHECK (TcgCreateSetAce (&Create, &Size, Session->OpalBaseComId, Session->ComIdExtension,
    Session->TperSessionId, Session->HostSessionId, Ace, OPAL_LOCKING_SP_USER1_AUTHORITY,
    TCG_ACE_EXPRESSION_OR, OPAL_LOCKING_SP_ADMINS_AUTHORITY));
  return Exchange (Session, Buffer, Size, &Parse, FALSE);
}

STATIC TCG_RESULT
GetGlobal (IN OPAL_SESSION *Session, OUT OPAL_TEST_RANGE *Range)
{
  UINT8 Buffer[512];
  UINT32 Size;
  UINT32 Column;
  UINT32 Index;
  BOOLEAN Values[4];
  TCG_CREATE_STRUCT Create;
  TCG_PARSE_STRUCT Parse;
  ERROR_CHECK (TcgInitTcgCreateStruct (&Create, Buffer, sizeof (Buffer)));
  ERROR_CHECK (TcgStartComPacket (&Create, Session->OpalBaseComId, Session->ComIdExtension));
  ERROR_CHECK (TcgStartPacket (&Create, Session->TperSessionId, Session->HostSessionId, 0, 0, 0));
  ERROR_CHECK (TcgStartSubPacket (&Create, 0));
  ERROR_CHECK (TcgStartMethodCall (&Create, OPAL_LOCKING_SP_LOCKING_GLOBALRANGE, TCG_UID_METHOD_GET));
  ERROR_CHECK (TcgStartParameters (&Create));
  ERROR_CHECK (TcgAddStartList (&Create));
  ERROR_CHECK (TcgAddStartName (&Create));
  ERROR_CHECK (TcgAddUINT8 (&Create, TCG_CELL_BLOCK_START_COLUMN_NAME));
  ERROR_CHECK (TcgAddUINT8 (&Create, 5)); // ReadLockEnabled
  ERROR_CHECK (TcgAddEndName (&Create));
  ERROR_CHECK (TcgAddStartName (&Create));
  ERROR_CHECK (TcgAddUINT8 (&Create, TCG_CELL_BLOCK_END_COLUMN_NAME));
  ERROR_CHECK (TcgAddUINT8 (&Create, 8)); // WriteLocked
  ERROR_CHECK (TcgAddEndName (&Create));
  ERROR_CHECK (TcgAddEndList (&Create));
  ERROR_CHECK (TcgEndParameters (&Create));
  ERROR_CHECK (TcgEndMethodCall (&Create));
  ERROR_CHECK (TcgEndSubPacket (&Create));
  ERROR_CHECK (TcgEndPacket (&Create));
  ERROR_CHECK (TcgEndComPacket (&Create, &Size));
  ERROR_CHECK (Exchange (Session, Buffer, Size, &Parse, FALSE));
  ERROR_CHECK (TcgGetNextStartList (&Parse));
  ERROR_CHECK (TcgGetNextStartList (&Parse));
  for (Index = 0; Index < 4; Index++) {
    ERROR_CHECK (TcgGetNextStartName (&Parse));
    ERROR_CHECK (TcgGetNextUINT32 (&Parse, &Column));
    if (Column != Index + 5) { return TcgResultFailure; }
    ERROR_CHECK (TcgGetNextBOOLEAN (&Parse, &Values[Index]));
    ERROR_CHECK (TcgGetNextEndName (&Parse));
  }
  ERROR_CHECK (TcgGetNextEndList (&Parse));
  ERROR_CHECK (TcgGetNextEndList (&Parse));
  ERROR_CHECK (TcgGetNextEndOfData (&Parse));
  Range->ReadLockEnabled = Values[0]; Range->WriteLockEnabled = Values[1];
  Range->ReadLocked = Values[2]; Range->WriteLocked = Values[3];
  return TcgResultSuccess;
}

EFI_STATUS
OpalTestVerify (IN OUT OPAL_TEST_DEVICE *Device, IN TCG_UID Authority, IN CONST UINT8 *Pin, IN UINT32 PinSize)
{
  EFI_STATUS Status;
  if (Authority != OPAL_ADMIN_SP_SID_AUTHORITY && Authority != OPAL_LOCKING_SP_ADMIN1_AUTHORITY &&
      Authority != OPAL_LOCKING_SP_USER1_AUTHORITY) { return EFI_INVALID_PARAMETER; }
  Status = Start (Device, Authority, Pin, PinSize, FALSE);
  return EFI_ERROR (Status) ? Status : End (Device, Status);
}

EFI_STATUS
OpalTestSetPin (IN OUT OPAL_TEST_DEVICE *Device, IN TCG_UID Authority,
  IN CONST UINT8 *OldPin, IN UINT32 OldSize, IN CONST UINT8 *NewPin, IN UINT32 NewSize)
{
  EFI_STATUS Status;
  TCG_RESULT Result;
  TCG_UID Row;
  UINT8 MethodStatus = 0xFF;
  if (NewPin == NULL || NewSize == 0 || NewSize > OPAL_TEST_PIN_MAX) { return EFI_INVALID_PARAMETER; }
  if (Authority == OPAL_ADMIN_SP_SID_AUTHORITY) { Row = OPAL_UID_ADMIN_SP_C_PIN_SID; }
  else if (Authority == OPAL_LOCKING_SP_ADMIN1_AUTHORITY) { Row = OPAL_LOCKING_SP_C_PIN_ADMIN1; }
  else if (Authority == OPAL_LOCKING_SP_USER1_AUTHORITY) { Row = OPAL_LOCKING_SP_C_PIN_USER1; }
  else { return EFI_INVALID_PARAMETER; }
  Status = Start (Device, Authority, OldPin, OldSize, TRUE);
  if (EFI_ERROR (Status)) { return Status; }
  Result = OpalSetPassword (&Device->Session, Row, NewPin, NewSize, &MethodStatus);
  Status = End (Device, ResultStatus (L"Set PIN", Result, MethodStatus));
  if (!EFI_ERROR (Status)) { Status = OpalTestVerify (Device, Authority, NewPin, NewSize); }
  return Status;
}

EFI_STATUS
OpalTestActivate (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Sid, IN UINT32 Size)
{
  EFI_STATUS Status = Start (Device, OPAL_ADMIN_SP_SID_AUTHORITY, Sid, Size, TRUE);
  TCG_RESULT Result;
  UINT8 MethodStatus = 0xFF;
  if (EFI_ERROR (Status)) { return Status; }
  Result = OpalActivateLockingSp (&Device->Session, &MethodStatus);
  return End (Device, ResultStatus (L"Activate Locking SP", Result, MethodStatus));
}

EFI_STATUS
OpalTestSetUser1 (IN OUT OPAL_TEST_DEVICE *Device,
  IN CONST UINT8 *Admin, IN UINT32 AdminSize, IN CONST UINT8 *NewPin, IN UINT32 NewSize)
{
  EFI_STATUS Status;
  TCG_RESULT Result;
  UINT8 MethodStatus = 0xFF;
  if (NewPin == NULL || NewSize == 0 || NewSize > OPAL_TEST_PIN_MAX) { return EFI_INVALID_PARAMETER; }
  Status = Start (Device, OPAL_LOCKING_SP_ADMIN1_AUTHORITY, Admin, AdminSize, TRUE);
  if (EFI_ERROR (Status)) { return Status; }
  // Set PIN before enabling. Do NOT use the EDK convenience setter: it also
  // grants User1 GenKey rights. Here only the two lock/unlock ACEs are changed.
  Result = OpalSetPassword (&Device->Session, OPAL_LOCKING_SP_C_PIN_USER1, NewPin, NewSize, &MethodStatus);
  Status = ResultStatus (L"Set User1 PIN", Result, MethodStatus);
  if (!EFI_ERROR (Status)) {
    Result = UserAce (&Device->Session, OPAL_LOCKING_SP_ACE_LOCKING_GLOBALRANGE_SET_RDLOCKED);
    if (Result == TcgResultSuccess) { Result = UserAce (&Device->Session, OPAL_LOCKING_SP_ACE_LOCKING_GLOBALRANGE_SET_WRLOCKED); }
    if (Result == TcgResultSuccess) { Result = UserEnabled (&Device->Session, TRUE); }
    if (Result != TcgResultSuccess) { Status = EFI_DEVICE_ERROR; }
  }
  Status = End (Device, Status);
  if (!EFI_ERROR (Status)) { Status = OpalTestVerify (Device, OPAL_LOCKING_SP_USER1_AUTHORITY, NewPin, NewSize); }
  return Status;
}

EFI_STATUS
OpalTestDisableUser1 (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size)
{
  EFI_STATUS Status = Start (Device, OPAL_LOCKING_SP_ADMIN1_AUTHORITY, Admin, Size, TRUE);
  TCG_RESULT Result;
  UINT8 MethodStatus = 0xFF;
  if (EFI_ERROR (Status)) { return Status; }
  Result = OpalDisableUser (&Device->Session, &MethodStatus);
  return End (Device, ResultStatus (L"Disable User1 (PIN not erased)", Result, MethodStatus));
}

EFI_STATUS
OpalTestGetGlobal (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size, OUT OPAL_TEST_RANGE *Range)
{
  EFI_STATUS Status;
  if (Range == NULL) { return EFI_INVALID_PARAMETER; }
  ZeroMem (Range, sizeof (*Range));
  Status = Start (Device, OPAL_LOCKING_SP_ADMIN1_AUTHORITY, Admin, Size, FALSE);
  if (EFI_ERROR (Status)) { return Status; }
  Status = GetGlobal (&Device->Session, Range) == TcgResultSuccess ? EFI_SUCCESS : EFI_DEVICE_ERROR;
  return End (Device, Status);
}

EFI_STATUS
OpalTestSetGlobal (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size,
  IN BOOLEAN Enabled, IN BOOLEAN Locked)
{
  OPAL_TEST_RANGE Range;
  EFI_STATUS Status;
  TCG_RESULT Result;
  UINT8 MethodStatus = 0xFF;
  if (Locked && !Enabled) { return EFI_INVALID_PARAMETER; }
  Status = Start (Device, OPAL_LOCKING_SP_ADMIN1_AUTHORITY, Admin, Size, TRUE);
  if (EFI_ERROR (Status)) { return Status; }
  Result = OpalSetLockingRange (&Device->Session, OPAL_LOCKING_SP_LOCKING_GLOBALRANGE, 0, 0,
    Enabled, Enabled, Locked, Locked, &MethodStatus);
  Status = ResultStatus (L"Set Global Range", Result, MethodStatus);
  if (!EFI_ERROR (Status)) {
    Status = GetGlobal (&Device->Session, &Range) == TcgResultSuccess ? EFI_SUCCESS : EFI_DEVICE_ERROR;
    if (!EFI_ERROR (Status) && (Range.ReadLockEnabled != Enabled || Range.WriteLockEnabled != Enabled ||
        Range.ReadLocked != Locked || Range.WriteLocked != Locked)) { Status = EFI_DEVICE_ERROR; }
  }
  return End (Device, Status);
}

EFI_STATUS
OpalTestUserGlobal (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Pin, IN UINT32 Size, IN BOOLEAN Locked)
{
  EFI_STATUS Status = Start (Device, OPAL_LOCKING_SP_USER1_AUTHORITY, Pin, Size, TRUE);
  TCG_RESULT Result;
  UINT8 MethodStatus = 0xFF;
  if (EFI_ERROR (Status)) { return Status; }
  Result = OpalUpdateGlobalLockingRange (&Device->Session, Locked, Locked, &MethodStatus);
  return End (Device, ResultStatus (L"User1 Global Range lock bits", Result, MethodStatus));
}

STATIC TCG_RESULT
SetMbrDone (IN OPAL_SESSION *Session)
{
  UINT8 Buffer[512];
  UINT32 SendSize;
  TCG_CREATE_STRUCT Create;
  TCG_PARSE_STRUCT Parse;
  // TCG Core MBRControl: UID 0000080300000001, column 2 = Done.
  ERROR_CHECK (TcgInitTcgCreateStruct (&Create, Buffer, sizeof (Buffer)));
  ERROR_CHECK (TcgStartComPacket (&Create, Session->OpalBaseComId, Session->ComIdExtension));
  ERROR_CHECK (TcgStartPacket (&Create, Session->TperSessionId, Session->HostSessionId, 0, 0, 0));
  ERROR_CHECK (TcgStartSubPacket (&Create, 0));
  ERROR_CHECK (TcgStartMethodCall (&Create, TCG_TO_UID (0, 0, 8, 3, 0, 0, 0, 1), TCG_UID_METHOD_SET));
  ERROR_CHECK (TcgStartParameters (&Create));
  ERROR_CHECK (TcgAddStartName (&Create));
  ERROR_CHECK (TcgAddUINT8 (&Create, 1)); // Values
  ERROR_CHECK (TcgAddStartList (&Create));
  ERROR_CHECK (TcgAddStartName (&Create));
  ERROR_CHECK (TcgAddUINT8 (&Create, 2)); // Done only
  ERROR_CHECK (TcgAddBOOLEAN (&Create, TRUE));
  ERROR_CHECK (TcgAddEndName (&Create));
  ERROR_CHECK (TcgAddEndList (&Create));
  ERROR_CHECK (TcgAddEndName (&Create));
  ERROR_CHECK (TcgEndParameters (&Create));
  ERROR_CHECK (TcgEndMethodCall (&Create));
  ERROR_CHECK (TcgEndSubPacket (&Create));
  ERROR_CHECK (TcgEndPacket (&Create));
  ERROR_CHECK (TcgEndComPacket (&Create, &SendSize));
  return Exchange (Session, Buffer, SendSize, &Parse, FALSE);
}

EFI_STATUS
OpalTestMbrDone (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size)
{
  EFI_STATUS Status = Start (Device, OPAL_LOCKING_SP_ADMIN1_AUTHORITY, Admin, Size, TRUE);
  if (EFI_ERROR (Status)) { return Status; }
  Status = SetMbrDone (&Device->Session) == TcgResultSuccess ? EFI_SUCCESS : EFI_DEVICE_ERROR;
  return End (Device, Status);
}

STATIC TCG_RESULT
SetMbrBytes (IN OPAL_SESSION *Session, IN UINT32 Offset, IN CONST UINT8 *Data, IN UINT32 DataSize)
{
  UINT8 Buffer[512];
  UINT32 SendSize;
  TCG_CREATE_STRUCT Create;
  TCG_PARSE_STRUCT Parse;
  ERROR_CHECK (TcgInitTcgCreateStruct (&Create, Buffer, sizeof (Buffer)));
  ERROR_CHECK (TcgStartComPacket (&Create, Session->OpalBaseComId, Session->ComIdExtension));
  ERROR_CHECK (TcgStartPacket (&Create, Session->TperSessionId, Session->HostSessionId, 0, 0, 0));
  ERROR_CHECK (TcgStartSubPacket (&Create, 0));
  // Byte-table UID, NOT MBRControl and NOT a namespace Write command.
  ERROR_CHECK (TcgStartMethodCall (&Create, TCG_TO_UID (0, 0, 8, 4, 0, 0, 0, 0), TCG_UID_METHOD_SET));
  ERROR_CHECK (TcgStartParameters (&Create));
  ERROR_CHECK (TcgAddStartName (&Create));
  ERROR_CHECK (TcgAddUINT8 (&Create, 0)); // Where: byte offset
  ERROR_CHECK (TcgAddUINT32 (&Create, Offset));
  ERROR_CHECK (TcgAddEndName (&Create));
  ERROR_CHECK (TcgAddStartName (&Create));
  ERROR_CHECK (TcgAddUINT8 (&Create, 1)); // Values: byte sequence, no object-column list
  ERROR_CHECK (TcgAddByteSequence (&Create, Data, DataSize, FALSE));
  ERROR_CHECK (TcgAddEndName (&Create));
  ERROR_CHECK (TcgEndParameters (&Create));
  ERROR_CHECK (TcgEndMethodCall (&Create));
  ERROR_CHECK (TcgEndSubPacket (&Create));
  ERROR_CHECK (TcgEndPacket (&Create));
  ERROR_CHECK (TcgEndComPacket (&Create, &SendSize));
  return Exchange (Session, Buffer, SendSize, &Parse, TRUE);
}

EFI_STATUS
OpalTestLoadPba (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size,
  IN CONST UINT8 *Image, IN UINT32 ImageSize)
{
  EFI_STATUS Status;
  UINT32 Offset;
  UINT32 Chunk;
  if (Image == NULL || ImageSize == 0 || ImageSize > OPAL_PBA_IMAGE_SIZE) { return EFI_INVALID_PARAMETER; }
  Status = Start (Device, OPAL_LOCKING_SP_ADMIN1_AUTHORITY, Admin, Size, TRUE);
  if (EFI_ERROR (Status)) { return Status; }
  for (Offset = 0; Offset < ImageSize; Offset += Chunk) {
    Chunk = MIN (ImageSize - Offset, OPAL_PBA_CHUNK_SIZE);
    if (SetMbrBytes (&Device->Session, Offset, Image + Offset, Chunk) != TcgResultSuccess) {
      Print (L"PBA upload failed at byte %u; Shadow MBR may be partially replaced. No retry.\r\n", Offset);
      Status = EFI_DEVICE_ERROR;
      break;
    }
    if (Offset / 1048576 != (Offset + Chunk) / 1048576 || Offset + Chunk == ImageSize) {
      Print (L"Shadow MBR upload: %u / %u bytes\r\n", Offset + Chunk, ImageSize);
    }
  }
  if (!EFI_ERROR (Status)) { Print (L"PBA bytes accepted by MBR table. Enable/Done/reset policy NOT changed.\r\n"); }
  return End (Device, Status);
}

EFI_STATUS
OpalTestRevert (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Sid, IN UINT32 Size)
{
  EFI_STATUS Status;
  TCG_RESULT Result;
  if (Device == NULL) { return EFI_INVALID_PARAMETER; }
  if (!Device->AllowWrites || !Device->AllowErase) { return EFI_WRITE_PROTECTED; }
  Status = Start (Device, OPAL_ADMIN_SP_SID_AUTHORITY, Sid, Size, TRUE);
  if (EFI_ERROR (Status)) { return Status; }
  // This EDK routine emits AdminSP.Revert; authentication is already explicitly
  // SID here, NOT a PSID fallback. Successful Revert aborts the session itself.
  Result = OpalPsidRevert (&Device->Session);
  Device->AllowWrites = FALSE; Device->AllowErase = FALSE;
  Print (L"Admin SP Revert: TCG_RESULT=%u. Do not automatically retry or reset/power-cycle.\r\n", (UINT32)Result);
  return Result == TcgResultSuccess ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
