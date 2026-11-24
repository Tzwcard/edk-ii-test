/* Host support only; production derivation and EDK PKCS5 wrapper are unchanged. */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseCryptLib.h>

VOID *EFIAPI
CopyMem (VOID *Destination, CONST VOID *Source, UINTN Size)
{
  UINT8 *To = Destination;
  CONST UINT8 *From = Source;
  UINTN Index;
  for (Index = 0; Index < Size; Index++) { To[Index] = From[Index]; }
  return Destination;
}

#ifdef TEST_CRYPTO_FAILURE
BOOLEAN EFIAPI
Pkcs5HashPassword (UINTN PasswordLength, CONST CHAR8 *Password, UINTN SaltLength,
  CONST UINT8 *Salt, UINTN IterationCount, UINTN DigestSize, UINTN KeyLength, UINT8 *OutKey)
{
  (VOID)PasswordLength; (VOID)Password; (VOID)SaltLength; (VOID)Salt;
  (VOID)IterationCount; (VOID)DigestSize;
  // Simulate a backend failing after partially writing its output.
  if (KeyLength != 0) { OutKey[0] = 0xCC; }
  return FALSE;
}
#endif
