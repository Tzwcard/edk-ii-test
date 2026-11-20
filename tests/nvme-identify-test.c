/* Decode synthetic Identify Controller data; never access a controller. */
#include "IdentifyData.h"
#include <IndustryStandard/Nvme.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void le16 (UINT8 *Data, UINT16 Value) { Data[0] = (UINT8)Value; Data[1] = (UINT8)(Value >> 8); }
static void le32 (UINT8 *Data, UINT32 Value) {
  Data[0] = (UINT8)Value; Data[1] = (UINT8)(Value >> 8); Data[2] = (UINT8)(Value >> 16); Data[3] = (UINT8)(Value >> 24);
}
int main (void)
{
  UINT8 Data[4096] = { 0 };
  NVME_IDENTIFY_INFO Info;
  UINTN Index;
  unsigned Test;
  unsigned State = 12345;
  assert (sizeof (NVME_ADMIN_CONTROLLER_DATA) == 4096);
  assert (OFFSET_OF (NVME_ADMIN_CONTROLLER_DATA, Oacs) == 256);
  assert (OFFSET_OF (NVME_ADMIN_CONTROLLER_DATA, Tnvmcap) == 280);
  assert (OFFSET_OF (NVME_ADMIN_CONTROLLER_DATA, Sanicap) == 328);
  assert (OFFSET_OF (NVME_ADMIN_CONTROLLER_DATA, Nn) == 516);
  assert (OFFSET_OF (NVME_ADMIN_CONTROLLER_DATA, Sgls) == 536);
  assert (OFFSET_OF (NVME_ADMIN_CONTROLLER_DATA, Subnqn) == 768);
  assert (!NvmeIdentifyParse (NULL, 4096, &Info));
  assert (!NvmeIdentifyParse (Data, 4096, NULL));
  assert (!NvmeIdentifyParse (Data, 4095, &Info));
  assert (!NvmeIdentifyParse (Data, 4097, &Info));
  le16 (Data, 0x144D); le16 (Data + 2, 0x1234);
  memset (Data + 4, ' ', 20); memcpy (Data + 4, "SERIAL", 6); Data[10] = 0x1B;
  memset (Data + 24, 'M', 40); memcpy (Data + 64, "FW01", 4);
  Data[73] = 0xAA; Data[74] = 0xBB; Data[75] = 0xCC;
  Data[76] = 3; Data[77] = 7; le16 (Data + 78, 0x1122); le32 (Data + 80, 0x00010400);
  le32 (Data + 84, 100); le32 (Data + 88, 200); le32 (Data + 92, 0xABCD1234); le32 (Data + 96, 0x12345678);
  Data[111] = 1; le16 (Data + 256, 0x03FF);
  Data[258] = 3; Data[259] = 4; Data[260] = 0x17; Data[261] = 0x1F;
  Data[262] = 7; Data[263] = 4; Data[265] = 1;
  le16 (Data + 266, 350); le16 (Data + 268, 370);
  le32 (Data + 272, 0x11223344); le32 (Data + 276, 0x55667788);
  Data[280] = 1; Data[295] = 0x80; Data[296] = 2;
  le32 (Data + 328, 7); Data[512] = 0x66; Data[513] = 0x44;
  le16 (Data + 514, 1024); le32 (Data + 516, 0x12345678);
  le16 (Data + 520, 0x01FF); le16 (Data + 522, 1);
  Data[524] = 3; Data[525] = 1; le16 (Data + 526, 31); le16 (Data + 528, 7);
  le32 (Data + 536, 0x87654321); memcpy (Data + 768, "nqn.test:controller", 19);
  assert (NvmeIdentifyParse (Data, 4096, &Info));
  assert (Info.Vid == 0x144D && Info.Ssvid == 0x1234 && Info.ControllerId == 0x1122);
  assert (strcmp (Info.Serial, "SERIAL?") == 0 && Info.Model[40] == 0 && Info.Firmware[4] == 0);
  assert (Info.Version == 0x10400 && Info.Mdts == 7 && Info.ControllerType == 1);
  assert (Info.IeeeOui[0] == 0xAA && Info.IeeeOui[2] == 0xCC);
  assert (Info.Rtd3ResumeUs == 100 && Info.Rtd3EntryUs == 200 && Info.Oaes == 0xABCD1234);
  assert (Info.ControllerAttributes == 0x12345678 && Info.Oacs == 0x03FF && Info.Oncs == 0x01FF);
  assert (Info.Frmw == 0x17 && Info.Npss == 4 && Info.Elpe == 7 && Info.Apsta == 1);
  assert (Info.WarningTemperatureK == 350 && Info.CriticalTemperatureK == 370);
  assert (Info.HostMemoryPreferred == 0x11223344 && Info.HostMemoryMinimum == 0x55667788);
  assert (Info.TotalNvmCapacity[0] == 1 && Info.TotalNvmCapacity[15] == 0x80 && Info.UnallocatedNvmCapacity[0] == 2);
  assert (Info.Sanicap == 7 && Info.Sqes == 0x66 && Info.Cqes == 0x44 && Info.MaxCommands == 1024);
  assert (Info.Namespaces == 0x12345678 && Info.Sgls == 0x87654321);
  assert (Info.Awun == 31 && Info.Awupf == 7 && Info.Fuses == 1 && Info.Fna == 3 && Info.Vwc == 1);
  assert (strcmp (Info.Subnqn, "nqn.test:controller") == 0);
  for (Test = 0; Test < 10000; Test++) {
    for (Index = 0; Index < sizeof (Data); Index++) {
      State = State * 1664525U + 1013904223U; Data[Index] = (UINT8)(State >> 24);
    }
    assert (NvmeIdentifyParse (Data, 4096, &Info));
    assert (Info.Serial[20] == 0 && Info.Model[40] == 0 && Info.Firmware[8] == 0 && Info.Subnqn[256] == 0);
  }
  puts ("PASS: Identify Controller offsets, LE decoding, safe text, 128-bit fields and 10000 random records.");
  return 0;
}
