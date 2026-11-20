/** @file
  Explicit little-endian decoding; do not truncate 128-bit NVMe counters.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "SmartData.h"

STATIC UINT16
Le16 (IN CONST UINT8 *Data)
{
  return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

STATIC UINT32
Le32 (IN CONST UINT8 *Data)
{
  return Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) | ((UINT32)Data[3] << 24);
}

BOOLEAN
SmartParse (IN CONST UINT8 *Data, IN UINTN Size, OUT SMART_INFO *Info)
{
  UINTN Index;
  UINTN Byte;
  SMART_INFO Parsed = { 0 };

  if ((Data == NULL) || (Info == NULL) || (Size != SMART_LOG_SIZE)) {
    return FALSE;
  }

  Parsed.CriticalWarning = Data[0];
  Parsed.TemperatureKelvin = Le16 (Data + 1);
  Parsed.AvailableSpare = Data[3];
  Parsed.SpareThreshold = Data[4];
  Parsed.PercentageUsed = Data[5];
  for (Index = 0; Index < SMART_COUNTER_COUNT; Index++) {
    for (Byte = 0; Byte < 16; Byte++) {
      Parsed.Counters[Index][Byte] = Data[32 + Index * 16 + Byte];
    }
  }

  Parsed.WarningTemperatureMinutes = Le32 (Data + 192);
  Parsed.CriticalTemperatureMinutes = Le32 (Data + 196);
  for (Index = 0; Index < 8; Index++) {
    Parsed.SensorsKelvin[Index] = Le16 (Data + 200 + Index * 2);
  }

  for (Index = 0; Index < 2; Index++) {
    Parsed.ThermalTransitionCount[Index] = Le32 (Data + 216 + Index * 4);
    Parsed.ThermalTotalSeconds[Index] = Le32 (Data + 224 + Index * 4);
  }

  *Info = Parsed;
  return TRUE;
}

BOOLEAN
SmartDecimal (
  IN CONST UINT8 *LittleEndian, IN UINTN Size, OUT CHAR8 *Output, IN UINTN Capacity
  )
{
  UINT8 Work[20] = { 0 };
  CHAR8 Reverse[49];
  UINTN Index;
  UINTN Digits = 0;
  UINT32 Remainder;

  if ((LittleEndian == NULL) || (Output == NULL) || (Capacity == 0) || (Size == 0) || (Size > sizeof (Work))) {
    return FALSE;
  }

  Output[0] = '\0';
  for (Index = 0; Index < Size; Index++) {
    Work[Index] = LittleEndian[Index];
  }

  do {
    Remainder = 0;
    for (Index = Size; Index > 0; Index--) {
      Remainder = Remainder * 256 + Work[Index - 1];
      Work[Index - 1] = (UINT8)(Remainder / 10);
      Remainder %= 10;
    }

    Reverse[Digits++] = (CHAR8)('0' + Remainder);
    while ((Size > 0) && (Work[Size - 1] == 0)) {
      Size--;
    }
  } while (Size != 0);

  if (Capacity <= Digits) {
    return FALSE;
  }

  for (Index = 0; Index < Digits; Index++) {
    Output[Index] = Reverse[Digits - Index - 1];
  }

  Output[Digits] = '\0';
  return TRUE;
}

BOOLEAN
SmartDataBytes (IN CONST UINT8 *Counter16, OUT CHAR8 *Output, IN UINTN Capacity)
{
  UINT8 Bytes[20] = { 0 };
  UINT32 Carry = 0;
  UINTN Index;

  if (Counter16 == NULL) {
    return FALSE;
  }

  // One NVMe Data Unit = 1000 * 512 bytes (reported rounded up).
  for (Index = 0; Index < sizeof (Bytes); Index++) {
    Carry += (Index < 16 ? Counter16[Index] : 0) * 512000U;
    Bytes[Index] = (UINT8)Carry;
    Carry >>= 8;
  }

  return SmartDecimal (Bytes, sizeof (Bytes), Output, Capacity);
}
