/** @file
  Bounds-checked NVMe Identify Controller (CNS=1) decoder.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef NVME_IDENTIFY_DATA_H_
#define NVME_IDENTIFY_DATA_H_
#include <Base.h>

#define NVME_IDENTIFY_SIZE  4096
typedef struct {
  CHAR8 Serial[21];
  CHAR8 Model[41];
  CHAR8 Firmware[9];
  CHAR8 Subnqn[257];
  UINT16 Vid;
  UINT16 Ssvid;
  UINT16 ControllerId;
  UINT32 Version;
  UINT8 IeeeOui[3];
  UINT8 Cmic;
  UINT8 Mdts;
  UINT8 ControllerType;
  UINT32 Rtd3ResumeUs;
  UINT32 Rtd3EntryUs;
  UINT32 Oaes;
  UINT32 ControllerAttributes;
  UINT16 Oacs;
  UINT8 Acl;
  UINT8 Aerl;
  UINT8 Frmw;
  UINT8 Lpa;
  UINT8 Elpe;
  UINT8 Npss;
  UINT8 Apsta;
  UINT16 WarningTemperatureK;
  UINT16 CriticalTemperatureK;
  UINT32 HostMemoryPreferred;
  UINT32 HostMemoryMinimum;
  UINT8 TotalNvmCapacity[16];
  UINT8 UnallocatedNvmCapacity[16];
  UINT32 Sanicap;
  UINT8 Sqes;
  UINT8 Cqes;
  UINT16 MaxCommands;
  UINT32 Namespaces;
  UINT16 Oncs;
  UINT16 Fuses;
  UINT8 Fna;
  UINT8 Vwc;
  UINT16 Awun;
  UINT16 Awupf;
  UINT32 Sgls;
} NVME_IDENTIFY_INFO;

BOOLEAN NvmeIdentifyParse (IN CONST UINT8 *Data, IN UINTN Size, OUT NVME_IDENTIFY_INFO *Info);
#endif
