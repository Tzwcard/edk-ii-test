/* Host tests exercise the same decoding/formatting code as SMARTDisp.efi. */
#include "SmartData.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main (void)
{
  UINT8 Data[512] = { 0 };
  UINT8 Number[20] = { 0 };
  CHAR8 Text[SMART_DECIMAL_SIZE];
  SMART_INFO Info;
  UINTN Index;
  unsigned State = 12345;
  unsigned Test;

  assert (!SmartParse (NULL, 512, &Info));
  assert (!SmartParse (Data, 511, &Info));
  assert (!SmartParse (Data, 513, &Info));
  assert (!SmartParse (Data, 512, NULL));
  Data[0] = 0x3F;
  Data[1] = 0x2C; Data[2] = 1; /* 300 Kelvin */
  Data[3] = 99; Data[4] = 10; Data[5] = 201;
  Data[32] = 1;
  Data[48 + 8] = 1; /* counter > UINT64_MAX */
  Data[192] = 0x78; Data[193] = 0x56; Data[194] = 0x34; Data[195] = 0x12;
  Data[196] = 7;
  Data[200] = 0x36; Data[201] = 1;
  Data[216] = 3; Data[220] = 4; Data[224] = 5; Data[228] = 6;
  assert (SmartParse (Data, 512, &Info));
  assert (Info.CriticalWarning == 0x3F && Info.TemperatureKelvin == 300);
  assert (Info.AvailableSpare == 99 && Info.SpareThreshold == 10 && Info.PercentageUsed == 201);
  assert (Info.Counters[0][0] == 1 && Info.Counters[1][8] == 1);
  assert (Info.WarningTemperatureMinutes == 0x12345678 && Info.CriticalTemperatureMinutes == 7);
  assert (Info.SensorsKelvin[0] == 310 && Info.SensorsKelvin[7] == 0);
  assert (Info.ThermalTransitionCount[0] == 3 && Info.ThermalTransitionCount[1] == 4);
  assert (Info.ThermalTotalSeconds[0] == 5 && Info.ThermalTotalSeconds[1] == 6);

  assert (SmartDecimal (Number, 16, Text, sizeof (Text)) && strcmp (Text, "0") == 0);
  Number[0] = 1;
  assert (SmartDataBytes (Number, Text, sizeof (Text)) && strcmp (Text, "512000") == 0);
  Number[0] = 0; Number[8] = 1;
  assert (SmartDecimal (Number, 16, Text, sizeof (Text)) && strcmp (Text, "18446744073709551616") == 0);
  memset (Number, 0xFF, sizeof (Number));
  assert (SmartDecimal (Number, 16, Text, sizeof (Text)) &&
    strcmp (Text, "340282366920938463463374607431768211455") == 0);
  assert (SmartDecimal (Number, 20, Text, sizeof (Text)) &&
    strcmp (Text, "1461501637330902918203684832716283019655932542975") == 0);
  assert (SmartDataBytes (Number, Text, sizeof (Text)));
  assert (!SmartDecimal (Number, 16, Text, 39) && Text[0] == 0);
  assert (!SmartDecimal (Number, 21, Text, sizeof (Text)));
  assert (!SmartDecimal (Number, 0, Text, sizeof (Text)));
  assert (!SmartDecimal (NULL, 16, Text, sizeof (Text)));
  assert (!SmartDecimal (Number, 16, NULL, sizeof (Text)));
  assert (!SmartDataBytes (NULL, Text, sizeof (Text)));

  for (Test = 0; Test < 10000; Test++) {
    for (Index = 0; Index < sizeof (Data); Index++) {
      State = State * 1664525U + 1013904223U;
      Data[Index] = (UINT8)(State >> 24);
    }
    assert (SmartParse (Data, sizeof (Data), &Info));
    assert (SmartDecimal (Data, 16, Text, sizeof (Text)));
    assert (SmartDataBytes (Data, Text, sizeof (Text)));
  }

  puts ("PASS: SMART layout, full-width counters, decimal conversion and 10000 random records.");
  return 0;
}
