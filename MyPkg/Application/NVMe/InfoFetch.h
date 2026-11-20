/** @file
**/
#ifndef NVME_INFO_FETCH_H_
#define NVME_INFO_FETCH_H_

#include <Uefi.h>
#include <Base.h>

#define NVME_INFO_ABILITY_FLAG_BIT_LEVEL_0_DISCOVERY BIT0
#define NVME_INFO_ABILITY_FLAG_BIT_ATA_PASSTHRU      BIT1 // EF ATA password protocol

#define NVME_INFO_SEC_FEATURE_OPAL_SSC               BIT0
#define NVME_INFO_SEC_FEATURE_ATA_SECURITY           BIT1

#define NVME_INFO_ENABLED_OPAL_SSC                   BIT0
#define NVME_INFO_ENABLED_ATA_SECURITY               BIT1

#define NVME_INFO_ARMED_OPAL_SSC                     BIT0
#define NVME_INFO_ARMED_ATA_SECURITY                 BIT1

#define NVME_INFO_SSC_VERSION_FLAG_V1                BIT1
#define NVME_INFO_SSC_VERSION_FLAG_V2                BIT2

typedef struct {
  UINT8   Ok;
  CHAR8   Serial[21];
  CHAR8   Model[41];
  CHAR8   Firmware[9];
  BOOLEAN SupportSecurityRecvSend;
  UINT8   Ability;          // bit0: LV0_DISCOVERY, bit1: ATA Passthru
  UINT16  SecurityFeature;  // bit0: TCG OPAL SSC, bit1: ATA Security
  UINT8   IsEnabled;        // bit0: TCG LockingEnabled, bit1: ATA Security Enabled
  UINT8   IsLocked;         // bit0: TCG Locked, bit1: ATA Security Locked

  // OPAL SEC specified info
  UINT8   OpalSSCVersionFlag;

  // ATA Security specified
  BOOLEAN AtaFrozen;

  // OPAL Encryption need a LBA range for lock/unlock
  UINT64  LBA;
} NVME_DRIVE_INFO;

// Read-only. On failure Ok=0; do not interpret partial security state as valid.
EFI_STATUS NVMeGetControllerInfo(OUT NVME_DRIVE_INFO *Info, IN EFI_HANDLE Handle);

#endif // NVME_INFO_FETCH_H_
