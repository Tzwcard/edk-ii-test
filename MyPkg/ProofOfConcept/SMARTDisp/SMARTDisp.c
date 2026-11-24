/** @file
  Read-only Identify Controller and Get Log Page 02h via NVMe PassThru.
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
#include "SmartData.h"

STATIC
EFI_STATUS
ReadAdmin (
  IN EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme, IN UINT8 Opcode, IN UINT32 Nsid,
  IN UINT32 Cdw10, IN VOID *Buffer, IN UINT32 Size
  )
{
  EFI_NVM_EXPRESS_COMMAND Command;
  EFI_NVM_EXPRESS_COMPLETION Completion;
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;

  ZeroMem (&Command, sizeof (Command));
  ZeroMem (&Completion, sizeof (Completion));
  ZeroMem (&Packet, sizeof (Packet));
  Command.Cdw0.Opcode = Opcode;
  Command.Nsid = Nsid;
  Command.Cdw10 = Cdw10;
  Command.Flags = CDW10_VALID;
  if (Opcode == NVME_ADMIN_GET_LOG_PAGE_CMD) {
    Command.Flags |= CDW11_VALID; // NUMDU = 0
  }

  Packet.CommandTimeout = 5ULL * 1000 * 1000 * 10;
  Packet.QueueType = NVME_ADMIN_QUEUE;
  Packet.TransferBuffer = Buffer;
  Packet.TransferLength = Size;
  Packet.NvmeCmd = &Command;
  Packet.NvmeCompletion = &Completion;
  Status = Nvme->PassThru (Nvme, Nsid, &Packet, NULL);
  if (EFI_ERROR (Status) || ((Completion.DW3 & 0x0FFE0000U) != 0)) {
    Print (L"  NVMe read failed: %r, opcode=0x%02x, NSID=0x%08x\r\n",
      Status, (UINT32)Opcode, Nsid);
    Print (L"  Completion DW3=0x%08x, SCT=%u, SC=0x%02x\r\n",
      Completion.DW3, (Completion.DW3 >> 25) & 7, (Completion.DW3 >> 17) & 0xFF);
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  if ((Status != EFI_SUCCESS) || (Packet.TransferLength != Size)) {
    Print (L"  Short/unexpected transfer: %u / %u bytes, %r\r\n", Packet.TransferLength, Size, Status);
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
IdentifyText (IN CONST UINT8 *Data, IN UINTN Length, OUT CHAR8 *Text)
{
  UINTN Index;
  while ((Length > 0) && (Data[Length - 1] == ' ')) {
    Length--;
  }

  for (Index = 0; Index < Length; Index++) {
    Text[Index] = (Data[Index] >= 0x20 && Data[Index] <= 0x7E) ? (CHAR8)Data[Index] : '?';
  }

  Text[Length] = '\0';
}

STATIC
VOID
Temperature (IN UINT16 Kelvin)
{
  INT32 Celsius100;
  if (Kelvin == 0) {
    Print (L"Not reported\r\n");
    return;
  }

  Celsius100 = (INT32)Kelvin * 100 - 27315;
  Print (L"%u K (", (UINT32)Kelvin);
  if (Celsius100 < 0) {
    Print (L"-");
    Celsius100 = -Celsius100;
  }

  Print (L"%u.%02u C)\r\n", (UINT32)Celsius100 / 100, (UINT32)Celsius100 % 100);
}

STATIC
VOID
DisplaySmart (IN CONST SMART_INFO *Info)
{
  STATIC CONST CHAR16 *Names[SMART_COUNTER_COUNT] = {
    L"Data Units Read", L"Data Units Written", L"Host Read Commands", L"Host Write Commands",
    L"Controller Busy Time (minutes)", L"Power Cycles", L"Power On Hours", L"Unsafe Shutdowns",
    L"Media and Data Integrity Errors", L"Error Information Log Entries"
  };
  STATIC CONST CHAR16 *Warnings[6] = {
    L"Available spare below threshold", L"Temperature outside threshold",
    L"Reliability degraded", L"Media is read-only", L"Volatile memory backup failed",
    L"Persistent memory region read-only/unreliable"
  };
  UINTN Index;
  CHAR8 Text[SMART_DECIMAL_SIZE];

  Print (L"\r\nSMART / Health Information (log 0x02, all namespaces):\r\n");
  Print (L"  Critical Warning: 0x%02x\r\n", (UINT32)Info->CriticalWarning);
  for (Index = 0; Index < 6; Index++) {
    if ((Info->CriticalWarning & (1U << Index)) != 0) {
      Print (L"    WARNING: %s\r\n", Warnings[Index]);
    }
  }

  Print (L"  Composite Temperature: ");
  Temperature (Info->TemperatureKelvin);
  Print (L"  Available Spare: %u%%\r\n", (UINT32)Info->AvailableSpare);
  Print (L"  Spare Threshold: %u%%\r\n", (UINT32)Info->SpareThreshold);
  Print (L"  Percentage Used: %u%% (can exceed 100%%)\r\n", (UINT32)Info->PercentageUsed);
  for (Index = 0; Index < SMART_COUNTER_COUNT; Index++) {
    if (SmartDecimal (Info->Counters[Index], 16, Text, sizeof (Text))) {
      Print (L"  %s: %a\r\n", Names[Index], Text);
    }

    if ((Index < 2) && SmartDataBytes (Info->Counters[Index], Text, sizeof (Text))) {
      Print (L"    Reported data units x 512000 = %a bytes (rounded accounting)\r\n", Text);
    }
  }

  Print (L"  Warning Temperature Time: %u minutes\r\n", Info->WarningTemperatureMinutes);
  Print (L"  Critical Temperature Time: %u minutes\r\n", Info->CriticalTemperatureMinutes);
  for (Index = 0; Index < 8; Index++) {
    if (Info->SensorsKelvin[Index] != 0) {
      Print (L"  Temperature Sensor %u: ", (UINT32)Index + 1);
      Temperature (Info->SensorsKelvin[Index]);
    }
  }

  for (Index = 0; Index < 2; Index++) {
    Print (L"  Thermal Management %u Transition Count: %u\r\n",
      (UINT32)Index + 1, Info->ThermalTransitionCount[Index]);
    Print (L"  Thermal Management %u Total Time: %u seconds\r\n",
      (UINT32)Index + 1, Info->ThermalTotalSeconds[Index]);
  }
}

STATIC
EFI_STATUS
ShowController (IN EFI_HANDLE Handle, IN UINTN Index, IN BOOLEAN ListOnly)
{
  EFI_STATUS Status;
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  CHAR16 *PathText;
  UINTN Alignment;
  UINT8 *Buffer;
  CHAR8 Text[41];
  SMART_INFO Info;

  Print (L"\r\nUEFI NVMe controller %u:\r\n", (UINT32)Index);
  Status = gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path);
  if (!EFI_ERROR (Status)) {
    PathText = ConvertDevicePathToText (Path, FALSE, FALSE);
    if (PathText != NULL) {
      Print (L"  Path: %s\r\n", PathText);
      FreePool (PathText);
    }
  }

  Status = gBS->HandleProtocol (Handle, &gEfiNvmExpressPassThruProtocolGuid, (VOID **)&Nvme);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Nvme->Mode == NULL) || (Nvme->PassThru == NULL)) {
    return EFI_UNSUPPORTED;
  }

  Alignment = MAX ((UINTN)Nvme->Mode->IoAlign, (UINTN)EFI_PAGE_SIZE);
  if ((Alignment & (Alignment - 1)) != 0) {
    return EFI_UNSUPPORTED;
  }

  Buffer = AllocateAlignedPages (1, Alignment);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Buffer, EFI_PAGE_SIZE);
  Status = ReadAdmin (Nvme, NVME_ADMIN_IDENTIFY_CMD, 0, 1, Buffer, 4096);
  if (!EFI_ERROR (Status)) {
    IdentifyText (Buffer + 24, 40, Text);
    Print (L"  Model: %a\r\n", Text);
    IdentifyText (Buffer + 4, 20, Text);
    Print (L"  Serial: %a\r\n", Text);
    IdentifyText (Buffer + 64, 8, Text);
    Print (L"  Firmware: %a\r\n", Text);
  }

  if (!EFI_ERROR (Status) && !ListOnly) {
    ZeroMem (Buffer, EFI_PAGE_SIZE);
    // NUMD is zero-based dword count: 512/4-1 = 127. RAE retains async events.
    Status = ReadAdmin (Nvme, NVME_ADMIN_GET_LOG_PAGE_CMD, MAX_UINT32,
               (127U << 16) | BIT15 | LID_SMART_INFO, Buffer, SMART_LOG_SIZE);
    if (!EFI_ERROR (Status)) {
      if (SmartParse (Buffer, SMART_LOG_SIZE, &Info)) {
        DisplaySmart (&Info);
      } else {
        Status = EFI_COMPROMISED_DATA;
      }
    }
  }

  FreeAlignedPages (Buffer, 1);
  return Status;
}

EFI_STATUS
EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN Count;
  UINTN Index;
  UINTN Selected = 0;
  UINTN Digit;
  BOOLEAN ListOnly = FALSE;
  EFI_SHELL_PARAMETERS_PROTOCOL *Parameters;
  CONST CHAR16 *Argument;

  Print (L"SMARTDisp - read-only NVMe SMART / Health\r\n");
  Print (L"UEFI controller indexes are NOT guaranteed to match Linux nvme0/nvme1.\r\n");
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Parameters);
  if (!EFI_ERROR (Status) && (Parameters->Argc > 1)) {
    Argument = Parameters->Argv[1];
    if ((Parameters->Argc != 2) || (StrCmp (Argument, L"--help") == 0) || (StrCmp (Argument, L"-h") == 0)) {
      Print (L"Usage: SMARTDisp.efi [controller-index | --list]\r\nDefault: controller 0.\r\n");
      return Parameters->Argc == 2 ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
    }

    if (StrCmp (Argument, L"--list") == 0) {
      ListOnly = TRUE;
    } else {
      if (*Argument == L'\0') {
        return EFI_INVALID_PARAMETER;
      }

      while (*Argument != L'\0') {
        if ((*Argument < L'0') || (*Argument > L'9')) {
          Print (L"Invalid controller index. Use SMARTDisp.efi --list\r\n");
          return EFI_INVALID_PARAMETER;
        }

        Digit = (UINTN)(*Argument++ - L'0');
        if (Selected > (MAX_UINT32 - Digit) / 10) {
          return EFI_INVALID_PARAMETER;
        }

        Selected = Selected * 10 + Digit;
      }
    }
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiNvmExpressPassThruProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    Print (L"No NVMe PassThru controllers available: %r\r\n", Status);
    return Status;
  }

  if (!ListOnly && (Selected >= Count)) {
    Print (L"Controller index %u unavailable; found %u controller(s).\r\n", (UINT32)Selected, (UINT32)Count);
    FreePool (Handles);
    return EFI_NOT_FOUND;
  }

  Status = EFI_SUCCESS;
  for (Index = 0; Index < Count; Index++) {
    if (ListOnly || (Index == Selected)) {
      Status = ShowController (Handles[Index], Index, ListOnly);
      if (EFI_ERROR (Status)) {
        Print (L"SMARTDisp controller %u failed: %r\r\n", (UINT32)Index, Status);
        break;
      }
    }
  }

  FreePool (Handles);
  if (!EFI_ERROR (Status)) {
    Print (ListOnly ? L"Controller listing complete.\r\n" : L"SMARTDisp completed successfully.\r\n");
  }

  return Status;
}
