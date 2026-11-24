/* Mock OPAL device only. Real EDK Core token builders/parsers, no hardware I/O. */
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "OpalOps.h"

static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
static EFI_STORAGE_SECURITY_COMMAND_PROTOCOL Sscp;
static OPAL_TEST_DEVICE Device;
static UINT8 Pin[] = "TestOnly-PIN-2026";
static UINT8 NewPin[] = "TestOnly-NewPIN";
static OPAL_TEST_RANGE Global;
static TCG_UID LastAuthority, LastRow, LastMethod;
static unsigned Starts, Ends, Sets, Sends, Reads, Reverts, AceWrites, Enables, Disables;
static unsigned FailStart, FailMethod, FailSend, FailReceive, BadLength, Busy, WrongSession, ShortGet;
static unsigned AceFailAt;
static CONST UINT8 *ExpectedPin, *ExpectedNewPin;
static UINT32 ExpectedSize;
static unsigned MbrWrites;
static BOOLEAN MbrDone;
static CONST UINT8 *PbaExpected;
static UINT32 PbaSize, PbaOffset;
static unsigned PbaWrites, FailPbaAt;

VOID *EFIAPI ZeroMem (VOID *Data, UINTN Size) { return memset (Data, 0, Size); }
VOID *EFIAPI CopyMem (VOID *To, CONST VOID *From, UINTN Size) { return memcpy (To, From, Size); }
UINT16 EFIAPI SwapBytes16 (UINT16 Value) { return __builtin_bswap16 (Value); }
UINT32 EFIAPI SwapBytes32 (UINT32 Value) { return __builtin_bswap32 (Value); }
UINT64 EFIAPI SwapBytes64 (UINT64 Value) { return __builtin_bswap64 (Value); }
UINT64 EFIAPI LShiftU64 (UINT64 Value, UINTN Count) { return Value << Count; }
UINTN EFIAPI Print (CONST CHAR16 *Format, ...) { (void)Format; return 0; }
static EFI_STATUS EFIAPI Stall (UINTN Time) { assert (Time == 10000); return EFI_SUCCESS; }

TCG_RESULT EFIAPI OpalStartSession (OPAL_SESSION *Session, TCG_UID Sp, BOOLEAN Write,
  UINT32 Size, CONST VOID *Password, TCG_UID Authority, UINT8 *MethodStatus)
{
  assert (Password != NULL && Size > 0 && Size <= OPAL_TEST_PIN_MAX);
  if (ExpectedPin != NULL) { assert (Size == ExpectedSize && memcmp (Password, ExpectedPin, Size) == 0); }
  assert (Sp == (Authority == OPAL_ADMIN_SP_SID_AUTHORITY ? OPAL_UID_ADMIN_SP : OPAL_UID_LOCKING_SP));
  // Model a conforming device that does not support optional read-only sessions.
  assert (Write == TRUE);
  Starts++; LastAuthority = Authority; *MethodStatus = FailStart ? TCG_METHOD_STATUS_CODE_NOT_AUTHORIZED : 0;
  Session->HostSessionId = 5; Session->TperSessionId = 6;
  return TcgResultSuccess;
}
TCG_RESULT EFIAPI OpalEndSession (OPAL_SESSION *Session) { (void)Session; Ends++; return TcgResultSuccess; }
TCG_RESULT EFIAPI OpalSetPassword (OPAL_SESSION *Session, TCG_UID Row, CONST VOID *Password, UINT32 Size, UINT8 *MethodStatus) {
  (void)Session; assert (Password != NULL && Size > 0); LastRow = Row; Sets++;
  if (ExpectedNewPin != NULL) { assert (Size == 32 && memcmp (Password, ExpectedNewPin, Size) == 0); ExpectedPin = ExpectedNewPin; }
  *MethodStatus = FailMethod ? TCG_METHOD_STATUS_CODE_NOT_AUTHORIZED : 0; return TcgResultSuccess;
}
TCG_RESULT EFIAPI OpalActivateLockingSp (OPAL_SESSION *Session, UINT8 *MethodStatus) {
  (void)Session; assert (LastAuthority == OPAL_ADMIN_SP_SID_AUTHORITY);
  Sets++; *MethodStatus = FailMethod ? TCG_METHOD_STATUS_CODE_FAIL : 0; return TcgResultSuccess;
}
TCG_RESULT EFIAPI OpalDisableUser (OPAL_SESSION *Session, UINT8 *MethodStatus) {
  (void)Session; assert (LastAuthority == OPAL_LOCKING_SP_ADMIN1_AUTHORITY);
  Disables++; *MethodStatus = FailMethod ? TCG_METHOD_STATUS_CODE_FAIL : 0; return TcgResultSuccess;
}
TCG_RESULT EFIAPI OpalSetLockingRange (OPAL_SESSION *Session, TCG_UID Row, UINT64 Start, UINT64 Length,
  BOOLEAN ReadEnabled, BOOLEAN WriteEnabled, BOOLEAN ReadLocked, BOOLEAN WriteLocked, UINT8 *MethodStatus)
{
  (void)Session;
  assert (LastAuthority == OPAL_LOCKING_SP_ADMIN1_AUTHORITY && Row == OPAL_LOCKING_SP_LOCKING_GLOBALRANGE);
  assert (Start == 0 && Length == 0);
  Sets++; Global = (OPAL_TEST_RANGE){ReadEnabled, WriteEnabled, ReadLocked, WriteLocked};
  *MethodStatus = FailMethod ? TCG_METHOD_STATUS_CODE_FAIL : 0; return TcgResultSuccess;
}
TCG_RESULT EFIAPI OpalUpdateGlobalLockingRange (OPAL_SESSION *Session, BOOLEAN ReadLocked, BOOLEAN WriteLocked, UINT8 *MethodStatus) {
  (void)Session; assert (LastAuthority == OPAL_LOCKING_SP_USER1_AUTHORITY);
  Sets++; Global.ReadLocked = ReadLocked; Global.WriteLocked = WriteLocked;
  *MethodStatus = 0; return TcgResultSuccess;
}
TCG_RESULT EFIAPI OpalPsidRevert (OPAL_SESSION *Session) {
  (void)Session; assert (LastAuthority == OPAL_ADMIN_SP_SID_AUTHORITY && Device.AllowErase && Device.AllowWrites);
  Reverts++; return TcgResultSuccess;
}

