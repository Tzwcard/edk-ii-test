#include "ValueAccess.h"

UINT16 GetLe16 (IN CONST UINT8 *Data)
{
  return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

UINT16 GetBe16 (IN CONST UINT8 *Data)
{
  return (UINT16)(Data[1] | ((UINT16)Data[0] << 8));
}

UINT32 GetLe32 (IN CONST UINT8 *Data)
{
  return Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) | ((UINT32)Data[3] << 24);
}

UINT32 GetBe32 (IN CONST UINT8 *Data)
{
  return Data[3] | ((UINT32)Data[2] << 8) | ((UINT32)Data[1] << 16) | ((UINT32)Data[0] << 24);
}

VOID GetText (OUT CHAR8 *Output, IN CONST UINT8 *Data, IN UINTN Length)
{
  UINTN Index;
  while (Length > 0 && (Data[Length - 1] == ' ' || Data[Length - 1] == 0)) { Length--; }
  for (Index = 0; Index < Length; Index++) {
    Output[Index] = Data[Index] >= 0x20 && Data[Index] <= 0x7E ? (CHAR8)Data[Index] : '?';
  }
  Output[Length] = 0;
}

VOID PutLe16 (OUT UINT8 *Data, IN UINT16 Value)
{
  Data[0] = (UINT8)Value; Data[1] = (UINT8)(Value >> 8);
}

VOID PutBe16 (OUT UINT8 *Data, IN UINT16 Value)
{
  Data[1] = (UINT8)Value; Data[0] = (UINT8)(Value >> 8);
}

VOID PutBe32 (OUT UINT8 *Data, IN UINT32 Value)
{
  Data[3] = (UINT8)Value; Data[2] = (UINT8)(Value >> 8);
  Data[1] = (UINT8)(Value >> 16); Data[0] = (UINT8)(Value >> 24);
}

VOID PutLe32 (OUT UINT8 *Data, IN UINT32 Value)
{
  Data[0] = (UINT8)Value; Data[1] = (UINT8)(Value >> 8);
  Data[2] = (UINT8)(Value >> 16); Data[3] = (UINT8)(Value >> 24);
}