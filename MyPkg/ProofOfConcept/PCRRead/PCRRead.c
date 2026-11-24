#include <Uefi.h>
#include <IndustryStandard/Tpm20.h>
#include <Library/BaseMemoryLib.h>
#include <Library/Tpm2CommandLib.h>
#include <Library/UefiLib.h>

EFI_STATUS
ReadSha256Pcr (
  IN UINT32 PcrIndex
  )
{
  EFI_STATUS          Status;
  TPML_PCR_SELECTION  Selection;
  TPML_PCR_SELECTION  ReturnedSelection;
  TPML_DIGEST         Values;
  UINT32              UpdateCounter;
  UINTN               Index;

  if (PcrIndex >= 24) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&Selection, sizeof (Selection));
  ZeroMem (&ReturnedSelection, sizeof (ReturnedSelection));
  ZeroMem (&Values, sizeof (Values));

  // Select one PCR in the SHA-256 bank.
  Selection.count = 1;
  Selection.pcrSelections[0].hash = TPM_ALG_SHA256;
  Selection.pcrSelections[0].sizeofSelect = 3;
  Selection.pcrSelections[0].pcrSelect[PcrIndex / 8] =
    (UINT8)(1U << (PcrIndex % 8));

  Status = Tpm2PcrRead (
             &Selection,
             &UpdateCounter,
             &ReturnedSelection,
             &Values
             );
  if (EFI_ERROR (Status)) {
    Print (L"PCR read failed: %r\r\n", Status);
    return Status;
  }

  // Verify that the TPM returned the requested PCR and digest.
  if ((ReturnedSelection.count != 1) ||
      (ReturnedSelection.pcrSelections[0].hash != TPM_ALG_SHA256) ||
      (ReturnedSelection.pcrSelections[0].sizeofSelect != 3) ||
      (CompareMem (
         ReturnedSelection.pcrSelections[0].pcrSelect,
         Selection.pcrSelections[0].pcrSelect,
         3
         ) != 0) ||
      (Values.count != 1) ||
      (Values.digests[0].size != 32))
  {
    return EFI_NOT_FOUND;
  }

  // Print SHA256 PCRs
  Print (L"PCR[%02u] = ", PcrIndex);

  for (Index = 0; Index < Values.digests[0].size; Index++) {
    Print (L"%02x", (UINT32)Values.digests[0].buffer[Index]);
  }

  Print (L"\r\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32 Index = 0;
  for (Index = 0; Index < 24; Index++) {
    ReadSha256Pcr(Index);
  }

  return EFI_SUCCESS;
}