#define TC(CALL) assert ((CALL) == TcgResultSuccess)
static EFI_STATUS EFIAPI Send (EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *This, UINT32 MediaId, UINT64 Timeout,
  UINT8 Protocol, UINT16 Specific, UINTN Size, VOID *Buffer)
{
  TCG_PARSE_STRUCT Parse;
  UINT32 Name;
  BOOLEAN Enabled;
  CONST VOID *Bytes;
  UINT32 Length, Offset;
  (void)This; assert (MediaId == 7 && Timeout != 0 && Protocol == 1 && Specific == SwapBytes16 (0x1001) && Size == 512);
  assert (Device.AllowWrites || LastAuthority == OPAL_LOCKING_SP_ADMIN1_AUTHORITY);
  Sends++;
  if (FailSend) { return EFI_DEVICE_ERROR; }
  TC (TcgInitTcgParseStruct (&Parse, Buffer, (UINT32)Size));
  TC (TcgGetNextCall (&Parse)); TC (TcgGetNextTcgUid (&Parse, &LastRow)); TC (TcgGetNextTcgUid (&Parse, &LastMethod));
  if (LastMethod == TCG_UID_METHOD_SET) {
    if (LastRow == OPAL_LOCKING_SP_USER1_AUTHORITY) {
      TC (TcgGetNextStartList (&Parse)); TC (TcgGetNextStartName (&Parse));
      TC (TcgGetNextUINT32 (&Parse, &Name)); assert (Name == 1);
      TC (TcgGetNextStartList (&Parse)); TC (TcgGetNextStartName (&Parse));
      TC (TcgGetNextUINT32 (&Parse, &Name)); assert (Name == 5);
      TC (TcgGetNextBOOLEAN (&Parse, &Enabled)); assert (Enabled && Sets > 0); Enables++;
    } else if (LastRow == TCG_TO_UID (0, 0, 8, 4, 0, 0, 0, 0)) {
      assert (Device.AllowWrites && LastAuthority == OPAL_LOCKING_SP_ADMIN1_AUTHORITY && PbaExpected != NULL);
      TC (TcgGetNextStartList (&Parse)); TC (TcgGetNextStartName (&Parse));
      TC (TcgGetNextUINT32 (&Parse, &Name)); assert (Name == 0); // Where
      TC (TcgGetNextUINT32 (&Parse, &Offset)); assert (Offset == PbaOffset);
      TC (TcgGetNextEndName (&Parse)); TC (TcgGetNextStartName (&Parse));
      TC (TcgGetNextUINT32 (&Parse, &Name)); assert (Name == 1); // Values, directly a byte string
      TC (TcgGetNextByteSequence (&Parse, &Bytes, &Length));
      assert (Length == MIN (PbaSize - PbaOffset, OPAL_PBA_CHUNK_SIZE));
      assert (memcmp (Bytes, PbaExpected + PbaOffset, Length) == 0);
      TC (TcgGetNextEndName (&Parse)); TC (TcgGetNextEndList (&Parse)); TC (TcgGetNextEndOfData (&Parse));
      PbaWrites++;
      if (!FailMethod && PbaWrites != FailPbaAt) { PbaOffset += Length; }
    } else if (LastRow == TCG_TO_UID (0, 0, 8, 3, 0, 0, 0, 1)) {
      assert (Device.AllowWrites && LastAuthority == OPAL_LOCKING_SP_ADMIN1_AUTHORITY);
      TC (TcgGetNextStartList (&Parse)); TC (TcgGetNextStartName (&Parse));
      TC (TcgGetNextUINT32 (&Parse, &Name)); assert (Name == 1);
      TC (TcgGetNextStartList (&Parse)); TC (TcgGetNextStartName (&Parse));
      TC (TcgGetNextUINT32 (&Parse, &Name)); assert (Name == 2); // Done, not Enable/reset policy
      TC (TcgGetNextBOOLEAN (&Parse, &Enabled)); assert (Enabled);
      TC (TcgGetNextEndName (&Parse)); TC (TcgGetNextEndList (&Parse));
      TC (TcgGetNextEndName (&Parse)); TC (TcgGetNextEndList (&Parse));
      TC (TcgGetNextEndOfData (&Parse)); // No other fields/methods in this command
      MbrWrites++;
      if (!FailMethod) { MbrDone = TRUE; }
    } else {
      // No GenKey, Get_All or other ACE can be granted by User1 setup.
      assert (LastRow == OPAL_LOCKING_SP_ACE_LOCKING_GLOBALRANGE_SET_RDLOCKED ||
              LastRow == OPAL_LOCKING_SP_ACE_LOCKING_GLOBALRANGE_SET_WRLOCKED);
      AceWrites++;
    }
  } else { assert (LastMethod == TCG_UID_METHOD_GET && LastRow == OPAL_LOCKING_SP_LOCKING_GLOBALRANGE); }
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI Receive (EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *This, UINT32 MediaId, UINT64 Timeout,
  UINT8 Protocol, UINT16 Specific, UINTN Capacity, VOID *Buffer, UINTN *Transferred)
{
  TCG_CREATE_STRUCT Create;
  UINT32 Size;
  UINTN Index;
  BOOLEAN Values[] = {Global.ReadLockEnabled, Global.WriteLockEnabled, Global.ReadLocked, Global.WriteLocked};
  UINT8 *Data = Buffer;
  (void)This; assert (MediaId == 7 && Timeout != 0 && Protocol == 1 && Specific == SwapBytes16 (0x1001) && Capacity == 512);
  Reads++;
  if (FailReceive) { return EFI_DEVICE_ERROR; }
  if (Busy) {
    ((TCG_COM_PACKET *)Data)->OutstandingDataBE = SwapBytes32 (1);
    *Transferred = 512; Busy--; return EFI_SUCCESS;
  }
  TC (TcgInitTcgCreateStruct (&Create, Buffer, (UINT32)Capacity));
  TC (TcgStartComPacket (&Create, 0x1001, 0));
  TC (TcgStartPacket (&Create, WrongSession ? 99 : 6, 5, 0, 0, 0));
  TC (TcgStartSubPacket (&Create, 0));
  TC (TcgAddStartList (&Create));
  if (LastMethod == TCG_UID_METHOD_GET) {
    TC (TcgAddStartList (&Create));
    for (Index = 0; Index < (ShortGet ? 3U : 4U); Index++) {
      TC (TcgAddStartName (&Create)); TC (TcgAddUINT32 (&Create, (UINT32)Index + 5));
      TC (TcgAddBOOLEAN (&Create, Values[Index])); TC (TcgAddEndName (&Create));
    }
    TC (TcgAddEndList (&Create));
  }
  TC (TcgAddEndList (&Create)); TC (TcgAddEndOfData (&Create));
  TC (TcgAddStartList (&Create));
  TC (TcgAddUINT8 (&Create, FailMethod || (AceFailAt != 0 && AceWrites == AceFailAt) ||
    (FailPbaAt != 0 && PbaWrites == FailPbaAt) ? TCG_METHOD_STATUS_CODE_FAIL : 0));
  TC (TcgAddUINT8 (&Create, 0)); TC (TcgAddUINT8 (&Create, 0)); TC (TcgAddEndList (&Create));
  TC (TcgEndSubPacket (&Create)); TC (TcgEndPacket (&Create)); TC (TcgEndComPacket (&Create, &Size));
  *Transferred = BadLength ? 513 : Size;
  return EFI_SUCCESS;
}

static void Reset (void) {
  memset (&Device, 0, sizeof (Device)); memset (&Global, 0, sizeof (Global));
  Starts = Ends = Sets = Sends = Reads = Reverts = AceWrites = Enables = Disables = 0;
  FailStart = FailMethod = FailSend = FailReceive = BadLength = Busy = WrongSession = ShortGet = AceFailAt = 0;
  ExpectedPin = ExpectedNewPin = NULL; ExpectedSize = 0;
  MbrWrites = 0; MbrDone = FALSE;
  PbaExpected = NULL; PbaSize = PbaOffset = 0; PbaWrites = FailPbaAt = 0;
  Sscp.SendData = Send; Sscp.ReceiveData = Receive; Services.Stall = Stall;
  Device.Session.Sscp = &Sscp; Device.Session.MediaId = 7; Device.Session.OpalBaseComId = 0x1001;
}

int main (void) {
  OPAL_TEST_RANGE Range;
  TCG_UID Authorities[] = {OPAL_ADMIN_SP_SID_AUTHORITY, OPAL_LOCKING_SP_ADMIN1_AUTHORITY, OPAL_LOCKING_SP_USER1_AUTHORITY};
  unsigned Index;
  UINT8 BinaryPin[32] = { 0 };
  UINT8 BinaryNew[32] = { 0 };
  static UINT8 Image[OPAL_PBA_IMAGE_SIZE];
  Reset ();
  assert (OpalTestSetPin (&Device, Authorities[0], Pin, 5, NewPin, 5) == EFI_WRITE_PROTECTED);
  assert (OpalTestActivate (&Device, Pin, 5) == EFI_WRITE_PROTECTED);
  assert (OpalTestSetUser1 (&Device, Pin, 5, NewPin, 5) == EFI_WRITE_PROTECTED);
  assert (OpalTestDisableUser1 (&Device, Pin, 5) == EFI_WRITE_PROTECTED);
  assert (OpalTestSetGlobal (&Device, Pin, 5, TRUE, TRUE) == EFI_WRITE_PROTECTED);
  assert (OpalTestUserGlobal (&Device, Pin, 5, TRUE) == EFI_WRITE_PROTECTED);
  assert (OpalTestMbrDone (&Device, Pin, 5) == EFI_WRITE_PROTECTED && MbrWrites == 0);
  assert (OpalTestLoadPba (&Device, Pin, 5, Image, sizeof (Image)) == EFI_WRITE_PROTECTED && PbaWrites == 0);
  assert (OpalTestRevert (&Device, Pin, 5) == EFI_WRITE_PROTECTED && Starts == 0 && Sends == 0);
  Device.AllowWrites = TRUE;
  assert (OpalTestRevert (&Device, Pin, 5) == EFI_WRITE_PROTECTED && Starts == 0);

  for (Index = 0; Index < 3; Index++) {
    Reset ();
    assert (OpalTestVerify (&Device, Authorities[Index], Pin, 5) == EFI_SUCCESS);
    assert (!Device.AllowWrites && Starts == 1 && Ends == 1 && LastAuthority == Authorities[Index]);
    assert (Sets == 0 && Sends == 0 && Reverts == 0 && Enables == 0 && Disables == 0);
  }
  Reset (); FailStart = 1;
  assert (EFI_ERROR (OpalTestVerify (&Device, Authorities[1], Pin, 5)));
  assert (Starts == 1 && Ends == 0 && Sets == 0 && Sends == 0);
  Reset ();
  assert (OpalTestGetGlobal (&Device, Pin, 5, &Range) == EFI_SUCCESS);
  assert (!Device.AllowWrites && Sets == 0 && AceWrites == 0 && Enables == 0 && Reverts == 0);

  for (Index = 0; Index < 3; Index++) {
    Reset (); Device.AllowWrites = TRUE;
    assert (OpalTestSetPin (&Device, Authorities[Index], Pin, 5, NewPin, 5) == EFI_SUCCESS);
    assert (Starts == 2 && Ends == 2 && Sets == 1 && LastAuthority == Authorities[Index] && Reverts == 0);
  }
  // Derived PINs are binary: embedded/leading NUL must not shorten the credential.
  for (Index = 0; Index < 32; Index++) { BinaryPin[Index] = (UINT8)Index; BinaryNew[Index] = (UINT8)(255 - Index); }
  BinaryNew[7] = 0;
  Reset (); Device.AllowWrites = TRUE;
  ExpectedPin = BinaryPin; ExpectedNewPin = BinaryNew; ExpectedSize = 32;
  assert (OpalTestSetPin (&Device, Authorities[1], BinaryPin, 32, BinaryNew, 32) == EFI_SUCCESS);
  assert (Starts == 2 && Sets == 1 && Ends == 2);
  Reset (); Device.AllowWrites = TRUE; FailStart = 1;
  assert (EFI_ERROR (OpalTestSetPin (&Device, Authorities[0], Pin, 5, NewPin, 5)) && Starts == 1 && Sets == 0 && Ends == 0);
  Reset (); Device.AllowWrites = TRUE; FailMethod = 1;
  assert (EFI_ERROR (OpalTestSetPin (&Device, Authorities[0], Pin, 5, NewPin, 5)) && Starts == 1 && Sets == 1 && Ends == 1);
  Reset (); Device.AllowWrites = TRUE;
  assert (OpalTestActivate (&Device, Pin, 5) == EFI_SUCCESS && Sets == 1 && Ends == 1);
  Reset (); Device.AllowWrites = TRUE;
  assert (OpalTestSetUser1 (&Device, Pin, 5, NewPin, 5) == EFI_SUCCESS);
  assert (Sets == 1 && AceWrites == 2 && Enables == 1 && Starts == 2 && Ends == 2 && Reverts == 0);
  Reset (); Device.AllowWrites = TRUE; AceFailAt = 1;
  assert (EFI_ERROR (OpalTestSetUser1 (&Device, Pin, 5, NewPin, 5)));
  assert (AceWrites == 1 && Enables == 0 && Starts == 1 && Ends == 1 && Reverts == 0);
  Reset (); Device.AllowWrites = TRUE;
  assert (OpalTestDisableUser1 (&Device, Pin, 5) == EFI_SUCCESS && Disables == 1 && Sets == 0);
  Reset (); Device.AllowWrites = TRUE;
  assert (OpalTestSetGlobal (&Device, Pin, 5, TRUE, TRUE) == EFI_SUCCESS && Global.ReadLocked && Global.WriteLocked);
  assert (OpalTestSetGlobal (&Device, Pin, 5, TRUE, FALSE) == EFI_SUCCESS && !Global.ReadLocked && Global.ReadLockEnabled);
  assert (OpalTestSetGlobal (&Device, Pin, 5, FALSE, FALSE) == EFI_SUCCESS && !Global.ReadLockEnabled);
  assert (OpalTestGetGlobal (&Device, Pin, 5, &Range) == EFI_SUCCESS && !Range.ReadLockEnabled);
  assert (OpalTestUserGlobal (&Device, Pin, 5, TRUE) == EFI_SUCCESS && Global.ReadLocked);
  assert (OpalTestSetGlobal (&Device, Pin, 5, FALSE, TRUE) == EFI_INVALID_PARAMETER);
  Reset (); Busy = 2;
  assert (OpalTestGetGlobal (&Device, Pin, 5, &Range) == EFI_SUCCESS && Sends == 1 && Reads == 3);
  for (Index = 0; Index < 5; Index++) {
    Reset ();
    if (Index == 0) { FailSend = 1; }
    if (Index == 1) { FailReceive = 1; }
    if (Index == 2) { BadLength = 1; }
    if (Index == 3) { WrongSession = 1; }
    if (Index == 4) { ShortGet = 1; }
    assert (EFI_ERROR (OpalTestGetGlobal (&Device, Pin, 5, &Range)) && Sends == 1 && Ends == 1);
  }
  Reset (); Device.AllowWrites = TRUE; Device.AllowErase = TRUE;
  for (Index = 0; Index < sizeof (Image); Index++) { Image[Index] = (UINT8)Index; }
  PbaExpected = Image; PbaSize = sizeof (Image);
  assert (OpalTestLoadPba (&Device, Pin, 5, Image, sizeof (Image)) == EFI_SUCCESS);
  assert (PbaOffset == sizeof (Image) && PbaWrites == (sizeof (Image) + OPAL_PBA_CHUNK_SIZE - 1) / OPAL_PBA_CHUNK_SIZE);
  assert (Starts == 1 && Ends == 1 && Sets == 0 && MbrWrites == 0 && AceWrites == 0 && Reverts == 0);
  Reset (); Device.AllowWrites = TRUE; PbaExpected = Image; PbaSize = 800; FailPbaAt = 2;
  assert (EFI_ERROR (OpalTestLoadPba (&Device, Pin, 5, Image, PbaSize)));
  assert (PbaOffset == OPAL_PBA_CHUNK_SIZE && PbaWrites == 2 && Sends == 2 && Starts == 1 && Ends == 1);
  Reset (); Device.AllowWrites = TRUE; FailStart = 1;
  assert (EFI_ERROR (OpalTestLoadPba (&Device, Pin, 5, Image, 800)) && Starts == 1 && Sends == 0);
  Reset (); Device.AllowWrites = TRUE;
  assert (OpalTestLoadPba (&Device, Pin, 5, Image, 0) == EFI_INVALID_PARAMETER);
  assert (OpalTestLoadPba (&Device, Pin, 5, Image, OPAL_PBA_IMAGE_SIZE + 1) == EFI_INVALID_PARAMETER && Starts == 0);
  Reset (); Device.AllowWrites = TRUE; Device.AllowErase = TRUE;
  assert (OpalTestMbrDone (&Device, Pin, 5) == EFI_SUCCESS);
  assert (Starts == 1 && Ends == 1 && Sends == 1 && MbrWrites == 1 && MbrDone);
  assert (Sets == 0 && AceWrites == 0 && Enables == 0 && Disables == 0 && Reverts == 0);
  for (Index = 0; Index < 3; Index++) {
    Reset (); Device.AllowWrites = TRUE;
    if (Index == 0) { FailStart = 1; }
    if (Index == 1) { FailMethod = 1; }
    if (Index == 2) { FailSend = 1; }
    assert (EFI_ERROR (OpalTestMbrDone (&Device, Pin, 5)) && Starts == 1 && !MbrDone && Reverts == 0);
    assert (Sends == (Index == 0 ? 0U : 1U));
  }
  Reset (); Device.AllowWrites = TRUE; Busy = 2;
  assert (OpalTestMbrDone (&Device, Pin, 5) == EFI_SUCCESS && Sends == 1 && Reads == 3 && Ends == 1);
  Reset (); Device.AllowWrites = TRUE; Device.AllowErase = TRUE;
  assert (OpalTestRevert (&Device, Pin, 5) == EFI_SUCCESS && Reverts == 1 && Starts == 1 && Ends == 0);
  assert (!Device.AllowErase && !Device.AllowWrites);
  assert (OpalTestRevert (&Device, Pin, 5) == EFI_WRITE_PROTECTED && Reverts == 1);
  OpalTestWipe (NewPin, sizeof (NewPin));
  for (Index = 0; Index < sizeof (NewPin); Index++) { assert (NewPin[Index] == 0); }
  puts ("PASS: SID/Admin1/User1 PIN calls, authority isolation, no auth retries, User1 minimal ACEs and enable ordering.");
  puts ("PASS: Global Range Get/Set token exchange and readback, errors, receive polling, erase/write guards and PIN wipe.");
  puts ("PASS: Admin1 MBRControl.Done-only token, write guard, no auth/send retry, failure cleanup; no range/ACE/erase changes.");
  puts ("PASS: complete 8 MiB MBR byte-table upload, offsets/binary payload/short final chunk, first-error stop and guards; no MBRControl/range changes.");
  puts ("Mock OPAL operations only: NO real password, locking, reset or erase command was sent to a disk.");
  return 0;
}
