/** @file
  Individual module tests callable from UefiMain or other test code.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef TEST_MAIN_H_
#define TEST_MAIN_H_

#include <Uefi.h>

EFI_STATUS TestNVMeEnumeration (VOID);
EFI_STATUS TestNVMeInfoFetch (VOID);

#endif
