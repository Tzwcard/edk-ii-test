/** @file
  User-stepped security check / driver-managed NVMe reset / security check.
  Never sends Security Send, writes blocks, or directly changes PCI/MMIO state.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <IndustryStandard/Nvme.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/ShellParameters.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "../NVMeDisp/IdentifyData.h"
#include "../TCGOpalCheck/SecurityCheck.h"

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL *ControllerPath;
  EFI_DEVICE_PATH_PROTOCOL *NamespacePath;
  UINT8 Identity[72]; // VID, SSVID, raw serial/model/firmware (no string normalization).
} RESET_TARGET;

STATIC VOID
PrintPath (IN CONST CHAR16 *Label, IN EFI_DEVICE_PATH_PROTOCOL *Path)
{
  CHAR16 *Text = ConvertDevicePathToText (Path, FALSE, FALSE);
  if (Text != NULL) { Print (L"%s%s\r\n", Label, Text); FreePool (Text); }
}

STATIC EFI_STATUS
Identify (IN EFI_HANDLE Handle, OUT UINT8 *Identity)
{
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme;
  EFI_NVM_EXPRESS_COMMAND Command;
  EFI_NVM_EXPRESS_COMPLETION Completion;
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;
  UINTN Alignment;
  UINT8 *Data;
  NVME_IDENTIFY_INFO Info;

  Status = gBS->HandleProtocol (Handle, &gEfiNvmExpressPassThruProtocolGuid, (VOID **)&Nvme);
  if (EFI_ERROR (Status)) { return Status; }
  if (Nvme->Mode == NULL || Nvme->PassThru == NULL) { return EFI_UNSUPPORTED; }
  Alignment = MAX ((UINTN)Nvme->Mode->IoAlign, (UINTN)EFI_PAGE_SIZE);
  if ((Alignment & (Alignment - 1)) != 0) { return EFI_UNSUPPORTED; }
  Data = AllocateAlignedPages (1, Alignment);
  if (Data == NULL) { return EFI_OUT_OF_RESOURCES; }
  ZeroMem (Data, NVME_IDENTIFY_SIZE);
  ZeroMem (&Command, sizeof (Command));
  ZeroMem (&Completion, sizeof (Completion));
  ZeroMem (&Packet, sizeof (Packet));
  Command.Cdw0.Opcode = NVME_ADMIN_IDENTIFY_CMD;
  Command.Nsid = 0; Command.Cdw10 = 1; Command.Flags = CDW10_VALID;
  Packet.CommandTimeout = 5ULL * 1000 * 1000 * 10;
  Packet.QueueType = NVME_ADMIN_QUEUE;
  Packet.TransferBuffer = Data; Packet.TransferLength = NVME_IDENTIFY_SIZE;
  Packet.NvmeCmd = &Command; Packet.NvmeCompletion = &Completion;
  Status = Nvme->PassThru (Nvme, 0, &Packet, NULL);
  if (Status == EFI_SUCCESS && (Completion.DW3 & 0x0FFE0000U) == 0 &&
      Packet.TransferLength == NVME_IDENTIFY_SIZE && NvmeIdentifyParse (Data, NVME_IDENTIFY_SIZE, &Info)) {
    CopyMem (Identity, Data, 72);
    Print (L"  Model: %a\r\n  Serial: %a\r\n  Firmware: %a\r\n", Info.Model, Info.Serial, Info.Firmware);
  } else {
    Print (L"Identify failed: %r, DW3=0x%08x, bytes=%u\r\n", Status, Completion.DW3, Packet.TransferLength);
    if (!EFI_ERROR (Status)) { Status = EFI_DEVICE_ERROR; }
  }
  FreeAlignedPages (Data, 1);
  return Status;
}

// Exact path match, never a new enumeration index or a best-effort fallback.
STATIC EFI_STATUS
FindExact (IN EFI_GUID *Protocol, IN EFI_DEVICE_PATH_PROTOCOL *SavedPath, OUT EFI_HANDLE *Handle)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  UINTN Count;
  UINTN Index;
  UINTN Size = GetDevicePathSize (SavedPath);
  Status = gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) { return Status; }
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path)) &&
        GetDevicePathSize (Path) == Size && CompareMem (Path, SavedPath, Size) == 0) {
      *Handle = Handles[Index]; Status = EFI_SUCCESS; break;
    }
  }
  FreePool (Handles);
  return Status;
}

// Select a whole NVMe namespace belonging to this exact PassThru controller.
STATIC EFI_STATUS
FindNamespace (IN EFI_HANDLE Controller, OUT EFI_DEVICE_PATH_PROTOCOL **SavedPath)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  EFI_HANDLE Parent;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_DEVICE_PATH_PROTOCOL *Remaining;
  UINTN Count;
  UINTN Index;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) { return Status; }
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo)) ||
        BlockIo->Media == NULL || BlockIo->Media->LogicalPartition || BlockIo->Reset == NULL) { continue; }
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path))) { continue; }
    Remaining = Path;
    if (EFI_ERROR (gBS->LocateDevicePath (&gEfiNvmExpressPassThruProtocolGuid, &Remaining, &Parent)) ||
        Parent != Controller || DevicePathType (Remaining) != MESSAGING_DEVICE_PATH ||
        DevicePathSubType (Remaining) != MSG_NVME_NAMESPACE_DP ||
        !IsDevicePathEnd (NextDevicePathNode (Remaining))) { continue; }
    *SavedPath = DuplicateDevicePath (Path);
    Status = *SavedPath == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
    break;
  }
  FreePool (Handles);
  return Status;
}

STATIC EFI_STATUS
ReopenTarget (IN RESET_TARGET *Target, OUT EFI_HANDLE *Namespace, OUT EFI_BLOCK_IO_PROTOCOL **BlockIo)
{
  EFI_HANDLE Controller;
  UINT8 Identity[72];
  EFI_STATUS Status;
  Status = FindExact (&gEfiNvmExpressPassThruProtocolGuid, Target->ControllerPath, &Controller);
  if (EFI_ERROR (Status)) { return Status; }
  Status = Identify (Controller, Identity);
  if (EFI_ERROR (Status)) { return Status; }
  if (CompareMem (Identity, Target->Identity, sizeof (Identity)) != 0) {
    Print (L"Identity changed: refusing to use a different device.\r\n");
    return EFI_SECURITY_VIOLATION;
  }
  Status = FindExact (&gEfiBlockIoProtocolGuid, Target->NamespacePath, Namespace);
  if (EFI_ERROR (Status)) { return Status; }
  Status = gBS->HandleProtocol (*Namespace, &gEfiBlockIoProtocolGuid, (VOID **)BlockIo);
  if (EFI_ERROR (Status)) { return Status; }
  if ((*BlockIo)->Media == NULL || (*BlockIo)->Media->LogicalPartition || (*BlockIo)->Reset == NULL) {
    return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}

// Require a complete line, not any key. Empty Enter never advances a step.
STATIC EFI_STATUS
Confirm (IN CONST CHAR16 *Token)
{
  CHAR16 Line[32];
  UINTN Length;
  UINTN EventIndex;
  EFI_INPUT_KEY Key;
  EFI_STATUS Status;
  if (gST->ConIn == NULL || gST->ConIn->WaitForKey == NULL) { return EFI_UNSUPPORTED; }
  for (;;) {
    Length = 0;
    Print (L"Type %s then Enter to continue; q then Enter or Esc cancels: ", Token);
    for (;;) {
      Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
      if (EFI_ERROR (Status)) { return Status; }
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (Status == EFI_NOT_READY) { continue; }
      if (EFI_ERROR (Status)) { return Status; }
      if (Key.ScanCode == SCAN_ESC || Key.UnicodeChar == 0x1B) { Print (L"\r\n"); return EFI_ABORTED; }
      if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) { break; }
      if (Key.UnicodeChar == CHAR_BACKSPACE) {
        if (Length != 0) { Length--; Print (L"\b \b"); }
      } else if (Key.UnicodeChar >= 0x20 && Key.UnicodeChar <= 0x7E) {
        if (Length + 1 >= ARRAY_SIZE (Line)) { Print (L"\r\nInput too long; cancelled.\r\n"); return EFI_ABORTED; }
        Line[Length++] = Key.UnicodeChar;
        Print (L"%c", Key.UnicodeChar);
      }
    }
    Print (L"\r\n");
    Line[Length] = 0;
    if (StrCmp (Line, L"q") == 0 || StrCmp (Line, L"Q") == 0) { return EFI_ABORTED; }
    if (StrCmp (Line, Token) == 0) { return EFI_SUCCESS; }
    Print (L"Not confirmed; no next step executed.\r\n");
  }
}

STATIC VOID
CompareResults (IN SECURITY_CHECK_RESULT *Before, IN SECURITY_CHECK_RESULT *After)
{
  Print (L"\r\n=== Before -> After (only valid responses are compared) ===\r\n");
  if (Before->AtaValid && After->AtaValid && Before->Ata.Supported && After->Ata.Supported) {
    Print (L"EF Enabled: %u -> %u\r\nEF Locked: %u -> %u\r\nEF Frozen: %u -> %u\r\n",
      (UINT32)Before->Ata.Enabled, (UINT32)After->Ata.Enabled,
      (UINT32)Before->Ata.Locked, (UINT32)After->Ata.Locked,
      (UINT32)Before->Ata.Frozen, (UINT32)After->Ata.Frozen);
  } else { Print (L"EF comparison: Unknown / unsupported; NOT evidence of unfreezing.\r\n"); }
  if (Before->DiscoveryValid && After->DiscoveryValid &&
      Before->Discovery.HasLocking && After->Discovery.HasLocking) {
    Print (L"TCG Locking flags: 0x%02x -> 0x%02x\r\n",
      (UINT32)Before->Discovery.LockingFlags, (UINT32)After->Discovery.LockingFlags);
  } else { Print (L"TCG locking comparison: Unknown / unavailable.\r\n"); }
  Print (L"Reset is driver-dependent, NOT a power cycle or a guaranteed unfreeze.\r\n");
}

STATIC EFI_STATUS
RunSteps (IN RESET_TARGET *Target)
{
  EFI_STATUS Status;
  EFI_STATUS ResetStatus;
  EFI_HANDLE Namespace;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  SECURITY_CHECK_RESULT Before;
  SECURITY_CHECK_RESULT After;

  Print (L"\r\nSTEP 1/3: TCGOpalCheck BEFORE reset (selected device only).\r\n");
  Status = Confirm (L"CHECK");
  if (EFI_ERROR (Status)) { return Status; }
  Status = ReopenTarget (Target, &Namespace, &BlockIo);
  if (EFI_ERROR (Status)) { return Status; }
  Status = CheckStorageSecurity (Namespace, &Before);
  Print (L"Before check returned: %r (EF and TCG validity are independent).\r\n", Status);

  Print (L"\r\nSTEP 2/3: ONE BlockIo->Reset(TRUE).\r\n");
  Print (L"WARNING: may reset ALL namespaces of this controller, interrupt I/O or re-lock ranges.\r\n");
  Print (L"Use a USB/ISO Shell, close disk files, and do not run on your active boot/storage workload.\r\n");
  Status = Confirm (L"RESET");
  if (EFI_ERROR (Status)) { return Status; }
  Status = ReopenTarget (Target, &Namespace, &BlockIo);
  if (EFI_ERROR (Status)) { return Status; }
  Print (L"Calling BlockIo->Reset(TRUE) now; no automatic retry.\r\n");
  ResetStatus = BlockIo->Reset (BlockIo, TRUE);
  // Never reuse protocol pointers across reset; even failure allows read-only diagnosis.
  BlockIo = NULL;
  Print (L"BlockIo->Reset(TRUE): %r\r\n", ResetStatus);

  Print (L"\r\nSTEP 3/3: re-enumerate + TCGOpalCheck AFTER reset.\r\n");
  Status = Confirm (L"CHECK");
  if (EFI_ERROR (Status)) { return Status; }
  Status = ReopenTarget (Target, &Namespace, &BlockIo);
  if (EFI_ERROR (Status)) {
    Print (L"Target not safely recovered: %r. No fallback to another drive.\r\n", Status);
    return Status;
  }
  Status = CheckStorageSecurity (Namespace, &After);
  Print (L"After check returned: %r (unknown is not unlocked/unfrozen).\r\n", Status);
  CompareResults (&Before, &After);
  Print (L"NVMeResetTest sequence completed; this does NOT assert security changed.\r\n");
  return ResetStatus;
}

EFI_STATUS EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_SHELL_PARAMETERS_PROTOCOL *Parameters;
  EFI_HANDLE *Handles;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  UINTN Count;
  UINTN Index;
  UINTN Selected = 0;
  UINTN Digit;
  BOOLEAN HasIndex = FALSE;
  BOOLEAN List = FALSE;
  CONST CHAR16 *Argument;
  RESET_TARGET Target = { 0 };

  Print (L"NVMeResetTest - interactive check / driver reset / check\r\n");
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Parameters);
  if (!EFI_ERROR (Status)) {
    for (Index = 1; Index < Parameters->Argc; Index++) {
      Argument = Parameters->Argv[Index];
      if (StrCmp (Argument, L"--help") == 0 || StrCmp (Argument, L"-h") == 0) {
        Print (L"Usage: NVMeResetTest.efi [controller-index] | --list\r\nDefault index: 0. Every step requires typed confirmation.\r\n");
        return EFI_SUCCESS;
      }
      if (StrCmp (Argument, L"--list") == 0 && !List) { List = TRUE; continue; }
      if (HasIndex || *Argument == 0) { return EFI_INVALID_PARAMETER; }
      HasIndex = TRUE;
      while (*Argument != 0) {
        if (*Argument < L'0' || *Argument > L'9') { return EFI_INVALID_PARAMETER; }
        Digit = (UINTN)(*Argument++ - L'0');
        if (Selected > (MAX_UINT32 - Digit) / 10) { return EFI_INVALID_PARAMETER; }
        Selected = Selected * 10 + Digit;
      }
    }
  }
  if (List && HasIndex) { return EFI_INVALID_PARAMETER; }
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiNvmExpressPassThruProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) { Print (L"No NVMe PassThru controllers: %r\r\n", Status); return Status; }
  if (!List && Selected >= Count) { FreePool (Handles); Print (L"Controller index unavailable.\r\n"); return EFI_NOT_FOUND; }
  Print (L"UEFI indexes are not Linux nvme indexes. Verify model, serial and path.\r\n");
  for (Index = 0; Index < Count; Index++) {
    if (!List && Index != Selected) { continue; }
    Print (L"\r\nUEFI NVMe controller %u:\r\n", (UINT32)Index);
    Status = Identify (Handles[Index], Target.Identity);
    if (EFI_ERROR (Status)) { break; }
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path);
    if (EFI_ERROR (Status)) { break; }
    PrintPath (L"  Controller: ", Path);
    if (List) { continue; }
    Target.ControllerPath = DuplicateDevicePath (Path);
    if (Target.ControllerPath == NULL) { Status = EFI_OUT_OF_RESOURCES; break; }
    Status = FindNamespace (Handles[Index], &Target.NamespacePath);
    if (EFI_ERROR (Status)) { Print (L"No eligible whole NVMe namespace with Block I/O: %r\r\n", Status); break; }
    PrintPath (L"  Reset via namespace: ", Target.NamespacePath);
    Status = RunSteps (&Target);
    break;
  }
  FreePool (Handles);
  if (Target.ControllerPath != NULL) { FreePool (Target.ControllerPath); }
  if (Target.NamespacePath != NULL) { FreePool (Target.NamespacePath); }
  if (Status == EFI_ABORTED) { Print (L"Cancelled; no further steps executed.\r\n"); }
  else if (EFI_ERROR (Status)) { Print (L"NVMeResetTest failed: %r\r\n", Status); }
  return Status;
}
