/** @file
  Read-only, bounds-checked TCG Level 0 Discovery parser.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef OPAL_DISCOVERY_H_
#define OPAL_DISCOVERY_H_

#include <Base.h>

typedef struct {
  BOOLEAN  Opal1;
  BOOLEAN  Opal2;
  BOOLEAN  Opalite;
  BOOLEAN  Pyrite1;
  BOOLEAN  Pyrite2;
  BOOLEAN  HasLocking;
  UINT8    LockingFlags;
  UINTN    FeatureCount;
} OPAL_DISCOVERY_INFO;

UINT32
DiscoveryLength (
  IN CONST UINT8  *Buffer
  );

BOOLEAN
ParseDiscovery (
  IN CONST UINT8           *Buffer,
  IN UINTN                 BufferSize,
  OUT OPAL_DISCOVERY_INFO  *Info
  );

#endif
