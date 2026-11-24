/** @file
  Explicit seal/unseal example, with a wrapped blob stored in a custom variable.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "TpmSeal.h"
#include <Protocol/ShellParameters.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC CHAR16 mPasswordVariable[] = L"TPMTest-{a4e9a95e-5fde-4bcb-9f44-2b487f03395d}";
STATIC CHAR16 mPcr0Variable[] = L"TPMTest-PCR0-{a4e9a95e-5fde-4bcb-9f44-2b487f03395d}";
STATIC CHAR16 *mVariableName = mPasswordVariable;
STATIC EFI_GUID mVendorGuid = {
  0xa4e9a95e, 0x5fde, 0x4bcb, { 0x9f, 0x44, 0x2b, 0x48, 0x7f, 0x03, 0x39, 0x5d }
};
STATIC CONST UINT8 mExampleSecret[] = "TPMOps example sealed payload v1";
#define BASE_ATTRIBUTES (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS)

STATIC EFI_STATUS
ReadRaw (OUT UINT8 *Data, OUT UINTN *Size, OUT UINT32 *Attributes)
{
  EFI_STATUS Status;
  *Size = TPM_OPS_BLOB_MAX;
  *Attributes = 0;
  Status = gRT->GetVariable (mVariableName, &mVendorGuid, Attributes, Size, Data);
  if (Status != EFI_SUCCESS) {
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }
  if ((*Size > TPM_OPS_BLOB_MAX) ||
      ((*Attributes != BASE_ATTRIBUTES) && (*Attributes != (BASE_ATTRIBUTES | EFI_VARIABLE_RUNTIME_ACCESS)))) {
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC VOID
Hex (IN CONST UINT8 *Data, IN UINTN Size)
{
  UINTN Index;
  for (Index = 0; Index < Size; Index++) {
    Print (L"%02x%s", (UINT32)Data[Index],
      ((Index + 1) % 16 == 0 || Index + 1 == Size) ? L"\r\n" : L" ");
  }
}

STATIC EFI_STATUS
ReadAuth (IN CONST CHAR16 *Prompt, OUT UINT8 *Auth, OUT UINTN *Size)
{
  EFI_STATUS Status;
  EFI_INPUT_KEY Key;
  UINTN EventIndex;
  *Size = 0;
  TpmOpsWipe (Auth, TPM_OPS_AUTH_MAX);
  Print (L"%s (8-32 printable ASCII bytes, input hidden; Esc cancels): ", Prompt);
  for (;;) {
    Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR (Status)) { break; }
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (Status == EFI_NOT_READY) { continue; }
    if (EFI_ERROR (Status)) { break; }
    if (Key.ScanCode == SCAN_ESC || Key.UnicodeChar == 0x1B) {
      Status = EFI_ABORTED;
      break;
    }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      Status = *Size >= 8 ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
      break;
    }
    if (Key.UnicodeChar == CHAR_BACKSPACE) {
      if (*Size > 0) { Auth[--*Size] = 0; }
    } else if (Key.UnicodeChar >= 0x20 && Key.UnicodeChar <= 0x7E) {
      if (*Size == TPM_OPS_AUTH_MAX) { Status = EFI_BAD_BUFFER_SIZE; break; }
      Auth[(*Size)++] = (UINT8)Key.UnicodeChar;
    }
  }
  Print (L"\r\n");
  if (EFI_ERROR (Status)) { TpmOpsWipe (Auth, TPM_OPS_AUTH_MAX); *Size = 0; }
  return Status;
}

STATIC EFI_STATUS
OpenTpm (OUT EFI_TCG2_PROTOCOL **Tcg2)
{
  EFI_STATUS Status = gBS->LocateProtocol (&gEfiTcg2ProtocolGuid, NULL, (VOID **)Tcg2);
  if (EFI_ERROR (Status)) { Print (L"TPM 2.0 TCG2 protocol unavailable: %r\r\n", Status); }
  return Status;
}

STATIC VOID
ReportTpm (IN EFI_STATUS Status, IN UINT32 Rc)
{
  Print (L"TPM operation: %r", Status);
  if (Rc != MAX_UINT32) { Print (L", TPM_RC=0x%08x", Rc); }
  Print (L"\r\n");
  if (EFI_ERROR (Status)) {
    Print (L"No authorization retry. Wrong authorization can count toward TPM dictionary-attack lockout.\r\n");
  }
}

STATIC EFI_STATUS
Wrap (IN BOOLEAN Runtime, IN BOOLEAN Pcr0)
{
  EFI_STATUS Status;
  EFI_TCG2_PROTOCOL *Tcg2;
  TPM_OPS_BLOB Blob;
  TPM_OPS_BLOB CheckBlob;
  UINT8 Raw[TPM_OPS_BLOB_MAX];
  UINT8 Readback[TPM_OPS_BLOB_MAX];
  UINT8 Auth[TPM_OPS_AUTH_MAX];
  UINT8 Confirmation[TPM_OPS_AUTH_MAX];
  UINT8 Secret[TPM_OPS_SECRET_MAX];
  UINTN AuthSize = 0;
  UINTN ConfirmationSize = 0;
  UINTN SecretSize = sizeof (Secret);
  UINTN Size;
  UINTN ReadbackSize;
  UINT32 Attributes;
  UINT32 Wanted = BASE_ATTRIBUTES | (Runtime ? EFI_VARIABLE_RUNTIME_ACCESS : 0);
  UINT32 Rc = MAX_UINT32;

  ZeroMem (&Blob, sizeof (Blob));
  ZeroMem (&CheckBlob, sizeof (CheckBlob));
  ZeroMem (Auth, sizeof (Auth)); ZeroMem (Confirmation, sizeof (Confirmation));
  ZeroMem (Secret, sizeof (Secret));
  Status = ReadRaw (Raw, &Size, &Attributes);
  if (Status != EFI_NOT_FOUND) {
    Print (L"Wrap refused: variable exists or could not be safely checked (%r).\r\n", Status);
    Print (L"Use info/dump first; delete explicitly before replacing it or changing --RT.\r\n");
    Status = Status == EFI_SUCCESS ? EFI_ALREADY_STARTED : Status;
    goto Done;
  }
  Status = OpenTpm (&Tcg2);
  if (EFI_ERROR (Status)) { goto Done; }
  if (!Pcr0) {
    Status = ReadAuth (L"New object authorization", Auth, &AuthSize);
    if (EFI_ERROR (Status)) { goto Done; }
    Status = ReadAuth (L"Confirm authorization", Confirmation, &ConfirmationSize);
    if (EFI_ERROR (Status)) { goto Done; }
    if ((AuthSize != ConfirmationSize) || (CompareMem (Auth, Confirmation, AuthSize) != 0)) {
      Print (L"Authorization mismatch; nothing written.\r\n"); Status = EFI_INVALID_PARAMETER; goto Done;
    }
  }
  TpmOpsWipe (Confirmation, sizeof (Confirmation));
  if (Pcr0) {
    Print (L"Policy-only: current SHA256 PCR0 + Unseal command; no object password or fallback.\r\n");
    Status = TpmOpsWrapPcr0 (Tcg2, mExampleSecret, sizeof (mExampleSecret), &Blob, &Rc);
  } else { Status = TpmOpsWrap (Tcg2, mExampleSecret, sizeof (mExampleSecret), Auth, AuthSize, &Blob, &Rc); }
  ReportTpm (Status, Rc);
  if (EFI_ERROR (Status)) { goto Done; }
  // Verify a fresh recreate-primary/load/unseal cycle BEFORE committing to NVRAM.
  Status = Pcr0 ? TpmOpsUnwrapPcr0 (Tcg2, &Blob, Secret, &SecretSize, &Rc) :
    TpmOpsUnwrap (Tcg2, &Blob, Auth, AuthSize, Secret, &SecretSize, &Rc);
  ReportTpm (Status, Rc);
  if (EFI_ERROR (Status)) { goto Done; }
  if ((SecretSize != sizeof (mExampleSecret)) || (CompareMem (Secret, mExampleSecret, SecretSize) != 0)) {
    Status = EFI_SECURITY_VIOLATION; goto Done;
  }
  TpmOpsWipe (Secret, sizeof (Secret));
  Size = sizeof (Raw);
  Status = TpmOpsEncode (&Blob, Raw, &Size);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = gRT->SetVariable (mVariableName, &mVendorGuid, Wanted, Size, Raw);
  Print (L"SetVariable: %r, attributes=0x%08x\r\n", Status, Wanted);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = ReadRaw (Readback, &ReadbackSize, &Attributes);
  if (EFI_ERROR (Status)) { goto Done; }
  if ((Attributes != Wanted) || (ReadbackSize != Size) || (CompareMem (Raw, Readback, Size) != 0)) {
    Status = EFI_COMPROMISED_DATA; goto Done;
  }
  Status = TpmOpsDecode (Readback, ReadbackSize, &CheckBlob);
  if (!EFI_ERROR (Status)) {
    Print (L"TPMOps WRAP PASS: sealed blob stored and verified; RT=%u.\r\n", (UINT32)Runtime);
  }
Done:
  TpmOpsWipe (Auth, sizeof (Auth)); TpmOpsWipe (Confirmation, sizeof (Confirmation));
  TpmOpsWipe (Secret, sizeof (Secret));
  TpmOpsWipe (&Blob, sizeof (Blob)); TpmOpsWipe (&CheckBlob, sizeof (CheckBlob));
  return Status;
}

STATIC EFI_STATUS
StoredOperation (IN CONST CHAR16 *Command, IN BOOLEAN Pcr0)
{
  EFI_STATUS Status;
  EFI_TCG2_PROTOCOL *Tcg2;
  TPM_OPS_BLOB Blob;
  UINT8 Raw[TPM_OPS_BLOB_MAX];
  UINT8 Auth[TPM_OPS_AUTH_MAX] = { 0 };
  UINT8 Secret[TPM_OPS_SECRET_MAX] = { 0 };
  UINTN Size;
  UINTN AuthSize;
  UINTN SecretSize = sizeof (Secret);
  UINT32 Attributes;
  UINT32 Rc = MAX_UINT32;

  ZeroMem (&Blob, sizeof (Blob));
  Status = ReadRaw (Raw, &Size, &Attributes);
  Print (L"GetVariable: %r\r\n", Status);
  if (EFI_ERROR (Status)) { goto Done; }
  Print (L"Attributes: 0x%08x (NV=1 BS=1 RT=%u), record=%u bytes\r\n",
    Attributes, (UINT32)((Attributes & EFI_VARIABLE_RUNTIME_ACCESS) != 0), (UINT32)Size);
  if (StrCmp (Command, L"delete") == 0) {
    Status = gRT->SetVariable (mVariableName, &mVendorGuid, 0, 0, NULL);
    Print (L"TPMOps delete: %r (only the wrapped blob; TPM hierarchy is unchanged).\r\n", Status);
    goto Done;
  }
  Status = TpmOpsDecode (Raw, Size, &Blob);
  if (EFI_ERROR (Status)) { Print (L"Invalid wrapped record; refusing to load it.\r\n"); goto Done; }
  if (Blob.Pcr0 != Pcr0) { Print (L"Record authorization mode mismatch; no fallback.\r\n"); Status = EFI_SECURITY_VIOLATION; goto Done; }
  Print (L"Record v%u: parent-template=1, name=%u, public=%u, private=%u bytes.\r\n",
    Pcr0 ? 2U : 1U, (UINT32)Blob.NameSize, (UINT32)Blob.PublicSize, (UINT32)Blob.PrivateSize);
  Print (Pcr0 ? L"Policy-only: SHA256 PCR0 + Unseal command; no object password.\r\n" :
    L"Object-auth required, no PCR policy.\r\n");
  if (StrCmp (Command, L"info") == 0) { goto Done; }
  if (StrCmp (Command, L"dump") == 0) {
    Print (L"TPMOPS_BLOB_BEGIN\r\n"); Hex (Raw, Size); Print (L"TPMOPS_BLOB_END\r\n"); goto Done;
  }
  Status = OpenTpm (&Tcg2);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Pcr0) {
    Status = TpmOpsUnwrapPcr0 (Tcg2, &Blob, Secret, &SecretSize, &Rc);
    if (EFI_ERROR (Status)) { Print (L"PCR policy unseal failed. No reseal or password fallback.\r\n"); }
  } else {
    Status = ReadAuth (L"Object authorization", Auth, &AuthSize);
    if (EFI_ERROR (Status)) { goto Done; }
    Status = TpmOpsUnwrap (Tcg2, &Blob, Auth, AuthSize, Secret, &SecretSize, &Rc);
  }
  ReportTpm (Status, Rc);
  if (!EFI_ERROR (Status)) {
    Print (L"Unsealed plaintext (%u bytes, displayed for this example):\r\n", (UINT32)SecretSize);
    Hex (Secret, SecretSize);
    Print (L"TPMOps UNWRAP PASS.\r\n");
  }
Done:
  TpmOpsWipe (Auth, sizeof (Auth)); TpmOpsWipe (Secret, sizeof (Secret));
  TpmOpsWipe (&Blob, sizeof (Blob));
  return Status;
}

EFI_STATUS
EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_SHELL_PARAMETERS_PROTOCOL *Parameters;
  EFI_STATUS Status;
  CONST CHAR16 *Command = NULL;
  BOOLEAN Runtime = FALSE;
  BOOLEAN Pcr0 = FALSE;
  UINTN Index;
  Print (L"TPMOps - TPM 2.0 sealed-data example\r\n");
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Parameters);
  if (!EFI_ERROR (Status)) {
    for (Index = 1; Index < Parameters->Argc; Index++) {
      if (StrCmp (Parameters->Argv[Index], L"--RT") == 0 && !Runtime) {
        Runtime = TRUE;
      } else if (StrCmp (Parameters->Argv[Index], L"--pcr0") == 0 && !Pcr0) {
        Pcr0 = TRUE;
      } else if (Command == NULL) {
        Command = Parameters->Argv[Index];
      } else {
        Print (L"Invalid arguments. Use TPMOps.efi --help\r\n"); return EFI_INVALID_PARAMETER;
      }
    }
  }
  mVariableName = Pcr0 ? mPcr0Variable : mPasswordVariable;
  Print (L"Variable: %s\r\nVendor GUID: %g\r\n", mVariableName, &mVendorGuid);
  if ((Command == NULL) || (StrCmp (Command, L"--help") == 0) || (StrCmp (Command, L"-h") == 0)) {
    Print (L"Usage: TPMOps.efi wrap [--RT] [--pcr0] | unwrap | info | dump | delete [--pcr0]\r\n");
    Print (L"Default NV|BS. --RT adds runtime visibility ONLY when wrapping a new variable.\r\n");
    Print (L"wrap seals built-in sample data, asks for object authorization twice, refuses overwrite.\r\n");
    Print (L"--pcr0 selects a SEPARATE variable: policy-only SHA256 PCR0 + Unseal, no password prompt.\r\n");
    Print (L"PCR mismatch never reseals/falls back. No PCR writes, TPM Clear, hierarchy changes or TPM NV indexes.\r\n");
    return EFI_SUCCESS;
  }
  if (StrCmp (Command, L"wrap") == 0) { return Wrap (Runtime, Pcr0); }
  if (Runtime) { Print (L"--RT is only valid with wrap; existing attributes are not changed.\r\n"); return EFI_INVALID_PARAMETER; }
  if ((StrCmp (Command, L"unwrap") == 0) || (StrCmp (Command, L"info") == 0) ||
      (StrCmp (Command, L"dump") == 0) || (StrCmp (Command, L"delete") == 0)) {
    return StoredOperation (Command, Pcr0);
  }
  Print (L"Unknown command. Use TPMOps.efi --help\r\n");
  return EFI_INVALID_PARAMETER;
}
