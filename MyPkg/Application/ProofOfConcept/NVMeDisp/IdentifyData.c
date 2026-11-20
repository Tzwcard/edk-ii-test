/** @file
  Explicit little-endian offsets; no casts of device-supplied packed structs.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "IdentifyData.h"

STATIC UINT16 Le16 (IN CONST UINT8 *Data)
{
  return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}
STATIC UINT32 Le32 (IN CONST UINT8 *Data)
{
  return Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) | ((UINT32)Data[3] << 24);
}
STATIC VOID Text (IN CONST UINT8 *Data, IN UINTN Length, OUT CHAR8 *Output)
{
  UINTN Index;
  while (Length > 0 && (Data[Length - 1] == ' ' || Data[Length - 1] == 0)) { Length--; }
  for (Index = 0; Index < Length; Index++) {
    Output[Index] = Data[Index] >= 0x20 && Data[Index] <= 0x7E ? (CHAR8)Data[Index] : '?';
  }
  Output[Length] = 0;
}

BOOLEAN
NvmeIdentifyParse (IN CONST UINT8 *Data, IN UINTN Size, OUT NVME_IDENTIFY_INFO *Info)
{
  NVME_IDENTIFY_INFO Parsed = { 0 };
  UINTN Index;
  if (Data == NULL || Info == NULL || Size != NVME_IDENTIFY_SIZE) { return FALSE; }
  Parsed.Vid = Le16 (Data); Parsed.Ssvid = Le16 (Data + 2);
  Text (Data + 4, 20, Parsed.Serial);
  Text (Data + 24, 40, Parsed.Model);
  Text (Data + 64, 8, Parsed.Firmware);
  for (Index = 0; Index < 3; Index++) { Parsed.IeeeOui[Index] = Data[73 + Index]; }
  Parsed.Cmic = Data[76]; Parsed.Mdts = Data[77];
  Parsed.ControllerId = Le16 (Data + 78); Parsed.Version = Le32 (Data + 80);
  Parsed.Rtd3ResumeUs = Le32 (Data + 84); Parsed.Rtd3EntryUs = Le32 (Data + 88);
  Parsed.Oaes = Le32 (Data + 92); Parsed.ControllerAttributes = Le32 (Data + 96);
  Parsed.ControllerType = Data[111];
  Parsed.Oacs = Le16 (Data + 256); Parsed.Acl = Data[258]; Parsed.Aerl = Data[259];
  Parsed.Frmw = Data[260]; Parsed.Lpa = Data[261]; Parsed.Elpe = Data[262];
  Parsed.Npss = Data[263]; Parsed.Apsta = Data[265];
  Parsed.WarningTemperatureK = Le16 (Data + 266); Parsed.CriticalTemperatureK = Le16 (Data + 268);
  Parsed.HostMemoryPreferred = Le32 (Data + 272); Parsed.HostMemoryMinimum = Le32 (Data + 276);
  for (Index = 0; Index < 16; Index++) {
    Parsed.TotalNvmCapacity[Index] = Data[280 + Index];
    Parsed.UnallocatedNvmCapacity[Index] = Data[296 + Index];
  }
  Parsed.Sanicap = Le32 (Data + 328);
  Parsed.Sqes = Data[512]; Parsed.Cqes = Data[513]; Parsed.MaxCommands = Le16 (Data + 514);
  Parsed.Namespaces = Le32 (Data + 516); Parsed.Oncs = Le16 (Data + 520);
  Parsed.Fuses = Le16 (Data + 522); Parsed.Fna = Data[524]; Parsed.Vwc = Data[525];
  Parsed.Awun = Le16 (Data + 526); Parsed.Awupf = Le16 (Data + 528);
  Parsed.Sgls = Le32 (Data + 536);
  Text (Data + 768, 256, Parsed.Subnqn);
  *Info = Parsed;
  return TRUE;
}
