/** @file
  Manual OPAL POC. No automatic test sequence and no physical-device test runner.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "OpalOps.h"
#include "SedutilPin.h"
#include <IndustryStandard/Nvme.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Protocol/ShellParameters.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "../NVMeDisp/IdentifyData.h"

typedef struct {
  EFI_STORAGE_SECURITY_COMMAND_PROTOCOL Protocol; // first member, SSCP adapter
  EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *Backend;
  UINTN Alignment;
} OPAL_IO;

typedef struct {
  OPAL_IO Io;
  OPAL_TEST_DEVICE Device;
  EFI_HANDLE Controller;
  NVME_IDENTIFY_INFO Identity;
  UINT8 RawIdentity[72];
  UINT8 LockingFlags;
  UINT32 BlockSize;
  BOOLEAN SidBlocked;
  BOOLEAN InitialSidIsMsid;
} TEST_DISK;

STATIC EFI_STATUS EFIAPI
AlignedSend (IN EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *This, IN UINT32 MediaId, IN UINT64 Timeout,
  IN UINT8 Protocol, IN UINT16 Specific, IN UINTN Size, IN VOID *Data)
{
  OPAL_IO *Io = (OPAL_IO *)This;
  EFI_STATUS Status;
  UINT8 *Buffer;
  UINTN Pages;
  if (Data == NULL || Size == 0 || Size > 65536) { return EFI_INVALID_PARAMETER; }
  Pages = EFI_SIZE_TO_PAGES (Size);
  Buffer = AllocateAlignedPages (Pages, Io->Alignment);
  if (Buffer == NULL) { return EFI_OUT_OF_RESOURCES; }
  CopyMem (Buffer, Data, Size);
  Status = Io->Backend->SendData (Io->Backend, MediaId, Timeout, Protocol, Specific, Size, Buffer);
  OpalTestWipe (Buffer, EFI_PAGES_TO_SIZE (Pages));
  FreeAlignedPages (Buffer, Pages);
  return Status;
}

STATIC EFI_STATUS EFIAPI
AlignedReceive (IN EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *This, IN UINT32 MediaId, IN UINT64 Timeout,
  IN UINT8 Protocol, IN UINT16 Specific, IN UINTN Size, OUT VOID *Data, OUT UINTN *Transferred)
{
  OPAL_IO *Io = (OPAL_IO *)This;
  EFI_STATUS Status;
  UINT8 *Buffer;
  UINTN Actual = 0;
  UINTN Pages;
  if (Data == NULL || Transferred == NULL || Size == 0 || Size > 65536) { return EFI_INVALID_PARAMETER; }
  *Transferred = 0;
  ZeroMem (Data, Size);
  Pages = EFI_SIZE_TO_PAGES (Size);
  Buffer = AllocateAlignedPages (Pages, Io->Alignment);
  if (Buffer == NULL) { return EFI_OUT_OF_RESOURCES; }
  ZeroMem (Buffer, EFI_PAGES_TO_SIZE (Pages));
  Status = Io->Backend->ReceiveData (Io->Backend, MediaId, Timeout, Protocol, Specific, Size, Buffer, &Actual);
  if (Status == EFI_SUCCESS && Actual <= Size) {
    CopyMem (Data, Buffer, Actual); *Transferred = Actual;
  } else if (!EFI_ERROR (Status)) { Status = EFI_COMPROMISED_DATA; }
  OpalTestWipe (Buffer, EFI_PAGES_TO_SIZE (Pages));
  FreeAlignedPages (Buffer, Pages);
  return Status;
}

STATIC EFI_STATUS
ReadIdentity (IN EFI_HANDLE Controller, OUT NVME_IDENTIFY_INFO *Info, OUT UINT8 *Raw)
{
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme;
  EFI_NVM_EXPRESS_COMMAND Command = { 0 };
  EFI_NVM_EXPRESS_COMPLETION Completion = { 0 };
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET Packet = { 0 };
  EFI_STATUS Status;
  UINTN Alignment;
  UINT8 *Data;
  Status = gBS->HandleProtocol (Controller, &gEfiNvmExpressPassThruProtocolGuid, (VOID **)&Nvme);
  if (EFI_ERROR (Status)) { return Status; }
  if (Nvme->Mode == NULL || Nvme->PassThru == NULL) { return EFI_UNSUPPORTED; }
  Alignment = Nvme->Mode->IoAlign;
  if (Alignment > 1 && (Alignment & (Alignment - 1)) != 0) { return EFI_UNSUPPORTED; }
  Data = AllocateAlignedPages (1, MAX (Alignment, (UINTN)EFI_PAGE_SIZE));
  if (Data == NULL) { return EFI_OUT_OF_RESOURCES; }
  ZeroMem (Data, 4096);
  Command.Cdw0.Opcode = NVME_ADMIN_IDENTIFY_CMD;
  Command.Cdw10 = 1; Command.Flags = CDW10_VALID;
  Packet.CommandTimeout = 50000000; Packet.QueueType = NVME_ADMIN_QUEUE;
  Packet.TransferBuffer = Data; Packet.TransferLength = 4096;
  Packet.NvmeCmd = &Command; Packet.NvmeCompletion = &Completion;
  Status = Nvme->PassThru (Nvme, 0, &Packet, NULL);
  if (Status == EFI_SUCCESS && Packet.TransferLength == 4096 && (Completion.DW3 & 0x0FFE0000U) == 0 &&
      NvmeIdentifyParse (Data, 4096, Info)) { CopyMem (Raw, Data, 72); }
  else if (!EFI_ERROR (Status)) { Status = EFI_DEVICE_ERROR; }
  FreeAlignedPages (Data, 1);
  return Status;
}

STATIC UINT16 Be16 (IN CONST UINT8 *Data) { return (UINT16)(((UINT16)Data[0] << 8) | Data[1]); }
STATIC UINT32 Be32 (IN CONST UINT8 *Data) {
  return ((UINT32)Data[0] << 24) | ((UINT32)Data[1] << 16) | ((UINT32)Data[2] << 8) | Data[3];
}

STATIC EFI_STATUS
Discovery (IN OUT TEST_DISK *Disk)
{
  UINT8 Data[4096];
  UINTN Actual;
  UINTN End;
  UINTN Offset;
  UINTN Size;
  UINT16 Code;
  UINT16 ComId = 0;
  BOOLEAN Opal2 = FALSE;
  BOOLEAN HasLocking = FALSE;
  BOOLEAN HasTcg = FALSE;
  EFI_STATUS Status;
  OPAL_SESSION *Session = &Disk->Device.Session;
  Disk->LockingFlags = 0; Disk->SidBlocked = FALSE; Disk->InitialSidIsMsid = FALSE;
  Session->OpalBaseComId = 0;
  Status = AlignedReceive (Session->Sscp, Session->MediaId, 50000000, 0, 0, 512, Data, &Actual);
  if (EFI_ERROR (Status)) { return Status; }
  if (Actual < 8 || Be16 (Data + 6) > Actual - 8) { return EFI_COMPROMISED_DATA; }
  for (Offset = 0; Offset < Be16 (Data + 6); Offset++) { if (Data[8 + Offset] == 1) { HasTcg = TRUE; } }
  if (!HasTcg) { Print (L"TCG protocol 01 not advertised; no OPAL write command sent.\r\n"); return EFI_UNSUPPORTED; }
  Status = AlignedReceive (Session->Sscp, Session->MediaId, 50000000, 1, SwapBytes16 (1), sizeof (Data), Data, &Actual);
  if (EFI_ERROR (Status)) { return Status; }
  if (Actual < 48 || Be32 (Data) < 44 || Be32 (Data) > Actual - 4) { return EFI_COMPROMISED_DATA; }
  End = (UINTN)Be32 (Data) + 4;
  for (Offset = 48; Offset < End; Offset += Size + 4) {
    if (End - Offset < 4) { return EFI_COMPROMISED_DATA; }
    Code = Be16 (Data + Offset); Size = Data[Offset + 3];
    if (Size > End - Offset - 4) { return EFI_COMPROMISED_DATA; }
    if (Code == TCG_FEATURE_LOCKING) {
      if (Size < 12) { return EFI_COMPROMISED_DATA; }
      HasLocking = TRUE; Disk->LockingFlags = Data[Offset + 4];
    } else if (Code == TCG_FEATURE_OPAL_SSC_V2_0_0) {
      if (Size < 16) { return EFI_COMPROMISED_DATA; }
      Opal2 = TRUE; ComId = Be16 (Data + Offset + 4);
      Disk->InitialSidIsMsid = Data[Offset + 13] == 0;
    } else if (Code == TCG_FEATURE_OPAL_SSC_V1_0_0 && !Opal2) {
      if (Size < 16) { return EFI_COMPROMISED_DATA; }
      ComId = Be16 (Data + Offset + 4); Disk->InitialSidIsMsid = TRUE;
    } else if (Code == TCG_FEATURE_BLOCK_SID) {
      if (Size < 12) { return EFI_COMPROMISED_DATA; }
      Disk->SidBlocked = (Data[Offset + 4] & BIT1) != 0;
    }
  }
  if (!HasLocking || (Disk->LockingFlags & (BIT0 | BIT3)) != (BIT0 | BIT3) || ComId < 0x1000) {
    Print (L"Complete OPAL 1/2 SSC + Locking + MediaEncryption not found; refusing mutation.\r\n");
    return EFI_UNSUPPORTED;
  }
  Session->OpalBaseComId = ComId;
  Print (L"MBREnabled=%u MBRDone=%u ShadowActive=%u\r\n",
    (UINT32)((Disk->LockingFlags & BIT4) != 0), (UINT32)((Disk->LockingFlags & BIT5) != 0),
    (UINT32)((Disk->LockingFlags & (BIT4 | BIT5)) == BIT4));
  Print (L"OPAL %s, ComID=0x%04x, LockingSupported=1 MediaEncryption=1\r\n", Opal2 ? L"2" : L"1", (UINT32)ComId);
  Print (L"LockingEnabled=%u Locked=%u SIDBlocked=%u InitialSidIsMsid=%u\r\n",
    (UINT32)((Disk->LockingFlags & BIT1) != 0), (UINT32)((Disk->LockingFlags & BIT2) != 0),
    (UINT32)Disk->SidBlocked, (UINT32)Disk->InitialSidIsMsid);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
SelectDisk (IN UINTN Selected, IN BOOLEAN List, OUT TEST_DISK *Disk, IN BOOLEAN DeriveOnly)
{
  EFI_HANDLE *Handles;
  EFI_HANDLE Controller;
  EFI_BLOCK_IO_PROTOCOL *Block;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_DEVICE_PATH_PROTOCOL *Remaining;
  EFI_STORAGE_SECURITY_COMMAND_PROTOCOL *Security;
  CHAR16 *Text;
  UINTN Count;
  UINTN Index;
  UINTN Number = 0;
  EFI_STATUS Status;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) { return Status; }
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&Block)) ||
        Block->Media == NULL || Block->Media->LogicalPartition) { continue; }
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path))) { continue; }
    Remaining = Path;
    if (EFI_ERROR (gBS->LocateDevicePath (&gEfiNvmExpressPassThruProtocolGuid, &Remaining, &Controller)) ||
        DevicePathType (Remaining) != MESSAGING_DEVICE_PATH || DevicePathSubType (Remaining) != MSG_NVME_NAMESPACE_DP) { continue; }
    if (List || Number == Selected) {
      Print (L"\r\nNVMe whole-namespace device %u:\r\n", (UINT32)Number);
      Text = ConvertDevicePathToText (Path, FALSE, FALSE);
      if (Text != NULL) { Print (L"Path: %s\r\n", Text); FreePool (Text); }
      Status = ReadIdentity (Controller, &Disk->Identity, Disk->RawIdentity);
      if (EFI_ERROR (Status)) { FreePool (Handles); return Status; }
      Print (L"Model: %a\r\nSerial: %a\r\nFirmware: %a\r\n", Disk->Identity.Model, Disk->Identity.Serial, Disk->Identity.Firmware);
      if (!List) {
        Disk->BlockSize = Block->Media->BlockSize;
        // Offline derivation needs Identify only, not SSCP or OPAL support.
        if (DeriveOnly) { FreePool (Handles); return EFI_SUCCESS; }
        Status = gBS->HandleProtocol (Handles[Index], &gEfiStorageSecurityCommandProtocolGuid, (VOID **)&Security);
        if (EFI_ERROR (Status)) { FreePool (Handles); return Status; }
        if (Security->ReceiveData == NULL || Security->SendData == NULL) { FreePool (Handles); return EFI_UNSUPPORTED; }
        Disk->Io.Alignment = Block->Media->IoAlign;
        if (Disk->Io.Alignment > 1 && (Disk->Io.Alignment & (Disk->Io.Alignment - 1)) != 0) {
          FreePool (Handles); return EFI_UNSUPPORTED;
        }
        Disk->Io.Alignment = MAX (Disk->Io.Alignment, (UINTN)EFI_PAGE_SIZE);
        Disk->Io.Backend = Security;
        Disk->Io.Protocol.SendData = AlignedSend; Disk->Io.Protocol.ReceiveData = AlignedReceive;
        Disk->Device.Session.Sscp = &Disk->Io.Protocol;
        Disk->Device.Session.MediaId = Block->Media->MediaId;
        Disk->Controller = Controller;
        FreePool (Handles);
        return Discovery (Disk);
      }
    }
    Number++;
  }
  FreePool (Handles);
  if (!List || Number == 0) { Print (L"Target unavailable; no matching whole NVMe namespace.\r\n"); return EFI_NOT_FOUND; }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadPbaImage (IN EFI_HANDLE ImageHandle, OUT UINT8 **Image)
{
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL *Root = NULL;
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_STATUS Status;
  UINT64 FileSize;
  UINTN Size;
  *Image = NULL;
  // Fixed path on the filesystem from which this application was loaded.
  // It is never read from or written to the target namespace's raw LBAs.
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) { return Status; }
  Status = gBS->HandleProtocol (Loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
  if (EFI_ERROR (Status)) { return Status; }
  Status = FileSystem->OpenVolume (FileSystem, &Root);
  if (EFI_ERROR (Status)) { return Status; }
  Status = Root->Open (Root, &File, L"\\OpalPba.img", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = File->SetPosition (File, MAX_UINT64);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = File->GetPosition (File, &FileSize);
  if (EFI_ERROR (Status)) { goto Done; }
  if (FileSize != OPAL_PBA_IMAGE_SIZE) { Status = EFI_BAD_BUFFER_SIZE; goto Done; }
  Status = File->SetPosition (File, 0);
  if (EFI_ERROR (Status)) { goto Done; }
  *Image = AllocatePool (OPAL_PBA_IMAGE_SIZE);
  if (*Image == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  Size = OPAL_PBA_IMAGE_SIZE;
  Status = File->Read (File, &Size, *Image);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Size != OPAL_PBA_IMAGE_SIZE || (*Image)[510] != 0x55 || (*Image)[511] != 0xAA ||
      CompareMem (*Image + 512, "EFI PART", 8) != 0) { Status = EFI_COMPROMISED_DATA; }
Done:
  if (File != NULL) { File->Close (File); }
  Root->Close (Root);
  if (EFI_ERROR (Status) && *Image != NULL) { FreePool (*Image); *Image = NULL; }
  return Status;
}

STATIC EFI_STATUS
ReadLine (IN CONST CHAR16 *Prompt, OUT CHAR8 *Output, IN UINTN Capacity, IN BOOLEAN Hidden)
{
  EFI_INPUT_KEY Key;
  EFI_STATUS Status;
  UINTN Length = 0;
  UINTN EventIndex;
  ZeroMem (Output, Capacity);
  Print (L"%s", Prompt);
  if (gST->ConIn == NULL) { return EFI_UNSUPPORTED; }
  for (;;) {
    Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR (Status)) { return Status; }
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (Status == EFI_NOT_READY) { continue; }
    if (EFI_ERROR (Status)) { return Status; }
    if (Key.ScanCode == SCAN_ESC || Key.UnicodeChar == 0x1B) { Print (L"\r\n"); return EFI_ABORTED; }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) { Print (L"\r\n"); return EFI_SUCCESS; }
    if (Key.UnicodeChar == CHAR_BACKSPACE) {
      if (Length != 0) { Output[--Length] = 0; if (!Hidden) { Print (L"\b \b"); } }
    } else if (Key.UnicodeChar >= 0x20 && Key.UnicodeChar <= 0x7E) {
      if (Length + 1 >= Capacity) { Print (L"\r\nInput too long; cancelled.\r\n"); return EFI_ABORTED; }
      Output[Length++] = (CHAR8)Key.UnicodeChar;
      if (!Hidden) { Print (L"%c", Key.UnicodeChar); }
    }
  }
}

STATIC VOID
Help (VOID)
{
  Print (L"Usage: OpalTest.efi --list | <command> <device-index> [--sedutil] [--msid | --destroy-data]\r\n");
  Print (L"Read-only: status, global-status, verify-sid, verify-admin1, verify-user1\r\n");
  Print (L"Offline: derive-pin <index> - show raw salt, sedutil PIN and TCG token; NO OPAL session.\r\n");
  Print (L"WARNING: derive-pin displays a usable credential. Do not share PIN/screenshots.\r\n");
  Print (L"Writes: sid-set, admin1-set, user1-set, user1-change, activate, user1-disable\r\n");
  Print (L"Range: global-lock, global-unlock, locking-disable, user1-lock, user1-unlock\r\n");
  Print (L"MBR: mbr-done <index> - Admin1 sets Done=1, bypassing Shadow MBR for this boot.\r\n");
  Print (L"PBA: load-pba <index> - Admin1 uploads \\OpalPba.img from this application's filesystem.\r\n");
  Print (L"Overwrites Shadow MBR image ONLY; does not set Enable/Done. Requires WRITE + serial.\r\n");
  Print (L"DESTRUCTIVE: revert <index> --destroy-data (Admin SP factory reset / data loss)\r\n");
  Print (L"sid-set --msid: use MSID only if Discovery declares it as initial SID. No auth fallback.\r\n");
  Print (L"Restore SID/Admin1 by sid-set/admin1-set with your known original PIN; they cannot be disabled like User1.\r\n");
  Print (L"Default: raw ASCII PIN (1-32 bytes). --sedutil: PBKDF2-HMAC-SHA1 (1-256 password bytes).\r\n");
  Print (L"--sedutil applies to BOTH current and new passwords; raw MSID is never hashed.\r\n");
  Print (L"75000 iterations, raw 20-byte NVMe serial salt, 32-byte PIN. No mode/auth retry.\r\n");
  Print (L"One operation per invocation. Passwords hidden; Esc cancels. Existing lockout is NOT cleared.\r\n");
  Print (L"Global lock/unlock is NOT bulk encryption/decryption; media stays encrypted.\r\n");
}

EFI_STATUS EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_SHELL_PARAMETERS_PROTOCOL *Parameters;
  TEST_DISK Disk = { 0 };
  OPAL_TEST_RANGE Range;
  NVME_IDENTIFY_INFO Check;
  UINT8 Raw[72];
  UINT8 Pin[SEDUTIL_PASSWORD_MAX + 1] = { 0 };
  UINT8 NewPin[SEDUTIL_PASSWORD_MAX + 1] = { 0 };
  UINT8 Confirmation[SEDUTIL_PASSWORD_MAX + 1] = { 0 };
  UINT8 *PbaImage = NULL;
  CHAR8 Input[80];
  EFI_STATUS Status;
  TCG_RESULT Result;
  CONST CHAR16 *Command;
  CONST CHAR16 *Argument;
  UINTN Index = 0;
  UINTN Digit;
  UINTN Arg;
  UINTN InputCapacity;
  UINTN Byte;
  UINT32 Size = 0;
  UINT32 NewSize = 0;
  TCG_UID Authority = OPAL_LOCKING_SP_ADMIN1_AUTHORITY;
  BOOLEAN SetPin;
  BOOLEAN ReadOnly;
  BOOLEAN Destroy;
  BOOLEAN Msid = FALSE;
  BOOLEAN Sedutil = FALSE;
  BOOLEAN DestroyConfirmed = FALSE;
  BOOLEAN DeriveOnly;

  Print (L"OpalTest - manual OPAL POC; use ONLY backed-up disposable test SSDs.\r\n");
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Parameters);
  if (EFI_ERROR (Status)) { return Status; }
  if (Parameters->Argc == 1 || (Parameters->Argc == 2 &&
      (StrCmp (Parameters->Argv[1], L"--help") == 0 || StrCmp (Parameters->Argv[1], L"-h") == 0))) { Help (); return EFI_SUCCESS; }
  if (Parameters->Argc == 2 && StrCmp (Parameters->Argv[1], L"--list") == 0) { return SelectDisk (0, TRUE, &Disk, FALSE); }
  if (Parameters->Argc < 3 || Parameters->Argc > 5) { Help (); return EFI_INVALID_PARAMETER; }
  Command = Parameters->Argv[1]; Argument = Parameters->Argv[2];
  if (*Argument == 0) { return EFI_INVALID_PARAMETER; }
  while (*Argument != 0) {
    if (*Argument < L'0' || *Argument > L'9') { return EFI_INVALID_PARAMETER; }
    Digit = (UINTN)(*Argument++ - L'0');
    if (Index > (MAX_UINT32 - Digit) / 10) { return EFI_INVALID_PARAMETER; }
    Index = Index * 10 + Digit;
  }
  DeriveOnly = StrCmp (Command, L"derive-pin") == 0;
  SetPin = StrCmp (Command, L"sid-set") == 0 || StrCmp (Command, L"admin1-set") == 0 ||
    StrCmp (Command, L"user1-set") == 0 || StrCmp (Command, L"user1-change") == 0;
  ReadOnly = DeriveOnly || StrCmp (Command, L"status") == 0 || StrCmp (Command, L"global-status") == 0 ||
    StrCmp (Command, L"verify-sid") == 0 || StrCmp (Command, L"verify-admin1") == 0 || StrCmp (Command, L"verify-user1") == 0;
  Destroy = StrCmp (Command, L"revert") == 0;
  if (!SetPin && !ReadOnly && !Destroy && StrCmp (Command, L"activate") != 0 && StrCmp (Command, L"user1-disable") != 0 &&
      StrCmp (Command, L"global-lock") != 0 && StrCmp (Command, L"global-unlock") != 0 && StrCmp (Command, L"locking-disable") != 0 &&
      StrCmp (Command, L"user1-lock") != 0 && StrCmp (Command, L"user1-unlock") != 0 &&
      StrCmp (Command, L"mbr-done") != 0 && StrCmp (Command, L"load-pba") != 0) { Help (); return EFI_INVALID_PARAMETER; }
  for (Arg = 3; Arg < Parameters->Argc; Arg++) {
    Argument = Parameters->Argv[Arg];
    if (StrCmp (Argument, L"--sedutil") == 0 && !Sedutil) { Sedutil = TRUE; }
    else if (StrCmp (Argument, L"--msid") == 0 && !Msid && StrCmp (Command, L"sid-set") == 0) { Msid = TRUE; }
    else if (StrCmp (Argument, L"--destroy-data") == 0 && !DestroyConfirmed && Destroy) { DestroyConfirmed = TRUE; }
    else { Print (L"Invalid or duplicate option: %s\r\n", Argument); return EFI_INVALID_PARAMETER; }
  }
  if (Destroy && !DestroyConfirmed) {
    Print (L"Revert refused: --destroy-data is required.\r\n"); return EFI_INVALID_PARAMETER;
  }
  if (DeriveOnly) { Sedutil = TRUE; }
  InputCapacity = Sedutil ? sizeof (Pin) : OPAL_TEST_PIN_MAX + 1;
  Print (Sedutil ? L"PIN mode: sedutil PBKDF2-HMAC-SHA1 / 75000 / raw serial[20] / PIN[32].\r\n" : L"PIN mode: raw ASCII (no password derivation).\r\n");
  Status = SelectDisk (Index, FALSE, &Disk, DeriveOnly);
  if (EFI_ERROR (Status)) {
    Print (L"OPAL discovery/interface unavailable: %r. No mutation attempted.\r\n", Status);
    return Status;
  }
  if (StrCmp (Command, L"status") == 0) { return EFI_SUCCESS; }
  if (DeriveOnly) {
    Print (L"WARNING: output PIN is a usable disk credential; do not share/save screenshots.\r\n");
    Print (L"Raw salt hex (Identify bytes 4..23, 20 bytes): ");
    for (Byte = 4; Byte < 24; Byte++) { Print (L"%02x", (UINT32)Disk.RawIdentity[Byte]); }
    Print (L"\r\n");
    Status = ReadLine (L"Password (hidden, 1-256 ASCII bytes; Esc cancels): ", (CHAR8 *)Pin, sizeof (Pin), TRUE);
    if (EFI_ERROR (Status)) { goto Done; }
    Size = (UINT32)AsciiStrLen ((CHAR8 *)Pin);
    Status = OpalSedutilDerivePin (Pin, Size, Disk.RawIdentity + 4, NewPin);
    OpalTestWipe (Pin, sizeof (Pin));
    if (EFI_ERROR (Status)) { goto Done; }
    Print (L"Password length: %u bytes (terminator excluded)\r\n", Size);
    Print (L"Derived PIN hex (32 bytes): ");
    for (Byte = 0; Byte < SEDUTIL_PIN_SIZE; Byte++) { Print (L"%02x", (UINT32)NewPin[Byte]); }
    Print (L"\r\nTCG byte-string token (34 bytes): d020");
    for (Byte = 0; Byte < SEDUTIL_PIN_SIZE; Byte++) { Print (L"%02x", (UINT32)NewPin[Byte]); }
    Print (L"\r\nd0 20 is encoding only, NOT part of the PIN. No OPAL session or credential sent.\r\n");
    goto Done;
  }
  if (StrCmp (Command, L"sid-set") == 0 || StrCmp (Command, L"activate") == 0 || Destroy ||
      StrCmp (Command, L"verify-sid") == 0) { Authority = OPAL_ADMIN_SP_SID_AUTHORITY; }
  else if (StrCmp (Command, L"user1-change") == 0 || StrCmp (Command, L"verify-user1") == 0 ||
      StrCmp (Command, L"user1-lock") == 0 || StrCmp (Command, L"user1-unlock") == 0) { Authority = OPAL_LOCKING_SP_USER1_AUTHORITY; }
  if (Authority == OPAL_ADMIN_SP_SID_AUTHORITY && Disk.SidBlocked) {
    Print (L"Block SID is active; refusing SID authentication. No reset/unblock attempted.\r\n"); return EFI_ACCESS_DENIED;
  }
  if (Authority != OPAL_ADMIN_SP_SID_AUTHORITY && !(Disk.LockingFlags & BIT1)) {
    Print (L"Locking SP not active; activate explicitly using SID first.\r\n"); return EFI_NOT_READY;
  }
  if (StrCmp (Command, L"load-pba") == 0) {
    // The generated image has 512-byte GPT/FAT sectors, not a 4Kn layout.
    if (Disk.BlockSize != 512) { Print (L"Bundled PBA supports 512-byte logical blocks only.\r\n"); return EFI_UNSUPPORTED; }
    Status = ReadPbaImage (ImageHandle, &PbaImage);
    if (EFI_ERROR (Status)) { Print (L"Cannot read valid 8 MiB \\OpalPba.img beside application filesystem root: %r\r\n", Status); goto Done; }
    Print (L"PBA source: \\OpalPba.img on application filesystem, %u bytes.\r\n", OPAL_PBA_IMAGE_SIZE);
    Print (L"WARNING: replaces existing Shadow MBR/PBA. Failure may leave an incomplete image.\r\n");
    Print (L"No raw namespace writes; Enable/Done/reset policy will NOT be changed.\r\n");
  }
  if (!ReadOnly) {
    Print (L"WARNING: modifies SSD security; may lock ALL applicable namespaces/LBAs.\r\n");
    Print (L"Global Range does not prove coverage of separately configured ranges/namespaces.\r\n");
    Print (L"Run Shell from separate USB/ISO. Close target disk files. NO automatic rollback.\r\n");
    if (StrCmp (Command, L"user1-set") == 0) {
      Print (L"User1 setup overwrites ONLY Global ReadLocked/WriteLocked ACEs with Admins OR User1.\r\n");
    }
    Print (L"Selected operation: %s; target serial: %a\r\n", Command, Disk.Identity.Serial);
    if (StrCmp (Command, L"mbr-done") == 0) {
      Print (L"Sets ONLY MBRControl.Done=1; does not unlock ranges or change reset policy.\r\n");
      Print (L"After success, boot your OS in THIS boot session; resetting may reactivate Shadow MBR.\r\n");
    }
    Status = ReadLine (L"Type WRITE (or ERASE for revert) then Enter; Esc cancels: ", Input, sizeof (Input), FALSE);
    if (EFI_ERROR (Status)) { goto Done; }
    if (AsciiStrCmp (Input, Destroy ? "ERASE" : "WRITE") != 0) { Status = EFI_ABORTED; goto Done; }
    if (Disk.Identity.Serial[0] == 0) { Status = EFI_SECURITY_VIOLATION; goto Done; }
    Status = ReadLine (L"Type the displayed serial exactly then Enter: ", Input, sizeof (Input), FALSE);
    if (EFI_ERROR (Status)) { goto Done; }
    if (AsciiStrCmp (Input, Disk.Identity.Serial) != 0) { Status = EFI_ABORTED; goto Done; }
    Status = ReadIdentity (Disk.Controller, &Check, Raw);
    if (EFI_ERROR (Status)) { goto Done; }
    if (CompareMem (Raw, Disk.RawIdentity, sizeof (Raw)) != 0) { Status = EFI_SECURITY_VIOLATION; goto Done; }
  }
  Print (L"Authenticate as %s. No automatic authority/password retry.\r\n",
    Authority == OPAL_ADMIN_SP_SID_AUTHORITY ? L"SID" : Authority == OPAL_LOCKING_SP_USER1_AUTHORITY ? L"User1" : L"Admin1");
  if (Msid) {
    if (!Disk.InitialSidIsMsid) { Print (L"Discovery does not declare initial SID=MSID.\r\n"); Status = EFI_UNSUPPORTED; goto Done; }
    // Public MSID read in an unauthenticated Admin SP session, not a guessed PIN.
    {
      UINT8 MethodStatus = 0xFF;
      Result = OpalStartSession (&Disk.Device.Session, OPAL_UID_ADMIN_SP, TRUE, 0, NULL,
        OPAL_ADMIN_SP_ANYBODY_AUTHORITY, &MethodStatus);
      if (Result != TcgResultSuccess || MethodStatus != 0) { Status = EFI_DEVICE_ERROR; goto Done; }
      Size = 0;
      Result = OpalGetMsid (&Disk.Device.Session, OPAL_TEST_PIN_MAX, Pin, &Size);
      if (OpalEndSession (&Disk.Device.Session) != TcgResultSuccess || Result != TcgResultSuccess || Size == 0 || Size > OPAL_TEST_PIN_MAX) {
        Status = EFI_DEVICE_ERROR; goto Done;
      }
    }
  } else {
    Status = ReadLine (L"Current password/PIN (hidden; Esc cancels): ", (CHAR8 *)Pin, InputCapacity, TRUE);
    if (EFI_ERROR (Status)) { goto Done; }
    Size = (UINT32)AsciiStrLen ((CHAR8 *)Pin);
    if (Size == 0) { Status = EFI_INVALID_PARAMETER; goto Done; }
  }
  if (SetPin) {
    Status = ReadLine (L"New password/PIN (hidden): ", (CHAR8 *)NewPin, InputCapacity, TRUE);
    if (EFI_ERROR (Status)) { goto Done; }
    Status = ReadLine (L"Confirm new password/PIN (hidden): ", (CHAR8 *)Confirmation, InputCapacity, TRUE);
    if (EFI_ERROR (Status)) { goto Done; }
    NewSize = (UINT32)AsciiStrLen ((CHAR8 *)NewPin);
    if (NewSize == 0 || NewSize != AsciiStrLen ((CHAR8 *)Confirmation) || CompareMem (NewPin, Confirmation, NewSize) != 0) {
      Status = EFI_INVALID_PARAMETER; goto Done;
    }
  }
  if (Sedutil) {
    // Passwords have been compared as text; from here onward use explicit binary
    // lengths. Derived PINs may contain NULs and must NEVER use AsciiStrLen.
    if (!Msid) {
      Status = OpalSedutilDerivePin (Pin, Size, Disk.RawIdentity + 4, Pin);
      if (EFI_ERROR (Status)) { goto Done; }
      Size = SEDUTIL_PIN_SIZE;
      OpalTestWipe (Pin + Size, sizeof (Pin) - Size);
    }
    if (SetPin) {
      Status = OpalSedutilDerivePin (NewPin, NewSize, Disk.RawIdentity + 4, NewPin);
      if (EFI_ERROR (Status)) { goto Done; }
      NewSize = SEDUTIL_PIN_SIZE;
      OpalTestWipe (NewPin + NewSize, sizeof (NewPin) - NewSize);
    }
  }
  Disk.Device.AllowWrites = !ReadOnly; Disk.Device.AllowErase = Destroy;
  if (StrCmp (Command, L"user1-set") == 0) { Status = OpalTestSetUser1 (&Disk.Device, Pin, Size, NewPin, NewSize); }
  else if (SetPin) { Status = OpalTestSetPin (&Disk.Device, Authority, Pin, Size, NewPin, NewSize); }
  else if (StrCmp (Command, L"activate") == 0) { Status = OpalTestActivate (&Disk.Device, Pin, Size); }
  else if (StrCmp (Command, L"user1-disable") == 0) { Status = OpalTestDisableUser1 (&Disk.Device, Pin, Size); }
  else if (StrCmp (Command, L"load-pba") == 0) { Status = OpalTestLoadPba (&Disk.Device, Pin, Size, PbaImage, OPAL_PBA_IMAGE_SIZE); }
  else if (StrCmp (Command, L"mbr-done") == 0) {
    Status = OpalTestMbrDone (&Disk.Device, Pin, Size);
    if (!EFI_ERROR (Status)) {
      Status = Discovery (&Disk);
      if (!EFI_ERROR (Status) && (Disk.LockingFlags & (BIT4 | BIT5)) == BIT4) {
        Print (L"MBR Shadow remains active after Set; readback mismatch.\r\n"); Status = EFI_DEVICE_ERROR;
      }
    }
  }
  else if (StrCmp (Command, L"global-lock") == 0) { Status = OpalTestSetGlobal (&Disk.Device, Pin, Size, TRUE, TRUE); }
  else if (StrCmp (Command, L"global-unlock") == 0) { Status = OpalTestSetGlobal (&Disk.Device, Pin, Size, TRUE, FALSE); }
  else if (StrCmp (Command, L"locking-disable") == 0) { Status = OpalTestSetGlobal (&Disk.Device, Pin, Size, FALSE, FALSE); }
  else if (StrCmp (Command, L"user1-lock") == 0 || StrCmp (Command, L"user1-unlock") == 0) {
    Status = OpalTestUserGlobal (&Disk.Device, Pin, Size, StrCmp (Command, L"user1-lock") == 0);
  } else if (Destroy) { Status = OpalTestRevert (&Disk.Device, Pin, Size); }
  else if (StrCmp (Command, L"global-status") == 0) {
    Status = OpalTestGetGlobal (&Disk.Device, Pin, Size, &Range);
    if (!EFI_ERROR (Status)) {
      Print (L"Global Range: ReadLockEnabled=%u WriteLockEnabled=%u ReadLocked=%u WriteLocked=%u\r\n",
        (UINT32)Range.ReadLockEnabled, (UINT32)Range.WriteLockEnabled, (UINT32)Range.ReadLocked, (UINT32)Range.WriteLocked);
    }
  } else { Status = OpalTestVerify (&Disk.Device, Authority, Pin, Size); }
Done:
  if (PbaImage != NULL) { FreePool (PbaImage); }
  OpalTestWipe (Pin, sizeof (Pin)); OpalTestWipe (NewPin, sizeof (NewPin)); OpalTestWipe (Confirmation, sizeof (Confirmation));
  Print (L"OpalTest result: %r. No other operation will run.\r\n", Status);
  if (EFI_ERROR (Status) && !ReadOnly) { Print (L"A partial change or unknown outcome is possible. Inspect state; never blindly retry Revert.\r\n"); }
  return Status;
}
