/** @file
**/

#include <Uefi.h>
#include <IndustryStandard/Nvme.h>
#include <IndustryStandard/TcgStorageCore.h>
#include <IndustryStandard/TcgStorageOpal.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "../Util/ValueAccess.h"
#include "Uefi/UefiBaseType.h"
#include "InfoFetch.h"


// per 100ns -> 5s
#define NVME_COMMAND_TIMEOUT   (5ULL * 1000 * 1000 * 10)
#define NVME_TRANSFER_SIZE_MAX 4096

/***** NVMe command passthru *****/
STATIC
EFI_STATUS
NVMeFetchIdentifyInfo (OUT UINT8 *Buffer, IN EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme)
{
  EFI_NVM_EXPRESS_COMMAND                  Command    = {0};
  EFI_NVM_EXPRESS_COMPLETION               Completion = {0};
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET Packet     = {0};
  EFI_STATUS                               Status     = EFI_SUCCESS;
  NVME_CQ                                  Cq         = {0};

  STATIC_ASSERT (sizeof (Cq) == sizeof (Completion), "NVMe CQ size not matched");

  if (Buffer == NULL || Nvme == NULL || Nvme->PassThru == NULL) { return EFI_INVALID_PARAMETER; }

  ZeroMem (&Command, sizeof (Command));
  ZeroMem (&Completion, sizeof (Completion));
  ZeroMem (&Packet, sizeof (Packet));
  ZeroMem (Buffer, NVME_TRANSFER_SIZE_MAX);

  // https://metebalci.com/blog/a-quick-tour-of-nvm-express-nvme/ -> NVMe Admin Commands
  Command.Cdw0.Opcode = NVME_ADMIN_IDENTIFY_CMD;
  Command.Nsid        = 0;
  Command.Cdw10       = 1;                          // Identity Controller
  Command.Flags       = CDW10_VALID;

  Packet.QueueType      = NVME_ADMIN_QUEUE;
  Packet.CommandTimeout = NVME_COMMAND_TIMEOUT;  // per 100ns -> 5s
  Packet.NvmeCmd        = &Command;
  Packet.NvmeCompletion = &Completion;
  Packet.TransferBuffer = Buffer;
  Packet.TransferLength = NVME_TRANSFER_SIZE_MAX;
  
  Status = Nvme->PassThru (Nvme, 0, &Packet, NULL);
  if (EFI_ERROR (Status)) {
    Print (L"PassThru failed: %r\r\n", Status);
    return Status;
  }
  if (Status != EFI_SUCCESS) { return EFI_DEVICE_ERROR; }

  // Sct -> Status code type, Sc -> Status code
  CopyMem (&Cq, &Completion, sizeof (Cq));
  if (Cq.Sct != 0 || Cq.Sc != 0) {
    Print (L"Command failed: SCT=%u SC=0x%02x\r\n", (UINT32)Cq.Sct, (UINT32)Cq.Sc);
    return EFI_DEVICE_ERROR;
  }

  if (Packet.TransferLength != NVME_TRANSFER_SIZE_MAX) {
    Print (L"Unexpected Identify length: %u / %u bytes\r\n",
          Packet.TransferLength, (UINT32)NVME_TRANSFER_SIZE_MAX);
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
NVMeSecurityReceive (
  IN     EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme,
  IN     UINT8                              SecurityProtocol,
  IN     UINT16                             ProtocolSpecific,
  OUT    VOID                              *Buffer,
  IN OUT UINT32                            *Length
  )
{
  EFI_NVM_EXPRESS_COMMAND                  Command    = { 0 };
  EFI_NVM_EXPRESS_COMPLETION               Completion = { 0 };
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET Packet     = { 0 };
  EFI_STATUS                               Status     = EFI_SUCCESS;
  UINT32                                   Requested  = 0;
  NVME_CQ                                  Cq         = {0};

  if (Length == NULL) { return EFI_INVALID_PARAMETER; }
  Requested = *Length;
  *Length = 0;
  if (Nvme == NULL || Nvme->PassThru == NULL || Buffer == NULL ||
      Requested == 0 || Requested > NVME_TRANSFER_SIZE_MAX) { return EFI_INVALID_PARAMETER; }
  ZeroMem (Buffer, Requested);

  Command.Cdw0.Opcode = NVME_ADMIN_SECURITY_RECEIVE_CMD;
  Command.Flags       = CDW10_VALID | CDW11_VALID;
  Command.Cdw10       = ((UINT32)SecurityProtocol << 24) |
                        ((UINT32)ProtocolSpecific << 8);
  Command.Cdw11       = Requested;

  Packet.QueueType      = NVME_ADMIN_QUEUE;
  Packet.CommandTimeout = NVME_COMMAND_TIMEOUT;
  Packet.NvmeCmd        = &Command;
  Packet.NvmeCompletion = &Completion;
  Packet.TransferBuffer = Buffer;
  Packet.TransferLength = Requested;

  Status = Nvme->PassThru (Nvme, 0, &Packet, NULL);
  if (EFI_ERROR (Status)) { return Status; }
  if (Status != EFI_SUCCESS) { return EFI_DEVICE_ERROR; }

  CopyMem (&Cq, &Completion, sizeof (Cq));
  if (Cq.Sct != 0 || Cq.Sc != 0) {
    Print (L"Security Receive failed: SCT=%u SC=0x%02x\r\n", (UINT32)Cq.Sct, (UINT32)Cq.Sc);
    return EFI_DEVICE_ERROR;
  }
  if (Packet.TransferLength == 0 || Packet.TransferLength > Requested) {
    return EFI_COMPROMISED_DATA;
  }
  *Length = Packet.TransferLength;
  return EFI_SUCCESS;
}

/***** NVMe packet buffer parser *****/
STATIC
BOOLEAN
NVMeIdentifyParse (OUT NVME_DRIVE_INFO *Info, IN CONST UINT8 *Buffer, IN UINTN Size)
{
  NVME_DRIVE_INFO            Parsed   = {0};
  NVME_ADMIN_CONTROLLER_DATA Identify = {0};

  if (Buffer == NULL || Info == NULL || Size < sizeof (Identify)) { return FALSE; }

  CopyMem (&Identify, Buffer, sizeof (Identify));
  GetText (Parsed.Serial, Identify.Sn, 20);
  GetText (Parsed.Model, Identify.Mn, 40);
  GetText (Parsed.Firmware, Identify.Fr, 8);
  Parsed.SupportSecurityRecvSend = (Identify.Oacs & BIT0) != 0;

  Parsed.Ok |= BIT0;
  *Info = Parsed;

  return TRUE;
}

STATIC
BOOLEAN
NVMeTCGSecurityInfoParse (IN OUT NVME_DRIVE_INFO *Info, IN CONST UINT8 *Buffer, IN UINT32 Size)
{
  UINT16 Count = 0;
  UINT16 Index = 0;
  UINT8 Ability = 0;
  
  if (Buffer == NULL || Info == NULL || Size < 8) { return FALSE; }

  // The actual format is TCG_SUPPORTED_SECURITY_PROTOCOLS
  // [0..5] - Reserved
  // [6, 7] - Len_BE
  // [8...] - Data
  Count = GetBe16(Buffer + 6);
  
  if ((UINT32)Count > Size - 8) { return FALSE; }
  for (Index = 0; Index < Count; ++Index) {
    switch (*(Buffer + 8 + Index)) {
      case TCG_OPAL_SECURITY_PROTOCOL_1:
        Ability |= NVME_INFO_ABILITY_FLAG_BIT_LEVEL_0_DISCOVERY;
        break;
      case TCG_SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASS:
        Ability |= NVME_INFO_ABILITY_FLAG_BIT_ATA_PASSTHRU;
        break;
      default:
        break;
    }
  }

  Info->Ok |= BIT1;
  Info->Ability = Ability;
  return TRUE;
}

STATIC
BOOLEAN
NVMeOPALDiscoveryParse (IN OUT NVME_DRIVE_INFO *Info, IN CONST UINT8 *Buffer, IN UINT32 Size)
{
  UINT32 ParameterSize  = 0;
  UINT32 End;
  NVME_DRIVE_INFO Parsed;

  UINT16           Code = 0;
  UINT8   DescriptorLen = 0;

  UINT32     CurrentPos = 0;
  
  if (Buffer == NULL || Info == NULL || Size < sizeof (TCG_LEVEL0_DISCOVERY_HEADER)) { return FALSE; }
  Parsed = *Info;
  Parsed.SecurityFeature    &= (UINT16)~NVME_INFO_SEC_FEATURE_OPAL_SSC;
  Parsed.IsEnabled          &= (UINT8)~NVME_INFO_ENABLED_OPAL_SSC;
  Parsed.IsLocked           &= (UINT8)~NVME_INFO_ARMED_OPAL_SSC;
  Parsed.OpalSSCVersionFlag  = 0;

  // https://medias.cdn.trustedcomputinggroup.org/production/TCG_Storage_Opal_SSC_v2_30_pub_f4e29a2c4b.pdf
  // Basically, header 0x30 -> [DescriptorA][DescriptorB]...
  ParameterSize = GetBe32(Buffer);
  
  // Discovery length excludes its 4-byte length field. Each descriptor length
  // independently excludes its own 4-byte feature header.
  if ((Size - 4 < ParameterSize) || (sizeof (TCG_LEVEL0_DISCOVERY_HEADER) - 4 > ParameterSize)) { return FALSE; }
  End = ParameterSize + 4;

  for (CurrentPos = sizeof (TCG_LEVEL0_DISCOVERY_HEADER); CurrentPos < End; CurrentPos += DescriptorLen + 4) {
    if (End - CurrentPos < 4) { return FALSE; }

    // > TCG_LEVEL0_FEATURE_DESCRIPTOR_HEADER
    Code = GetBe16(Buffer + CurrentPos);
    DescriptorLen = Buffer[CurrentPos + 3];
    if (DescriptorLen > End - CurrentPos - 4) { return FALSE; }

    switch (Code) {
      case TCG_FEATURE_LOCKING:
        if (DescriptorLen < sizeof (TCG_LOCKING_FEATURE_DESCRIPTOR) - 4) {
          return FALSE;
        }

        // Check MediaEncryption and Locking Support, pdf said SSC compliant 
        // device should have both of them set to 1
        if ((Buffer[CurrentPos + 4] & BIT0) && (Buffer[CurrentPos + 4] & BIT3)) {
          Parsed.SecurityFeature |= NVME_INFO_SEC_FEATURE_OPAL_SSC;
        }

        if (Buffer[CurrentPos + 4] & BIT1) {
          Parsed.IsEnabled |= NVME_INFO_ENABLED_OPAL_SSC;
        }

        if (Buffer[CurrentPos + 4] & BIT2) {
          Parsed.IsLocked |= NVME_INFO_ARMED_OPAL_SSC;
        }
        break;
        
      // We're gonna use SSC V1 and V2 only
      case TCG_FEATURE_OPAL_SSC_V1_0_0:
        if (DescriptorLen < sizeof (OPAL_SSCV1_FEATURE_DESCRIPTOR) - 4) {
          return FALSE;
        }
        
        Parsed.OpalSSCVersionFlag |= NVME_INFO_SSC_VERSION_FLAG_V1;

        break;
      case TCG_FEATURE_OPAL_SSC_V2_0_0:
        if (DescriptorLen < sizeof (OPAL_SSCV2_FEATURE_DESCRIPTOR) - 4) {
          return FALSE;
        }
        
        Parsed.OpalSSCVersionFlag |= NVME_INFO_SSC_VERSION_FLAG_V2;

        break;
      default:
        break;
    }

  }

  Parsed.Ok |= BIT2;
  *Info = Parsed;
  return TRUE;
}

STATIC
BOOLEAN
NVMeATASecurityParse (IN OUT NVME_DRIVE_INFO *Info, IN CONST UINT8 *Buffer, IN UINT32 Size)
{
  NVME_DRIVE_INFO Parsed;
  UINT8 Flag;
  
  if (Buffer == NULL || Info == NULL || Size != 16 || GetBe16 (Buffer) != 14) { return FALSE; }
  Parsed = *Info;
  Parsed.SecurityFeature &= (UINT16)~NVME_INFO_SEC_FEATURE_ATA_SECURITY;
  Parsed.IsEnabled       &= (UINT8)~NVME_INFO_ENABLED_ATA_SECURITY;
  Parsed.IsLocked        &= (UINT8)~NVME_INFO_ARMED_ATA_SECURITY;
  Parsed.AtaFrozen        = FALSE;

  // https://www.t10.org/ftp/t10/document.08/08-075r0.pdf
  /*
    [0..1] - Big-endian length, 14 (16 bytes including length field)
    [2..3] - Security Erase Time
    [4..5] - Enhanced Security Erase Time
    [6..7] - Master Password Identifier
    [8] BIT0 - MAXSET
    [9] BIT0 - S_SUPRT
        BIT1 - S_ENABLD
        BIT2 - LOCKED
        BIT3 - FROZEN
        BIT4 - PWCNTEX
        BIT5 - EN_ER_SUP
  */
  Flag = Buffer[9];

  if (Flag & BIT0) {
    Parsed.SecurityFeature |= NVME_INFO_SEC_FEATURE_ATA_SECURITY;
    if (Flag & BIT1) { Parsed.IsEnabled |= NVME_INFO_ENABLED_ATA_SECURITY; }
    if (Flag & BIT2) { Parsed.IsLocked |= NVME_INFO_ARMED_ATA_SECURITY; }
    Parsed.AtaFrozen = (Flag & BIT3) != 0;
  }

  Parsed.Ok |= BIT3;
  *Info = Parsed;
  return TRUE;
}

/***** Main *****/
EFI_STATUS
NVMeGetControllerInfo(OUT NVME_DRIVE_INFO *Info, IN EFI_HANDLE Handle)
{
  EFI_STATUS                         Status    = EFI_SUCCESS;
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *Nvme     = NULL;
  UINTN                              Alignment = 0;
  UINTN                              Pages     = 0;
  UINT8                              *Buffer   = NULL;
  UINT32                             Length    = 0;
  EFI_STATUS                         FirstError = EFI_SUCCESS;

  if (Info == NULL) { return EFI_INVALID_PARAMETER; }
  ZeroMem (Info, sizeof (*Info));
  if (Handle == NULL) { return EFI_INVALID_PARAMETER; }

  Status = gBS->HandleProtocol (Handle, &gEfiNvmExpressPassThruProtocolGuid, (VOID **)&Nvme);
  if (EFI_ERROR (Status)) { return Status; }
  if (Nvme->Mode == NULL || Nvme->PassThru == NULL) { return EFI_UNSUPPORTED; }

  Pages = EFI_SIZE_TO_PAGES(NVME_TRANSFER_SIZE_MAX);

  Alignment = Nvme->Mode->IoAlign;
  if (Alignment > 1 && (Alignment & (Alignment - 1)) != 0) {
    return EFI_UNSUPPORTED;
  }
  if (Alignment < EFI_PAGE_SIZE) {
    Alignment = EFI_PAGE_SIZE;
  }

  Buffer = AllocateAlignedPages (Pages, Alignment);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  // Identify Controller
  Status = NVMeFetchIdentifyInfo (Buffer, Nvme);
  if (!EFI_ERROR (Status) && !NVMeIdentifyParse (Info, Buffer, NVME_TRANSFER_SIZE_MAX)) {
    Status = EFI_COMPROMISED_DATA;
  }
  if (EFI_ERROR (Status)) { goto Done; }
  if (!Info->SupportSecurityRecvSend) { goto Done; }

  // Supported Security Protocols
  Length = 512;
  Status = NVMeSecurityReceive (Nvme, TCG_SECURITY_PROTOCOL_INFO, 0x0000, Buffer, &Length);
  if (!EFI_ERROR (Status) && !NVMeTCGSecurityInfoParse (Info, Buffer, Length)) {
    Status = EFI_COMPROMISED_DATA;
  }
  if (EFI_ERROR (Status)) { goto Done; }

  // SSC Check
  if (Info->Ability & NVME_INFO_ABILITY_FLAG_BIT_LEVEL_0_DISCOVERY) {
    Length = NVME_TRANSFER_SIZE_MAX;
    Status = NVMeSecurityReceive (Nvme, TCG_OPAL_SECURITY_PROTOCOL_1, 0x0001, Buffer, &Length);
    if (!EFI_ERROR (Status) && !NVMeOPALDiscoveryParse (Info, Buffer, Length)) {
      Status = EFI_COMPROMISED_DATA;
    }
    if (EFI_ERROR (Status)) {
      Print (L"OPAL Discovery failed: %r\r\n", Status);
      FirstError = Status;
    }
  }

  // ATA Security Check (still attempted if OPAL failed)
  if (Info->Ability & NVME_INFO_ABILITY_FLAG_BIT_ATA_PASSTHRU) {
    Length = 16;
    Status = NVMeSecurityReceive (Nvme, TCG_SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASS, 0x0000, Buffer, &Length);
    if (!EFI_ERROR (Status) && !NVMeATASecurityParse (Info, Buffer, Length)) {
      Status = EFI_COMPROMISED_DATA;
    }
    if (EFI_ERROR (Status)) {
      Print (L"ATA Security query failed: %r\r\n", Status);
      if (!EFI_ERROR (FirstError)) { FirstError = Status; }
    }
  }
  Status = FirstError;

Done:
  if (EFI_ERROR (Status)) { Info->Ok = 0; }
  FreeAlignedPages (Buffer, Pages);
  return Status;
}
