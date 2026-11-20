/** @file
  NVMe SMART / Health log decoding and lossless counter formatting.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef SMART_DATA_H_
#define SMART_DATA_H_
#include <Base.h>

#define SMART_LOG_SIZE       512
#define SMART_COUNTER_COUNT  10
#define SMART_DECIMAL_SIZE   50

typedef struct {
  UINT8   CriticalWarning;
  UINT16  TemperatureKelvin;
  UINT8   AvailableSpare;
  UINT8   SpareThreshold;
  UINT8   PercentageUsed;
  UINT8   Counters[SMART_COUNTER_COUNT][16];
  UINT32  WarningTemperatureMinutes;
  UINT32  CriticalTemperatureMinutes;
  UINT16  SensorsKelvin[8];
  UINT32  ThermalTransitionCount[2];
  UINT32  ThermalTotalSeconds[2];
} SMART_INFO;

BOOLEAN SmartParse (IN CONST UINT8 *Data, IN UINTN Size, OUT SMART_INFO *Info);
BOOLEAN SmartDecimal (
  IN CONST UINT8 *LittleEndian, IN UINTN Size, OUT CHAR8 *Output, IN UINTN Capacity
  );
BOOLEAN SmartDataBytes (IN CONST UINT8 *Counter16, OUT CHAR8 *Output, IN UINTN Capacity);

#endif
