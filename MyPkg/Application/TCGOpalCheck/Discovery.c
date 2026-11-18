/** @file
  Parse device-supplied bytes without casting unaligned protocol structures.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "Discovery.h"
#include <IndustryStandard/TcgStorageCore.h>

UINT32
DiscoveryLength (
  IN CONST UINT8  *Buffer
  )
{
  return ((UINT32)Buffer[0] << 24) | ((UINT32)Buffer[1] << 16) |
         ((UINT32)Buffer[2] << 8) | Buffer[3];
}

BOOLEAN
ParseDiscovery (
  IN CONST UINT8           *Buffer,
  IN UINTN                 BufferSize,
  OUT OPAL_DISCOVERY_INFO  *Info
  )
{
  OPAL_DISCOVERY_INFO  Parsed = { 0 };
  UINTN                End;
  UINTN                Offset;
  UINTN                Length;
  UINT16               Code;

  if ((Buffer == NULL) || (Info == NULL) ||
      (BufferSize < sizeof (TCG_LEVEL0_DISCOVERY_HEADER)))
  {
    return FALSE;
  }

  // Length excludes its own four bytes. Check subtraction before addition.
  Length = DiscoveryLength (Buffer);
  if ((Length < sizeof (TCG_LEVEL0_DISCOVERY_HEADER) - 4) ||
      (Length > BufferSize - 4))
  {
    return FALSE;
  }

  End = Length + 4;
  for (Offset = sizeof (TCG_LEVEL0_DISCOVERY_HEADER); Offset < End;
       Offset += 4 + Length)
  {
    if (End - Offset < 4) {
      return FALSE;
    }

    Code   = (UINT16)(((UINT16)Buffer[Offset] << 8) | Buffer[Offset + 1]);
    Length = Buffer[Offset + 3];
    if (Length > End - Offset - 4) {
      return FALSE;
    }

    Parsed.FeatureCount++;
    switch (Code) {
      case TCG_FEATURE_LOCKING:
        if ((Length < 12) || Parsed.HasLocking) {
          return FALSE;
        }

        Parsed.HasLocking   = TRUE;
        Parsed.LockingFlags = Buffer[Offset + 4];
        break;
      case TCG_FEATURE_OPAL_SSC_V1_0_0:
      case TCG_FEATURE_OPAL_SSC_V2_0_0:
      case TCG_FEATURE_OPAL_SSC_LITE:
      case TCG_FEATURE_PYRITE_SSC:
      case TCG_FEATURE_PYRITE_SSC_V2_0_0:
        if (Length < 16) {
          return FALSE;
        }

        if (Code == TCG_FEATURE_OPAL_SSC_V1_0_0) {
          Parsed.Opal1 = TRUE;
        } else if (Code == TCG_FEATURE_OPAL_SSC_V2_0_0) {
          Parsed.Opal2 = TRUE;
        } else if (Code == TCG_FEATURE_OPAL_SSC_LITE) {
          Parsed.Opalite = TRUE;
        } else if (Code == TCG_FEATURE_PYRITE_SSC) {
          Parsed.Pyrite1 = TRUE;
        } else {
          Parsed.Pyrite2 = TRUE;
        }

        break;
      default:
        // Unknown feature codes are extensible; skip only after bounds checks.
        break;
    }
  }

  *Info = Parsed;
  return TRUE;
}
