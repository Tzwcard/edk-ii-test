/** @file
  Read-only OPAL detection and ATA password status via firmware storage security.
  No SendData, authentication, activation, unlock, reset, or revert commands.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/StorageSecurityCommand.h>
#include <IndustryStandard/TcgStorageCore.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "Discovery.h"
#include "AtaSecurity.h"
#include "SecurityCheck.h"

#define DISCOVERY_INITIAL_SIZE  4096
#define DISCOVERY_MAX_SIZE      65536
#define DISCOVERY_TIMEOUT       (5ULL * 1000 * 1000 * 10) // 100ns units

STATIC
CONST CHAR16 *
YesNo (
  IN BOOLEAN  Value
  )
{
  return Value ? L"Yes" : L"No";
}

STATIC
EFI_STATUS
QuerySecurityProtocols (
  IN EFI_STORAGE_SECURITY_COMMAND_PROTOCOL  *Security,
  IN EFI_BLOCK_IO_MEDIA                     *Media,
  OUT BOOLEAN                              *HasTcg,
  OUT BOOLEAN                              *HasAtaPassword
  )
{
  EFI_STATUS  Status;
  UINT8       *Buffer;
  UINTN       Alignment;
  UINTN       Transferred;
  UINTN       Index;
  UINT16      ListLength;

  *HasTcg         = FALSE;
  *HasAtaPassword = FALSE;

  Alignment = MAX ((UINTN)Media->IoAlign, (UINTN)EFI_PAGE_SIZE);
  if ((Alignment & (Alignment - 1)) != 0) {
    return EFI_UNSUPPORTED;
  }

  Buffer = AllocateAlignedPages (1, Alignment);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Buffer, EFI_PAGE_SIZE);
  Transferred = 0;

  Status = Security->ReceiveData (
                       Security,
                       Media->MediaId,
                       DISCOVERY_TIMEOUT,
                       0x00,  // Security Protocol Information
                       0x0000,
                       512,
                       Buffer,
                       &Transferred
                       );

  Print (
    L"  Protocol-list query: %r, transferred=%u\r\n",
    Status,
    (UINT32)Transferred
    );

  if (Status != EFI_SUCCESS) {
    FreeAlignedPages (Buffer, 1);
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }

  if ((Transferred < 8) || (Transferred > 512)) {
    FreeAlignedPages (Buffer, 1);
    return EFI_COMPROMISED_DATA;
  }

  // Bytes 6–7: big-endian list length.
  // Byte 8 onward: one byte per supported Security Protocol ID.
  ListLength = (UINT16)(((UINT16)Buffer[6] << 8) | Buffer[7]);

  if (ListLength > Transferred - 8) {
    Print (L"  Protocol list is truncated; support remains unknown.\r\n");
    FreeAlignedPages (Buffer, 1);
    return EFI_BUFFER_TOO_SMALL;
  }

  Print (L"  Supported security protocol IDs:");

  for (Index = 0; Index < ListLength; Index++) {
    Print (L" %02x", (UINT32)Buffer[8 + Index]);

    if (Buffer[8 + Index] == TCG_OPAL_SECURITY_PROTOCOL_1) {
      *HasTcg = TRUE;
    }

    if (Buffer[8 + Index] == TCG_SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASS) {
      *HasAtaPassword = TRUE;
    }
  }

  Print (L"\r\n");
  Print (
    L"  TCG protocol 0x01 advertised: %s\r\n",
    YesNo (*HasTcg)
    );
  Print (L"  ATA password protocol 0xEF advertised: %s\r\n", YesNo (*HasAtaPassword));

  FreeAlignedPages (Buffer, 1);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
QueryAtaPasswordStatus (
  IN EFI_STORAGE_SECURITY_COMMAND_PROTOCOL  *Security,
  IN EFI_BLOCK_IO_MEDIA                     *Media,
  OUT SECURITY_CHECK_RESULT                 *Result
  )
{
  EFI_STATUS           Status;
  UINT8                *Buffer;
  UINTN                Alignment;
  UINTN                Transferred;
  UINTN                Index;
  ATA_PASSWORD_STATUS  Info;

  Alignment = MAX ((UINTN)Media->IoAlign, (UINTN)EFI_PAGE_SIZE);
  if ((Alignment & (Alignment - 1)) != 0) {
    return EFI_UNSUPPORTED;
  }

  Buffer = AllocateAlignedPages (1, Alignment);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Buffer, EFI_PAGE_SIZE);
  Transferred = 0;
  // SAT specifies a 16-byte status read. No SECURITY PROTOCOL OUT is issued.
  Status = Security->ReceiveData (
                       Security,
                       Media->MediaId,
                       DISCOVERY_TIMEOUT,
                       TCG_SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASS,
                       0x0000,
                       16,
                       Buffer,
                       &Transferred
                       );
  Print (L"  EF status query: %r, transferred=%u\r\n", Status, (UINT32)Transferred);
  if (Status != EFI_SUCCESS) {
    Print (L"  ATA password state: Unknown (status read failed).\r\n");
    FreeAlignedPages (Buffer, 1);
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }

  if (Transferred != 16) {
    Print (L"  ATA password state: Unknown (expected 16 bytes).\r\n");
    FreeAlignedPages (Buffer, 1);
    return EFI_COMPROMISED_DATA;
  }

  Print (L"  EF raw status (16 bytes):");
  for (Index = 0; Index < 16; Index++) {
    Print (L" %02x", (UINT32)Buffer[Index]);
  }

  Print (L"\r\n");
  if (!ParseAtaPasswordStatus (Buffer, Transferred, &Info)) {
    Print (L"  ATA password state: Unknown (unrecognized response length/layout).\r\n");
    FreeAlignedPages (Buffer, 1);
    return EFI_COMPROMISED_DATA;
  }

  FreeAlignedPages (Buffer, 1);
  Result->AtaValid = TRUE;
  Result->Ata = Info;
  Print (L"  ATA Security supported: %s\r\n", YesNo (Info.Supported));
  if (!Info.Supported) {
    Print (L"  Remaining ATA password fields are not interpreted (S_SUPRT=0).\r\n");
    return EFI_SUCCESS;
  }

  Print (L"  Password security enabled: %s\r\n", YesNo (Info.Enabled));
  Print (L"  Locked:                    %s\r\n", YesNo (Info.Locked));
  Print (L"  Frozen:                    %s\r\n", YesNo (Info.Frozen));
  Print (L"  Password attempts exceeded: %s\r\n", YesNo (Info.AttemptsExceeded));
  Print (L"  Enhanced erase supported:  %s (capability only; NOT executed)\r\n",
    YesNo (Info.EnhancedEraseSupported));
  Print (L"  Master password identifier: 0x%04x (identifier, NOT the password)\r\n",
    (UINT32)Info.MasterPasswordIdentifier);
  Print (L"  MAXSET: %s\r\n", YesNo (Info.MaximumSecurity));
  if (Info.Enabled) {
    Print (L"  Security level: %s\r\n", Info.MaximumSecurity ? L"Maximum" : L"High");
  } else {
    Print (L"  Security level: Not active (password security disabled).\r\n");
  }

  Print (L"  EF status does not prove encryption at rest.\r\n");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadDiscovery (
  IN EFI_STORAGE_SECURITY_COMMAND_PROTOCOL  *Security,
  IN EFI_BLOCK_IO_MEDIA                     *Media,
  OUT OPAL_DISCOVERY_INFO                   *Info
  )
{
  EFI_STATUS  Status;
  UINT8       *Buffer;
  UINTN       Size;
  UINTN       Pages;
  UINTN       Alignment;
  UINTN       Transferred;
  UINT32      Length;

  Alignment = MAX ((UINTN)Media->IoAlign, (UINTN)EFI_PAGE_SIZE);
  if ((Alignment & (Alignment - 1)) != 0) {
    return EFI_UNSUPPORTED;
  }

  Size = DISCOVERY_INITIAL_SIZE;
  for (;;) {
    Pages  = EFI_SIZE_TO_PAGES (Size);
    Buffer = AllocateAlignedPages (Pages, Alignment);
    if (Buffer == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    ZeroMem (Buffer, EFI_PAGES_TO_SIZE (Pages));
    Transferred = 0;
    // EDK II's OPAL transport passes the protocol-specific field in wire order.
    Status = Security->ReceiveData (
                         Security,
                         Media->MediaId,
                         DISCOVERY_TIMEOUT,
                         TCG_OPAL_SECURITY_PROTOCOL_1,
                         SwapBytes16 (TCG_SP_SPECIFIC_PROTOCOL_LEVEL0_DISCOVERY),
                         Size,
                         Buffer,
                         &Transferred
                         );
    if (EFI_ERROR (Status)) {
      FreeAlignedPages (Buffer, Pages);
      return Status;
    }

    if ((Transferred < sizeof (TCG_LEVEL0_DISCOVERY_HEADER)) ||
        (Transferred > Size))
    {
      FreeAlignedPages (Buffer, Pages);
      return EFI_COMPROMISED_DATA;
    }

    Length = DiscoveryLength (Buffer);
    if (Length > DISCOVERY_MAX_SIZE - 4) {
      FreeAlignedPages (Buffer, Pages);
      return EFI_BAD_BUFFER_SIZE;
    }

    if (Length + 4 > Size) {
      Size = ALIGN_VALUE ((UINTN)Length + 4, 512);
      FreeAlignedPages (Buffer, Pages);
      continue;
    }

    if ((Status != EFI_SUCCESS) || !ParseDiscovery (Buffer, Transferred, Info)) {
      Status = EFI_COMPROMISED_DATA;
    }

    FreeAlignedPages (Buffer, Pages);
    return Status;
  }
}

EFI_STATUS
CheckStorageSecurity (
  IN EFI_HANDLE Handle,
  OUT SECURITY_CHECK_RESULT *Result
  )
{
  EFI_STATUS                             Status;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_STORAGE_SECURITY_COMMAND_PROTOCOL  *Security;
  EFI_DEVICE_PATH_PROTOCOL               *Path;
  CHAR16                                 *Text;
  OPAL_DISCOVERY_INFO                     Info;
  EFI_STATUS                             ProtocolListStatus;
  EFI_STATUS                             AtaStatus;
  BOOLEAN                                HasTcg;
  BOOLEAN                                HasAtaPassword;

  if (Result == NULL) { return EFI_INVALID_PARAMETER; }
  ZeroMem (Result, sizeof (*Result));
  Status = gBS->HandleProtocol (Handle, &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
  if (EFI_ERROR (Status) || (BlockIo->Media == NULL) || BlockIo->Media->LogicalPartition) {
    return EFI_ERROR (Status) ? Status : EFI_UNSUPPORTED;
  }

  Status = gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Path);
  if (!EFI_ERROR (Status)) {
    Text = ConvertDevicePathToText (Path, FALSE, FALSE);
    if (Text != NULL) {
      Print (L"  Path: %s\r\n", Text);
      FreePool (Text);
    }
  }

  Status = gBS->HandleProtocol (
                  Handle, &gEfiStorageSecurityCommandProtocolGuid, (VOID **)&Security
                  );
  if (EFI_ERROR (Status)) {
    Print (L"  Storage security interface: Not exposed (%r)\r\n", Status);
    Print (L"  OPAL support: Unknown (not proof of unsupported hardware).\r\n");
    return Status;
  }

  Print (L"  Storage security interface: Available\r\n");
  ProtocolListStatus = QuerySecurityProtocols (Security, BlockIo->Media, &HasTcg, &HasAtaPassword);
  // EF and OPAL are independent. An OPAL error must not skip EF status.
  if (!EFI_ERROR (ProtocolListStatus) && HasAtaPassword) {
    AtaStatus = QueryAtaPasswordStatus (Security, BlockIo->Media, Result);
    if (EFI_ERROR (AtaStatus)) {
      Print (L"  EF query did not yield a usable state: %r\r\n", AtaStatus);
    }
  } else if (EFI_ERROR (ProtocolListStatus)) {
    Print (L"  EF status: Unknown; query skipped because protocol list is unavailable.\r\n");
  } else {
    Print (L"  EF status: Skipped (protocol 0xEF not advertised).\r\n");
  }

  // Preserve the existing 0x01 probe, even when absent from the list.
  Print (L"  OPAL Discovery query (protocol 0x01):\r\n");
  Status = ReadDiscovery (Security, BlockIo->Media, &Info);
  if (EFI_ERROR (Status)) {
    Print (L"  Discovery failed: %r\r\n", Status);
    if (!EFI_ERROR (ProtocolListStatus) && !HasTcg) {
      Print (L"  OPAL: Not available through current interface (0x01 not advertised).\r\n");
    } else {
      Print (L"  OPAL support: Unknown.\r\n");
    }
    return Status;
  }

  Result->DiscoveryValid = TRUE;
  Result->Discovery = Info;
  Print (L"  OPAL 1.x SSC advertised: %s\r\n", YesNo (Info.Opal1));
  Print (L"  OPAL 2.x SSC advertised: %s\r\n", YesNo (Info.Opal2));
  Print (L"  Opalite SSC advertised: %s\r\n", YesNo (Info.Opalite));
  Print (L"  Pyrite 1/2 SSC advertised: %s / %s (not OPAL)\r\n",
    YesNo (Info.Pyrite1), YesNo (Info.Pyrite2));
  if (!Info.Opal1 && !Info.Opal2) {
    Print (L"  No OPAL 1.x/2.x descriptor in the complete discovery response.\r\n");
  }

  if (Info.HasLocking) {
    Print (L"  LockingSupported: %s\r\n", YesNo ((Info.LockingFlags & BIT0) != 0));
    Print (L"  LockingEnabled:   %s\r\n", YesNo ((Info.LockingFlags & BIT1) != 0));
    Print (L"  Locked:           %s\r\n", YesNo ((Info.LockingFlags & BIT2) != 0));
    Print (L"  MediaEncryption:  %s\r\n", YesNo ((Info.LockingFlags & BIT3) != 0));
    Print (L"  MBREnabled:       %s\r\n", YesNo ((Info.LockingFlags & BIT4) != 0));
    Print (L"  MBRDone:          %s\r\n", YesNo ((Info.LockingFlags & BIT5) != 0));
  } else {
    Print (L"  Locking state: Unknown (no Locking feature descriptor).\r\n");
  }
  return EFI_SUCCESS;
}
