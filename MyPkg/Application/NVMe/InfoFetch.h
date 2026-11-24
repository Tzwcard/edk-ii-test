/** @file
**/
#ifndef NVME_INFO_FETCH_H_
#define NVME_INFO_FETCH_H_

#include <Uefi.h>
#include <Base.h>

// Abilities
#define NVME_INFO_ABILITY_FLAG_BIT_LEVEL_0_DISCOVERY BIT0
#define NVME_INFO_ABILITY_FLAG_BIT_ATA_PASSTHRU      BIT1 // EF ATA password protocol

// Features
#define NVME_INFO_SEC_FEATURE_TCG_LOCKING            BIT0
#define NVME_INFO_SEC_FEATURE_ATA_SECURITY           BIT1
#define NVME_INFO_SEC_FEATURE_TCG_MEDIA_ENC          BIT2

// Security method enabled
#define NVME_INFO_ENABLED_TCG_SECURIY                BIT0
#define NVME_INFO_ENABLED_ATA_SECURITY               BIT1

// Security armed (Locked)
#define NVME_INFO_ARMED_TCG_SECURITY                 BIT0
#define NVME_INFO_ARMED_ATA_SECURITY                 BIT1

// OPAL: Protocol version
#define NVME_INFO_OPAL_SSC_VERSION_FLAG_V1           BIT1
#define NVME_INFO_OPAL_SSC_VERSION_FLAG_V2           BIT2

// PYRITE: Protocol version
#define NVME_INFO_PYRITE_SSC_VERSION_FLAG_V1         BIT1
#define NVME_INFO_PYRITE_SSC_VERSION_FLAG_V2         BIT2

typedef struct {
  UINT8   Ok;
  CHAR8   Serial[21];
  CHAR8   Model[41];
  CHAR8   Firmware[9];
  BOOLEAN SupportSecurityRecvSend;
  UINT8   Ability;          // See above: Abilities
  UINT16  SecurityFeature;  // See above: Features

  // Internally, the flag below is for all methods
  // (TCG SSC and ATA Security)
  // Use Opal over Pyrite if both available
  // Then if none of them usable use ATA Security
  UINT8   IsEnabled;        // See above: Security method enabled
  UINT8   IsLocked;         // See above: Security armed

  // OPAL SEC specified info
  UINT8   OpalSSCVersionFlag;

  // PYRITE SSC specified info
  UINT8   PyriteSSCVersionFlag;

  // ATA Security specified
  BOOLEAN AtaFrozen;

  // OPAL/PYRITE have MBR Shadowing functionality in spec
  // We can utilize it to show some error message if unlock/decrypt error
  BOOLEAN MBRShadowAvailable;

} NVME_DRIVE_INFO;

// Read-only. On failure Ok=0; do not interpret partial security state as valid.
EFI_STATUS NVMeGetControllerInfo(OUT NVME_DRIVE_INFO *Info, IN EFI_HANDLE Handle);

#endif // NVME_INFO_FETCH_H_
