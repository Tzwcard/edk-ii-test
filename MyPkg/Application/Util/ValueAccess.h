/** @file
**/

#ifndef UTIL_VALUE_ACCESS_H_
#define UTIL_VALUE_ACCESS_H_

#include <Base.h>

UINT16 GetLe16 (IN CONST UINT8 *Data);
UINT16 GetBe16 (IN CONST UINT8 *Data);

VOID PutLe16(OUT UINT8 *Data, IN UINT16);
VOID PutBe16(OUT UINT8 *Data, IN UINT16);

UINT32 GetLe32 (IN CONST UINT8 *Data);
UINT32 GetBe32 (IN CONST UINT8 *Data);

VOID PutLe32(OUT UINT8 *Data, IN UINT32);
VOID PutBe32(OUT UINT8 *Data, IN UINT32);

/**
  This for reading Model/Serial/Firmware text that appended with space
  Auto discard space and only read until the end of it
 */
VOID GetText (OUT CHAR8 *Output, IN CONST UINT8 *Data, IN UINTN Length);

#endif // UTIL_VALUE_ACCESS_H_