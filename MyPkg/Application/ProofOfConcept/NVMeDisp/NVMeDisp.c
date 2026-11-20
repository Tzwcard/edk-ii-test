/** @file
  Read-only NVMe Identify Controller display via UEFI NVMe PassThru.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <IndustryStandard/Nvme.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Protocol/DevicePath.h>
#include <Protocol/ShellParameters.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "IdentifyData.h"
#include "../SMARTDisp/SmartData.h"

STATIC CONST CHAR16 *YesNo (IN BOOLEAN Value) { return Value ? L"Yes" : L"No"; }

STATIC EFI_STATUS
ReadIdentify (IN EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme, OUT UINT8 *Data)
{
  EFI_NVM_EXPRESS_COMMAND Command;
  EFI_NVM_EXPRESS_COMPLETION Completion;
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;
  ZeroMem (&Command, sizeof (Command)); ZeroMem (&Completion, sizeof (Completion));
  ZeroMem (&Packet, sizeof (Packet)); ZeroMem (Data, NVME_IDENTIFY_SIZE);
  Command.Cdw0.Opcode = NVME_ADMIN_IDENTIFY_CMD;
  Command.Nsid = 0; Command.Cdw10 = 1; Command.Flags = CDW10_VALID;
  Packet.CommandTimeout = 5ULL * 1000 * 1000 * 10;
  Packet.QueueType = NVME_ADMIN_QUEUE;
  Packet.TransferBuffer = Data; Packet.TransferLength = NVME_IDENTIFY_SIZE;
  Packet.NvmeCmd = &Command; Packet.NvmeCompletion = &Completion;
  Status = Nvme->PassThru (Nvme, 0, &Packet, NULL);
  if (EFI_ERROR (Status) || (Completion.DW3 & 0x0FFE0000U) != 0) {
    Print (L"Identify failed: %r, completion DW3=0x%08x SCT=%u SC=0x%02x\r\n",
      Status, Completion.DW3, (Completion.DW3 >> 25) & 7, (Completion.DW3 >> 17) & 0xFF);
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }
  if (Status != EFI_SUCCESS || Packet.TransferLength != NVME_IDENTIFY_SIZE) {
    Print (L"Unexpected Identify transfer: %u / 4096 bytes, %r\r\n", Packet.TransferLength, Status);
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC VOID
Capabilities (IN CONST NVME_IDENTIFY_INFO *Info)
{
  STATIC CONST CHAR16 *Admin[] = {
    L"Security Send/Receive", L"Format NVM", L"Firmware Download/Commit", L"Namespace Management",
    L"Device Self-test", L"Directives", L"NVMe-MI Send/Receive", L"Virtualization Management",
    L"Doorbell Buffer Config", L"Get LBA Status"
  };
  STATIC CONST CHAR16 *Nvm[] = {
    L"Compare", L"Write Uncorrectable", L"Dataset Management", L"Write Zeroes",
    L"Save/Select Features", L"Reservations", L"Timestamp", L"Verify", L"Copy"
  };
  UINTN Index;
  CHAR8 Decimal[SMART_DECIMAL_SIZE];
  Print (L"  VID: 0x%04x  SSVID: 0x%04x  Controller ID: 0x%04x\r\n",
    (UINT32)Info->Vid, (UINT32)Info->Ssvid, (UINT32)Info->ControllerId);
  Print (L"  NVMe version: %u.%u.%u (VER=0x%08x)\r\n",
    Info->Version >> 16, (Info->Version >> 8) & 0xFF, Info->Version & 0xFF, Info->Version);
  Print (L"  IEEE OUI bytes: %02x-%02x-%02x\r\n",
    (UINT32)Info->IeeeOui[0], (UINT32)Info->IeeeOui[1], (UINT32)Info->IeeeOui[2]);
  Print (L"  Controller Type: %u (0=not reported, 1=I/O, 2=discovery, 3=admin)\r\n", (UINT32)Info->ControllerType);
  Print (L"  CMIC: 0x%02x  CTRATT: 0x%08x  OAES: 0x%08x\r\n",
    (UINT32)Info->Cmic, Info->ControllerAttributes, Info->Oaes);
  Print (L"  MDTS: %u", (UINT32)Info->Mdts);
  if (Info->Mdts == 0) { Print (L" (no transfer limit reported by MDTS)\r\n"); }
  else { Print (L" (2^MDTS * controller minimum page size; not assumed to be 4 KiB)\r\n"); }
  Print (L"  RTD3 Resume/Entry Latency: %u / %u us\r\n", Info->Rtd3ResumeUs, Info->Rtd3EntryUs);
  Print (L"  OACS: 0x%04x\r\n", (UINT32)Info->Oacs);
  for (Index = 0; Index < ARRAY_SIZE (Admin); Index++) {
    Print (L"    %s: %s\r\n", Admin[Index], YesNo ((Info->Oacs & (1U << Index)) != 0));
  }
  Print (L"    Security Send/Receive support does NOT prove OPAL or EF support.\r\n");
  Print (L"  Abort limit: %u  Async event request limit: %u (zero-based fields + 1)\r\n",
    (UINT32)Info->Acl + 1, (UINT32)Info->Aerl + 1);
  Print (L"  FRMW: 0x%02x, firmware slots=%u, slot1 read-only=%s, activation without reset=%s\r\n",
    (UINT32)Info->Frmw, (Info->Frmw >> 1) & 7, YesNo ((Info->Frmw & BIT0) != 0), YesNo ((Info->Frmw & BIT4) != 0));
  Print (L"  LPA: 0x%02x (per-namespace SMART=%s, command effects log=%s)\r\n",
    (UINT32)Info->Lpa, YesNo ((Info->Lpa & BIT0) != 0), YesNo ((Info->Lpa & BIT1) != 0));
  Print (L"  Error log entries: %u  Power states: %u (zero-based fields + 1)\r\n",
    (UINT32)Info->Elpe + 1, (UINT32)Info->Npss + 1);
  Print (L"  Autonomous power-state transitions: %s (APSTA=0x%02x)\r\n",
    YesNo ((Info->Apsta & BIT0) != 0), (UINT32)Info->Apsta);
  Print (L"  Warning/Critical Temperature Threshold: %u / %u K (0=not reported)\r\n",
    (UINT32)Info->WarningTemperatureK, (UINT32)Info->CriticalTemperatureK);
  Print (L"  HMB preferred/minimum: %u / %u units of 4 KiB\r\n", Info->HostMemoryPreferred, Info->HostMemoryMinimum);
  if (SmartDecimal (Info->TotalNvmCapacity, 16, Decimal, sizeof (Decimal))) {
    Print (L"  Total NVM Capacity: %a bytes (zero may mean not reported)\r\n", Decimal);
  }
  if (SmartDecimal (Info->UnallocatedNvmCapacity, 16, Decimal, sizeof (Decimal))) {
    Print (L"  Unallocated NVM Capacity: %a bytes (zero may mean not reported)\r\n", Decimal);
  }
  Print (L"  SANICAP: 0x%08x (crypto erase=%s, block erase=%s, overwrite=%s)\r\n", Info->Sanicap,
    YesNo ((Info->Sanicap & BIT0) != 0), YesNo ((Info->Sanicap & BIT1) != 0), YesNo ((Info->Sanicap & BIT2) != 0));
  Print (L"  SQES=0x%02x: min=%u max=%u bytes; CQES=0x%02x: min=%u max=%u bytes\r\n",
    (UINT32)Info->Sqes, 1U << (Info->Sqes & 15), 1U << (Info->Sqes >> 4),
    (UINT32)Info->Cqes, 1U << (Info->Cqes & 15), 1U << (Info->Cqes >> 4));
  Print (L"  MAXCMD: %u  NN (Namespaces): %u\r\n", (UINT32)Info->MaxCommands, Info->Namespaces);
  Print (L"  ONCS: 0x%04x\r\n", (UINT32)Info->Oncs);
  for (Index = 0; Index < ARRAY_SIZE (Nvm); Index++) {
    Print (L"    %s: %s\r\n", Nvm[Index], YesNo ((Info->Oncs & (1U << Index)) != 0));
  }
  Print (L"  FUSES: 0x%04x  FNA: 0x%02x  VWC: 0x%02x (volatile cache=%s)\r\n",
    (UINT32)Info->Fuses, (UINT32)Info->Fna, (UINT32)Info->Vwc, YesNo ((Info->Vwc & BIT0) != 0));
  Print (L"  AWUN/AWUPF raw: %u / %u  SGLS: 0x%08x\r\n",
    (UINT32)Info->Awun, (UINT32)Info->Awupf, Info->Sgls);
  Print (L"  SUBNQN: %a\r\n", Info->Subnqn[0] != 0 ? Info->Subnqn : "(not reported)");
}

STATIC EFI_STATUS
ShowController (IN EFI_HANDLE Handle, IN UINTN Index, IN BOOLEAN List, IN BOOLEAN Raw)
{
  EFI_STATUS Status;
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  CHAR16 *PathText;
  UINTN Alignment;
  UINTN Byte;
  UINT8 *Data;
  NVME_IDENTIFY_INFO Info;
  Print (L"\r\nUEFI NVMe controller %u:\r\n", (UINT32)Index);
  Status = gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path);
  if (!EFI_ERROR (Status)) {
    PathText = ConvertDevicePathToText (Path, FALSE, FALSE);
    if (PathText != NULL) { Print (L"  Path: %s\r\n", PathText); FreePool (PathText); }
  }
  Status = gBS->HandleProtocol (Handle, &gEfiNvmExpressPassThruProtocolGuid, (VOID **)&Nvme);
  if (EFI_ERROR (Status)) { return Status; }
  if (Nvme->Mode == NULL || Nvme->PassThru == NULL) { return EFI_UNSUPPORTED; }
  Alignment = MAX ((UINTN)Nvme->Mode->IoAlign, (UINTN)EFI_PAGE_SIZE);
  if ((Alignment & (Alignment - 1)) != 0) { return EFI_UNSUPPORTED; }
  Data = AllocateAlignedPages (1, Alignment);
  if (Data == NULL) { return EFI_OUT_OF_RESOURCES; }
  Status = ReadIdentify (Nvme, Data);
  if (!EFI_ERROR (Status)) {
    if (!NvmeIdentifyParse (Data, NVME_IDENTIFY_SIZE, &Info)) { Status = EFI_COMPROMISED_DATA; }
    else {
      Print (L"  Model: %a\r\n  Serial: %a\r\n  Firmware: %a\r\n", Info.Model, Info.Serial, Info.Firmware);
      if (!List) { Capabilities (&Info); }
      if (Raw) {
        Print (L"Identify Controller raw data (4096 bytes):\r\n");
        for (Byte = 0; Byte < NVME_IDENTIFY_SIZE; Byte++) {
          if (Byte % 16 == 0) { Print (L"%04x:", (UINT32)Byte); }
          Print (L" %02x", (UINT32)Data[Byte]);
          if (Byte % 16 == 15) { Print (L"\r\n"); }
        }
      }
    }
  }
  FreeAlignedPages (Data, 1);
  return Status;
}

EFI_STATUS
EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_SHELL_PARAMETERS_PROTOCOL *Parameters;
  EFI_HANDLE *Handles;
  UINTN Count;
  UINTN Index;
  UINTN Selected = 0;
  UINTN Digit;
  BOOLEAN HasIndex = FALSE;
  BOOLEAN List = FALSE;
  BOOLEAN Raw = FALSE;
  CONST CHAR16 *Argument;
  Print (L"NVMeDisp - read-only NVMe Identify Controller (CNS=1)\r\n");
  Print (L"UEFI indexes do NOT necessarily match Linux nvme0/nvme1; check model and serial.\r\n");
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Parameters);
  if (!EFI_ERROR (Status)) {
    for (Index = 1; Index < Parameters->Argc; Index++) {
      Argument = Parameters->Argv[Index];
      if (StrCmp (Argument, L"--help") == 0 || StrCmp (Argument, L"-h") == 0) {
        Print (L"Usage: NVMeDisp.efi [index] [--raw] | --list\r\nDefault: controller 0.\r\n");
        return EFI_SUCCESS;
      }
      if (StrCmp (Argument, L"--list") == 0 && !List) { List = TRUE; continue; }
      if (StrCmp (Argument, L"--raw") == 0 && !Raw) { Raw = TRUE; continue; }
      if (HasIndex || *Argument == 0) { return EFI_INVALID_PARAMETER; }
      HasIndex = TRUE;
      while (*Argument != 0) {
        if (*Argument < L'0' || *Argument > L'9') { Print (L"Invalid argument. Use NVMeDisp.efi --help\r\n"); return EFI_INVALID_PARAMETER; }
        Digit = (UINTN)(*Argument++ - L'0');
        if (Selected > (MAX_UINT32 - Digit) / 10) { return EFI_INVALID_PARAMETER; }
        Selected = Selected * 10 + Digit;
      }
    }
  }
  if (List && (HasIndex || Raw)) { Print (L"--list cannot be combined with an index or --raw.\r\n"); return EFI_INVALID_PARAMETER; }
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiNvmExpressPassThruProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) { Print (L"No NVMe PassThru controllers available: %r\r\n", Status); return Status; }
  if (!List && Selected >= Count) {
    Print (L"Controller index %u unavailable; found %u controller(s).\r\n", (UINT32)Selected, (UINT32)Count);
    FreePool (Handles); return EFI_NOT_FOUND;
  }
  for (Index = 0; Index < Count; Index++) {
    if (List || Index == Selected) {
      Status = ShowController (Handles[Index], Index, List, Raw);
      if (EFI_ERROR (Status)) { Print (L"NVMeDisp controller %u failed: %r\r\n", (UINT32)Index, Status); break; }
    }
  }
  FreePool (Handles);
  if (!EFI_ERROR (Status)) { Print (List ? L"NVMeDisp listing complete.\r\n" : L"NVMeDisp completed successfully.\r\n"); }
  return Status;
}
