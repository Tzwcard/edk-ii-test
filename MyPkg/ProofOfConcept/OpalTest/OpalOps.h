/** @file
  Explicit OPAL test operations. All mutating calls require AllowWrites.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef OPAL_TEST_OPS_H_
#define OPAL_TEST_OPS_H_
#include <Uefi.h>
#include <Library/TcgStorageOpalLib.h>

#define OPAL_TEST_PIN_MAX 32
#define OPAL_PBA_IMAGE_SIZE (8U * 1024U * 1024U)
#define OPAL_PBA_CHUNK_SIZE 384U

typedef struct {
  OPAL_SESSION Session;
  BOOLEAN AllowWrites;
  BOOLEAN AllowErase;
} OPAL_TEST_DEVICE;

typedef struct {
  BOOLEAN ReadLockEnabled;
  BOOLEAN WriteLockEnabled;
  BOOLEAN ReadLocked;
  BOOLEAN WriteLocked;
} OPAL_TEST_RANGE;

VOID OpalTestWipe (IN OUT VOID *Data, IN UINTN Size);
EFI_STATUS OpalTestVerify (IN OUT OPAL_TEST_DEVICE *Device, IN TCG_UID Authority, IN CONST UINT8 *Pin, IN UINT32 PinSize);
EFI_STATUS OpalTestSetPin (IN OUT OPAL_TEST_DEVICE *Device, IN TCG_UID Authority,
  IN CONST UINT8 *OldPin, IN UINT32 OldSize, IN CONST UINT8 *NewPin, IN UINT32 NewSize);
EFI_STATUS OpalTestActivate (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Sid, IN UINT32 Size);
EFI_STATUS OpalTestSetUser1 (IN OUT OPAL_TEST_DEVICE *Device,
  IN CONST UINT8 *Admin, IN UINT32 AdminSize, IN CONST UINT8 *NewPin, IN UINT32 NewSize);
EFI_STATUS OpalTestDisableUser1 (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size);
EFI_STATUS OpalTestGetGlobal (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size, OUT OPAL_TEST_RANGE *Range);
// Admin1 only: explicitly configure both enable bits and both lock bits.
EFI_STATUS OpalTestSetGlobal (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size,
  IN BOOLEAN Enabled, IN BOOLEAN Locked);
// User1 only: update lock bits, never enable flags or range geometry.
EFI_STATUS OpalTestUserGlobal (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Pin, IN UINT32 Size, IN BOOLEAN Locked);
// Admin1 only: set MBRControl.Done=TRUE, without changing Enable or reset policy.
EFI_STATUS OpalTestMbrDone (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size);
// Replace bytes at offset zero of the MBR BYTE TABLE, not namespace LBAs.
// Stops on the first failure; existing Shadow MBR may then be partially replaced.
EFI_STATUS OpalTestLoadPba (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Admin, IN UINT32 Size,
  IN CONST UINT8 *Image, IN UINT32 ImageSize);
// Admin SP Revert: destructive. Requires BOTH AllowWrites and AllowErase.
EFI_STATUS OpalTestRevert (IN OUT OPAL_TEST_DEVICE *Device, IN CONST UINT8 *Sid, IN UINT32 Size);
#endif
