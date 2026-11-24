/** @file
  TPM2_CreatePrimary/Create/Load/Unseal/FlushContext using TCG2 SubmitCommand.
  All TPM structures are explicitly marshalled in big-endian wire format.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "TpmSeal.h"
#include <IndustryStandard/Tpm20.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>

typedef struct {
  UINT8 *Data;
  UINTN Size;
  UINTN Pos;
  BOOLEAN Ok;
} CURSOR;

typedef struct {
  EFI_TCG2_PROTOCOL *Tcg2;
  UINT32 *Rc;
  UINTN MaxCommand;
  UINTN MaxResponse;
  UINT32 PcrBanks;
  BOOLEAN PolicyAuthorization;
  UINT8 Command[4096];
  UINT8 Response[4096];
  CURSOR Write;
} TPM_IO;

// Owner RSA-2048 storage primary: fixedTPM|fixedParent|sensitiveDataOrigin|
// userWithAuth|noDA|restricted|decrypt; SHA256 name, AES128-CFB, empty unique.
STATIC CONST UINT8 mParentPublic[] = {
  0x00,0x1A, 0x00,0x01, 0x00,0x0B, 0x00,0x03,0x04,0x72, 0x00,0x00,
  0x00,0x06, 0x00,0x80, 0x00,0x43, 0x00,0x10, 0x08,0x00,
  0x00,0x00,0x00,0x00, 0x00,0x00
};
// KeyedHash sealed object: fixedTPM|fixedParent|userWithAuth. Dictionary attack
// protection stays enabled (noDA is NOT set). No PCR policy is attached.
STATIC CONST UINT8 mSealedPublic[] = {
  0x00,0x0E, 0x00,0x08, 0x00,0x0B, 0x00,0x00,0x00,0x52,
  0x00,0x00, 0x00,0x10, 0x00,0x00
};

VOID
TpmOpsWipe (IN OUT VOID *Data, IN UINTN Size)
{
  volatile UINT8 *Byte = (volatile UINT8 *)Data;
  if (Byte != NULL) {
    while (Size-- != 0) {
      *Byte++ = 0;
    }
  }
}

STATIC UINT16 Be16 (IN CONST UINT8 *Data)
{
  return (UINT16)(((UINT16)Data[0] << 8) | Data[1]);
}

STATIC UINT32 Be32 (IN CONST UINT8 *Data)
{
  return ((UINT32)Data[0] << 24) | ((UINT32)Data[1] << 16) | ((UINT32)Data[2] << 8) | Data[3];
}

STATIC VOID Put (IN OUT CURSOR *Cursor, IN CONST VOID *Data, IN UINTN Size)
{
  if (!Cursor->Ok || (Size > Cursor->Size - Cursor->Pos)) {
    Cursor->Ok = FALSE;
    return;
  }
  if (Size != 0) {
    CopyMem (Cursor->Data + Cursor->Pos, Data, Size);
  }
  Cursor->Pos += Size;
}

STATIC VOID U16 (IN OUT CURSOR *Cursor, IN UINT16 Value)
{
  UINT8 Bytes[2] = { (UINT8)(Value >> 8), (UINT8)Value };
  Put (Cursor, Bytes, sizeof (Bytes));
}

STATIC VOID U32 (IN OUT CURSOR *Cursor, IN UINT32 Value)
{
  UINT8 Bytes[4] = { (UINT8)(Value >> 24), (UINT8)(Value >> 16), (UINT8)(Value >> 8), (UINT8)Value };
  Put (Cursor, Bytes, sizeof (Bytes));
}

STATIC UINT8 *Take (IN OUT CURSOR *Cursor, IN UINTN Size)
{
  UINT8 *Data;
  if (!Cursor->Ok || (Size > Cursor->Size - Cursor->Pos)) {
    Cursor->Ok = FALSE;
    return NULL;
  }
  Data = Cursor->Data + Cursor->Pos;
  Cursor->Pos += Size;
  return Data;
}

STATIC UINT32 Get32 (IN OUT CURSOR *Cursor)
{
  UINT8 *Data = Take (Cursor, 4);
  return Data != NULL ? Be32 (Data) : 0;
}

STATIC BOOLEAN TwoB (
  IN OUT CURSOR *Cursor, OUT UINT8 *Output OPTIONAL, IN UINTN Capacity,
  OUT UINT16 *OutputSize OPTIONAL
  )
{
  UINT8 *Start = Take (Cursor, 2);
  UINTN Length;
  if (Start == NULL) {
    return FALSE;
  }
  Length = Be16 (Start);
  if (Take (Cursor, Length) == NULL) {
    return FALSE;
  }
  if (Output != NULL) {
    if (Length + 2 > Capacity) {
      Cursor->Ok = FALSE;
      return FALSE;
    }
    CopyMem (Output, Start, Length + 2);
  }
  if (OutputSize != NULL) {
    *OutputSize = (UINT16)(Length + 2);
  }
  return TRUE;
}

STATIC VOID Start (IN OUT TPM_IO *Io, IN UINT32 CommandCode, IN BOOLEAN Session)
{
  Io->PolicyAuthorization = FALSE;
  TpmOpsWipe (Io->Command, sizeof (Io->Command));
  TpmOpsWipe (Io->Response, sizeof (Io->Response));
  Io->Write = (CURSOR){ Io->Command, Io->MaxCommand, 0, TRUE };
  U16 (&Io->Write, Session ? TPM_ST_SESSIONS : TPM_ST_NO_SESSIONS);
  U32 (&Io->Write, 0); // Filled with complete command size before submission.
  U32 (&Io->Write, CommandCode);
}

STATIC VOID PasswordSession (IN OUT TPM_IO *Io, IN CONST UINT8 *Auth OPTIONAL, IN UINTN Size)
{
  UINT8 Flags = 0;
  U32 (&Io->Write, (UINT32)(9 + Size));
  U32 (&Io->Write, TPM_RS_PW);
  U16 (&Io->Write, 0); // nonce
  Put (&Io->Write, &Flags, 1);
  U16 (&Io->Write, (UINT16)Size);
  Put (&Io->Write, Auth, Size);
}

STATIC EFI_STATUS Submit (
  IN OUT TPM_IO *Io, IN BOOLEAN Session, IN BOOLEAN HasHandle,
  OUT UINT32 *Handle OPTIONAL, OUT CURSOR *Parameters
  )
{
  EFI_STATUS Status;
  CURSOR Header;
  CURSOR Auth;
  UINTN Length;
  UINT32 ParamSize;
  UINT8 *Flags;
  UINT32 CommandSize = (UINT32)Io->Write.Pos;
  UINTN Attempt;
  UINT16 NonceSize;

  if (!Io->Write.Ok || (CommandSize > Io->MaxCommand)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Io->Command[2] = (UINT8)(CommandSize >> 24);
  Io->Command[3] = (UINT8)(CommandSize >> 16);
  Io->Command[4] = (UINT8)(CommandSize >> 8);
  Io->Command[5] = (UINT8)CommandSize;
  for (Attempt = 0; Attempt < 50; Attempt++) {
    TpmOpsWipe (Io->Response, sizeof (Io->Response));
    *Io->Rc = MAX_UINT32;
    Status = Io->Tcg2->SubmitCommand (
             Io->Tcg2, CommandSize, Io->Command, (UINT32)Io->MaxResponse, Io->Response
             );
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (Status != EFI_SUCCESS) {
      return EFI_DEVICE_ERROR;
    }
    Length = Be32 (Io->Response + 2);
    if ((Length < 10) || (Length > Io->MaxResponse)) {
      return EFI_COMPROMISED_DATA;
    }
    *Io->Rc = Be32 (Io->Response + 6);
    // These TPM warnings explicitly request resubmission (e.g. async RSA key
    // generation). NEVER retry bad authorization, lockout, or transport errors.
    if ((*Io->Rc != TPM_RC_RETRY) && (*Io->Rc != TPM_RC_YIELDED) && (*Io->Rc != TPM_RC_TESTING)) {
      break;
    }
    if (Attempt + 1 == 50) { return EFI_NOT_READY; }
    Status = gBS->Stall (100000);
    if (EFI_ERROR (Status)) { return Status; }
  }
  if (*Io->Rc != TPM_RC_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }
  if (Be16 (Io->Response) != (Session ? TPM_ST_SESSIONS : TPM_ST_NO_SESSIONS)) {
    return EFI_COMPROMISED_DATA;
  }
  Header = (CURSOR){ Io->Response, Length, 10, TRUE };
  if (HasHandle) {
    *Handle = Get32 (&Header);
    if ((*Handle & 0xFF000000U) !=
        (Be32 (Io->Command + 6) == TPM_CC_StartAuthSession ? 0x03000000U : 0x80000000U)) {
      *Handle = 0;
      return EFI_COMPROMISED_DATA;
    }
  }
  if (!Header.Ok) {
    return EFI_COMPROMISED_DATA;
  }
  if (Session) {
    ParamSize = Get32 (&Header);
    if (!Header.Ok || (ParamSize > Length - Header.Pos)) {
      return EFI_COMPROMISED_DATA;
    }
    *Parameters = (CURSOR){ Io->Response + Header.Pos, ParamSize, 0, TRUE };
    Auth = (CURSOR){ Io->Response + Header.Pos + ParamSize, Length - Header.Pos - ParamSize, 0, TRUE };
    // One response authorization. Plain policy sessions have a SHA256 nonce
    // but no HMAC; password sessions have neither. No encrypted parameters.
    if (!TwoB (&Auth, NULL, 0, &NonceSize) || (NonceSize != (Io->PolicyAuthorization ? 34 : 2))) {
      return EFI_COMPROMISED_DATA;
    }
    Flags = Take (&Auth, 1);
    if ((Flags == NULL) || ((*Flags & ~1U) != 0) || !TwoB (&Auth, NULL, 0, NULL) ||
        !Auth.Ok || (Auth.Pos != (UINTN)NonceSize + 3) || (Auth.Pos != Auth.Size))
    {
      return EFI_COMPROMISED_DATA;
    }
  } else {
    *Parameters = (CURSOR){ Io->Response + Header.Pos, Length - Header.Pos, 0, TRUE };
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS Init (OUT TPM_IO *Io, IN EFI_TCG2_PROTOCOL *Tcg2, OUT UINT32 *Rc)
{
  EFI_TCG2_BOOT_SERVICE_CAPABILITY Cap;
  EFI_STATUS Status;
  ZeroMem (Io, sizeof (*Io));
  if ((Tcg2 == NULL) || (Rc == NULL) || (Tcg2->GetCapability == NULL) || (Tcg2->SubmitCommand == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  *Rc = MAX_UINT32;
  ZeroMem (&Cap, sizeof (Cap));
  Cap.Size = sizeof (Cap);
  Status = Tcg2->GetCapability (Tcg2, &Cap);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!Cap.TPMPresentFlag || (Cap.MaxCommandSize < 512) || (Cap.MaxResponseSize < 512)) {
    return EFI_UNSUPPORTED;
  }
  Io->Tcg2 = Tcg2;
  Io->Rc = Rc;
  Io->MaxCommand = MIN ((UINTN)Cap.MaxCommandSize, sizeof (Io->Command));
  Io->MaxResponse = MIN ((UINTN)Cap.MaxResponseSize, sizeof (Io->Response));
  Io->PcrBanks = Cap.ActivePcrBanks;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS Primary (IN OUT TPM_IO *Io, OUT UINT32 *Handle, OUT UINT8 *Name, OUT UINT16 *NameSize)
{
  EFI_STATUS Status;
  CURSOR Params;
  Start (Io, TPM_CC_CreatePrimary, TRUE);
  U32 (&Io->Write, TPM_RH_OWNER);
  PasswordSession (Io, NULL, 0);
  U16 (&Io->Write, 4); U16 (&Io->Write, 0); U16 (&Io->Write, 0);
  Put (&Io->Write, mParentPublic, sizeof (mParentPublic));
  U16 (&Io->Write, 0); U32 (&Io->Write, 0); // outsideInfo, empty PCR selection
  Status = Submit (Io, TRUE, TRUE, Handle, &Params);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  TwoB (&Params, NULL, 0, NULL); // outPublic
  TwoB (&Params, NULL, 0, NULL); // creationData
  TwoB (&Params, NULL, 0, NULL); // creationHash
  Take (&Params, 6); TwoB (&Params, NULL, 0, NULL); // creationTicket
  TwoB (&Params, Name, 68, NameSize);
  if (!Params.Ok || (Params.Pos != Params.Size) || (*NameSize != 36) ||
      (Be16 (Name + 2) != TPM_ALG_SHA256))
  {
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS Flush (IN OUT TPM_IO *Io, IN UINT32 Handle)
{
  CURSOR Params;
  EFI_STATUS Status;
  UINT32 SavedRc = *Io->Rc;
  if (Handle == 0) {
    return EFI_SUCCESS;
  }
  Start (Io, TPM_CC_FlushContext, FALSE);
  U32 (&Io->Write, Handle);
  Status = Submit (Io, FALSE, FALSE, NULL, &Params);
  if (!EFI_ERROR (Status) && (Params.Pos != Params.Size)) {
    Status = EFI_COMPROMISED_DATA;
  }
  // Preserve the actual failing command's TPM RC across successful cleanup.
  if (SavedRc != TPM_RC_SUCCESS) {
    *Io->Rc = SavedRc;
  }
  return Status;
}

STATIC BOOLEAN ValidBlob (IN CONST TPM_OPS_BLOB *Blob)
{
  return (Blob != NULL) && (Blob->NameSize == 36) &&
    (Blob->PublicSize >= 2) && (Blob->PublicSize <= sizeof (Blob->Public)) &&
    (Blob->PrivateSize >= 2) && (Blob->PrivateSize <= sizeof (Blob->Private)) &&
    (Be16 (Blob->ParentName) + 2 == Blob->NameSize) &&
    (Be16 (Blob->ParentName + 2) == TPM_ALG_SHA256) &&
    (Be16 (Blob->Public) + 2 == Blob->PublicSize) &&
    (Be16 (Blob->Private) + 2 == Blob->PrivateSize);
}

STATIC BOOLEAN Pcr0Public (IN CONST TPM_OPS_BLOB *Blob)
{
  return ValidBlob (Blob) && Blob->Pcr0 && Blob->PublicSize >= 48 &&
    Be16 (Blob->Public + 2) == TPM_ALG_KEYEDHASH && Be16 (Blob->Public + 4) == TPM_ALG_SHA256 &&
    Be32 (Blob->Public + 6) == 0x12 && // fixedTPM|fixedParent; NO userWithAuth/password bypass
    Be16 (Blob->Public + 10) == 32 && Be16 (Blob->Public + 44) == TPM_ALG_NULL &&
    (UINTN)Be16 (Blob->Public + 46) + 48 == Blob->PublicSize;
}

// TPM computes both PCR composite and policy digest. No host crypto dependency.
// Empty PolicyPCR.pcrDigest uses the selected PCR's CURRENT value, even in a
// trial session (TPM2_PolicyPCR). Unseal also checks the PCR update counter.
STATIC EFI_STATUS Pcr0Policy (IN OUT TPM_IO *Io, IN BOOLEAN Trial, OUT UINT32 *Handle, OUT UINT8 Digest[32])
{
  EFI_STATUS Status;
  CURSOR Params;
  UINT8 Nonce[34] = { 0 };
  UINT16 Size;
  UINTN Filled = 0;
  UINT8 Type = Trial ? TPM_SE_TRIAL : TPM_SE_POLICY;
  CONST UINT8 Selection[] = { 0,0,0,1, 0,0x0B, 3, 1,0,0 }; // SHA256 PCR0 only
  if ((Io->PcrBanks & EFI_TCG2_BOOT_HASH_ALG_SHA256) == 0) { return EFI_UNSUPPORTED; }
  // TPM GetRandom may return fewer bytes than requested. Never accept zero or
  // overlong output; at most 32 successful requests can fill this nonce.
  while (Filled < 32) {
    Start (Io, TPM_CC_GetRandom, FALSE);
    U16 (&Io->Write, (UINT16)(32 - Filled));
    Status = Submit (Io, FALSE, FALSE, NULL, &Params);
    if (EFI_ERROR (Status)) { goto Done; }
    TwoB (&Params, Nonce, sizeof (Nonce), &Size);
    if (!Params.Ok || Params.Pos != Params.Size || Size <= 2 || (UINTN)(Size - 2) > 32 - Filled) {
      Status = EFI_COMPROMISED_DATA; goto Done;
    }
    CopyMem (Digest + Filled, Nonce + 2, Size - 2);
    Filled += Size - 2;
  }
  Start (Io, TPM_CC_StartAuthSession, FALSE);
  U32 (&Io->Write, TPM_RH_NULL); U32 (&Io->Write, TPM_RH_NULL); // unsalted, unbound
  U16 (&Io->Write, 32); Put (&Io->Write, Digest, 32);
  U16 (&Io->Write, 0); // encryptedSalt
  Put (&Io->Write, &Type, 1); U16 (&Io->Write, TPM_ALG_NULL); U16 (&Io->Write, TPM_ALG_SHA256);
  Status = Submit (Io, FALSE, TRUE, Handle, &Params);
  if (EFI_ERROR (Status)) { goto Done; }
  TwoB (&Params, Nonce, sizeof (Nonce), &Size);
  if (!Params.Ok || Params.Pos != Params.Size || Size != 34) { Status = EFI_COMPROMISED_DATA; goto Done; }
  Start (Io, TPM_CC_PolicyPCR, FALSE);
  U32 (&Io->Write, *Handle); U16 (&Io->Write, 0); // current PCR digest, not a host-supplied guess
  Put (&Io->Write, Selection, sizeof (Selection));
  Status = Submit (Io, FALSE, FALSE, NULL, &Params);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Params.Pos != Params.Size) { Status = EFI_COMPROMISED_DATA; goto Done; }
  Start (Io, TPM_CC_PolicyCommandCode, FALSE);
  U32 (&Io->Write, *Handle); U32 (&Io->Write, TPM_CC_Unseal);
  Status = Submit (Io, FALSE, FALSE, NULL, &Params);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Params.Pos != Params.Size) { Status = EFI_COMPROMISED_DATA; goto Done; }
  Start (Io, TPM_CC_PolicyGetDigest, FALSE);
  U32 (&Io->Write, *Handle);
  Status = Submit (Io, FALSE, FALSE, NULL, &Params);
  if (EFI_ERROR (Status)) { goto Done; }
  TwoB (&Params, Nonce, sizeof (Nonce), &Size);
  if (!Params.Ok || Params.Pos != Params.Size || Size != 34) { Status = EFI_COMPROMISED_DATA; goto Done; }
  CopyMem (Digest, Nonce + 2, 32);
Done:
  TpmOpsWipe (Nonce, sizeof (Nonce));
  return Status;
}

STATIC EFI_STATUS
WrapInternal (
  IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST UINT8 *Secret, IN UINTN SecretSize,
  IN CONST UINT8 *Auth, IN UINTN AuthSize, OUT TPM_OPS_BLOB *Blob, OUT UINT32 *TpmRc,
  IN BOOLEAN Pcr0
  )
{
  TPM_IO Io;
  EFI_STATUS Status;
  EFI_STATUS Cleanup;
  CURSOR Params;
  UINT32 Parent = 0;
  UINT32 Policy = 0;
  UINT8 Digest[32] = { 0 };
  if ((Blob == NULL) || (Secret == NULL) || (SecretSize == 0) || (SecretSize > TPM_OPS_SECRET_MAX) ||
      (!Pcr0 && ((Auth == NULL) || (AuthSize == 0) || (AuthSize > TPM_OPS_AUTH_MAX)))) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Blob, sizeof (*Blob));
  Blob->Pcr0 = Pcr0;
  Status = Init (&Io, Tcg2, TpmRc);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Pcr0) {
    Status = Pcr0Policy (&Io, TRUE, &Policy, Digest);
    Cleanup = Flush (&Io, Policy);
    Policy = 0;
    if (!EFI_ERROR (Status)) { Status = Cleanup; }
  }
  if (!EFI_ERROR (Status)) { Status = Primary (&Io, &Parent, Blob->ParentName, &Blob->NameSize); }
  if (!EFI_ERROR (Status)) {
    Start (&Io, TPM_CC_Create, TRUE);
    U32 (&Io.Write, Parent);
    PasswordSession (&Io, NULL, 0);
    U16 (&Io.Write, (UINT16)(4 + AuthSize + SecretSize));
    U16 (&Io.Write, (UINT16)AuthSize); Put (&Io.Write, Auth, AuthSize);
    U16 (&Io.Write, (UINT16)SecretSize); Put (&Io.Write, Secret, SecretSize);
    if (Pcr0) {
      U16 (&Io.Write, 46); // TPMT_PUBLIC length, including 32-byte authPolicy
      U16 (&Io.Write, TPM_ALG_KEYEDHASH); U16 (&Io.Write, TPM_ALG_SHA256);
      U32 (&Io.Write, 0x12); // fixedTPM|fixedParent, policy ONLY
      U16 (&Io.Write, sizeof (Digest)); Put (&Io.Write, Digest, sizeof (Digest));
      U16 (&Io.Write, TPM_ALG_NULL); U16 (&Io.Write, 0); // keyedHash scheme, unique
    } else { Put (&Io.Write, mSealedPublic, sizeof (mSealedPublic)); }
    U16 (&Io.Write, 0); U32 (&Io.Write, 0);
    Status = Submit (&Io, TRUE, FALSE, NULL, &Params);
    if (!EFI_ERROR (Status)) {
      TwoB (&Params, Blob->Private, sizeof (Blob->Private), &Blob->PrivateSize);
      TwoB (&Params, Blob->Public, sizeof (Blob->Public), &Blob->PublicSize);
      TwoB (&Params, NULL, 0, NULL); TwoB (&Params, NULL, 0, NULL);
      Take (&Params, 6); TwoB (&Params, NULL, 0, NULL);
      if (!Params.Ok || (Params.Pos != Params.Size) || !ValidBlob (Blob) ||
          (Pcr0 && (!Pcr0Public (Blob) || CompareMem (Blob->Public + 12, Digest, 32) != 0))) {
        Status = EFI_COMPROMISED_DATA;
      }
    }
  }
  Cleanup = Flush (&Io, Parent);
  if (!EFI_ERROR (Status)) {
    Status = Cleanup;
  }
  TpmOpsWipe (&Io, sizeof (Io));
  TpmOpsWipe (Digest, sizeof (Digest));
  if (EFI_ERROR (Status)) {
    TpmOpsWipe (Blob, sizeof (*Blob));
  }
  return Status;
}

EFI_STATUS
TpmOpsWrap (
  IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST UINT8 *Secret, IN UINTN SecretSize,
  IN CONST UINT8 *Auth, IN UINTN AuthSize, OUT TPM_OPS_BLOB *Blob, OUT UINT32 *TpmRc
  )
{
  return WrapInternal (Tcg2, Secret, SecretSize, Auth, AuthSize, Blob, TpmRc, FALSE);
}

EFI_STATUS
TpmOpsWrapPcr0 (IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST UINT8 *Secret, IN UINTN SecretSize,
  OUT TPM_OPS_BLOB *Blob, OUT UINT32 *TpmRc)
{
  return WrapInternal (Tcg2, Secret, SecretSize, NULL, 0, Blob, TpmRc, TRUE);
}

STATIC EFI_STATUS
UnwrapInternal (
  IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST TPM_OPS_BLOB *Blob,
  IN CONST UINT8 *Auth, IN UINTN AuthSize, OUT UINT8 *Secret,
  IN OUT UINTN *SecretSize, OUT UINT32 *TpmRc, IN BOOLEAN Pcr0
  )
{
  TPM_IO Io;
  EFI_STATUS Status;
  EFI_STATUS Cleanup;
  UINT32 Parent = 0;
  UINT32 Object = 0;
  UINT32 Policy = 0;
  UINT8 Digest[32] = { 0 };
  UINT8 Name[68];
  UINT16 NameSize = 0;
  CURSOR Params;
  UINT8 *Data;
  UINTN Size;
  UINTN Capacity;

  if (!ValidBlob (Blob) || Blob->Pcr0 != Pcr0 || (Pcr0 && !Pcr0Public (Blob)) ||
      (!Pcr0 && ((Auth == NULL) || (AuthSize == 0) || (AuthSize > TPM_OPS_AUTH_MAX))) ||
      (Secret == NULL) || (SecretSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  Capacity = *SecretSize;
  *SecretSize = 0;
  TpmOpsWipe (Secret, Capacity);
  Status = Init (&Io, Tcg2, TpmRc);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = Primary (&Io, &Parent, Name, &NameSize);
  if (!EFI_ERROR (Status) && ((NameSize != Blob->NameSize) ||
      (CompareMem (Name, Blob->ParentName, NameSize) != 0))) {
    Status = EFI_SECURITY_VIOLATION;
  }
  if (!EFI_ERROR (Status)) {
    Start (&Io, TPM_CC_Load, TRUE);
    U32 (&Io.Write, Parent); PasswordSession (&Io, NULL, 0);
    Put (&Io.Write, Blob->Private, Blob->PrivateSize);
    Put (&Io.Write, Blob->Public, Blob->PublicSize);
    Status = Submit (&Io, TRUE, TRUE, &Object, &Params);
    if (!EFI_ERROR (Status)) {
      TwoB (&Params, NULL, 0, NULL);
      if (!Params.Ok || (Params.Pos != Params.Size)) {
        Status = EFI_COMPROMISED_DATA;
      }
    }
  }
  if (!EFI_ERROR (Status) && Pcr0) {
    Status = Pcr0Policy (&Io, FALSE, &Policy, Digest);
    if (!EFI_ERROR (Status) && CompareMem (Digest, Blob->Public + 12, 32) != 0) {
      // Current PCR state differs. No Unseal attempt, no password fallback/reseal.
      Status = EFI_SECURITY_VIOLATION;
    }
  }
  if (!EFI_ERROR (Status)) {
    Start (&Io, TPM_CC_Unseal, TRUE);
    U32 (&Io.Write, Object);
    if (Pcr0) {
      UINT8 Flags = 1; // continueSession, so cleanup can explicitly Flush it
      U32 (&Io.Write, 9); U32 (&Io.Write, Policy);
      U16 (&Io.Write, 0); Put (&Io.Write, &Flags, 1); U16 (&Io.Write, 0);
      Io.PolicyAuthorization = TRUE;
    } else { PasswordSession (&Io, Auth, AuthSize); }
    Status = Submit (&Io, TRUE, FALSE, NULL, &Params);
    if (!EFI_ERROR (Status)) {
      Data = Take (&Params, 2);
      Size = Data != NULL ? Be16 (Data) : 0;
      Data = Take (&Params, Size);
      if (!Params.Ok || (Params.Pos != Params.Size) || (Size == 0) || (Size > TPM_OPS_SECRET_MAX)) {
        Status = EFI_COMPROMISED_DATA;
      } else if (Size > Capacity) {
        *SecretSize = Size;
        Status = EFI_BUFFER_TOO_SMALL;
      } else {
        CopyMem (Secret, Data, Size);
        *SecretSize = Size;
      }
    }
  }
  Cleanup = Flush (&Io, Policy);
  if (!EFI_ERROR (Status)) { Status = Cleanup; }
  Cleanup = Flush (&Io, Object);
  if (!EFI_ERROR (Status)) { Status = Cleanup; }
  Cleanup = Flush (&Io, Parent);
  if (!EFI_ERROR (Status)) { Status = Cleanup; }
  TpmOpsWipe (&Io, sizeof (Io));
  TpmOpsWipe (Digest, sizeof (Digest));
  if (EFI_ERROR (Status)) {
    TpmOpsWipe (Secret, Capacity);
    if (Status != EFI_BUFFER_TOO_SMALL) { *SecretSize = 0; }
  }
  return Status;
}

EFI_STATUS
TpmOpsUnwrap (
  IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST TPM_OPS_BLOB *Blob,
  IN CONST UINT8 *Auth, IN UINTN AuthSize, OUT UINT8 *Secret,
  IN OUT UINTN *SecretSize, OUT UINT32 *TpmRc
  )
{
  return UnwrapInternal (Tcg2, Blob, Auth, AuthSize, Secret, SecretSize, TpmRc, FALSE);
}

EFI_STATUS
TpmOpsUnwrapPcr0 (IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST TPM_OPS_BLOB *Blob,
  OUT UINT8 *Secret, IN OUT UINTN *SecretSize, OUT UINT32 *TpmRc)
{
  return UnwrapInternal (Tcg2, Blob, NULL, 0, Secret, SecretSize, TpmRc, TRUE);
}

STATIC UINT32 Le32 (IN CONST UINT8 *Data)
{
  return Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) | ((UINT32)Data[3] << 24);
}
STATIC VOID PutLe32 (OUT UINT8 *Data, IN UINT32 Value)
{
  Data[0] = (UINT8)Value; Data[1] = (UINT8)(Value >> 8);
  Data[2] = (UINT8)(Value >> 16); Data[3] = (UINT8)(Value >> 24);
}

EFI_STATUS TpmOpsEncode (IN CONST TPM_OPS_BLOB *Blob, OUT UINT8 *Data, IN OUT UINTN *Size)
{
  UINTN Needed;
  if (!ValidBlob (Blob) || (Blob->Pcr0 && !Pcr0Public (Blob)) || (Size == NULL)) { return EFI_INVALID_PARAMETER; }
  Needed = TPM_OPS_HEADER_SIZE + Blob->NameSize + Blob->PublicSize + Blob->PrivateSize;
  if ((Data == NULL) || (*Size < Needed)) { *Size = Needed; return EFI_BUFFER_TOO_SMALL; }
  ZeroMem (Data, Needed);
  CopyMem (Data, Blob->Pcr0 ? "TPMWRAP2" : "TPMWRAP1", 8);
  PutLe32 (Data + 8, Blob->Pcr0 ? 2 : 1); PutLe32 (Data + 12, TPM_OPS_HEADER_SIZE);
  PutLe32 (Data + 16, Blob->Pcr0 ? 2 : 1); PutLe32 (Data + 20, 1); // PCR0 policy or auth-required; parent template ID
  PutLe32 (Data + 24, Blob->NameSize);
  PutLe32 (Data + 28, Blob->PublicSize); PutLe32 (Data + 32, Blob->PrivateSize);
  CopyMem (Data + TPM_OPS_HEADER_SIZE, Blob->ParentName, Blob->NameSize);
  CopyMem (Data + TPM_OPS_HEADER_SIZE + Blob->NameSize, Blob->Public, Blob->PublicSize);
  CopyMem (Data + TPM_OPS_HEADER_SIZE + Blob->NameSize + Blob->PublicSize, Blob->Private, Blob->PrivateSize);
  *Size = Needed;
  return EFI_SUCCESS;
}

EFI_STATUS TpmOpsDecode (IN CONST UINT8 *Data, IN UINTN Size, OUT TPM_OPS_BLOB *Blob)
{
  UINT32 Name;
  UINT32 Public;
  UINT32 Private;
  BOOLEAN Pcr0;
  if ((Data == NULL) || (Blob == NULL)) { return EFI_INVALID_PARAMETER; }
  ZeroMem (Blob, sizeof (*Blob));
  Pcr0 = Size >= TPM_OPS_HEADER_SIZE && CompareMem (Data, "TPMWRAP2", 8) == 0;
  if ((Size < TPM_OPS_HEADER_SIZE) || (Size > TPM_OPS_BLOB_MAX) ||
      (CompareMem (Data, Pcr0 ? "TPMWRAP2" : "TPMWRAP1", 8) != 0) || (Le32 (Data + 8) != (Pcr0 ? 2U : 1U)) ||
      (Le32 (Data + 12) != TPM_OPS_HEADER_SIZE) || (Le32 (Data + 16) != (Pcr0 ? 2U : 1U)) ||
      (Le32 (Data + 20) != 1) || (Le32 (Data + 36) != 0)) {
    return EFI_COMPROMISED_DATA;
  }
  Name = Le32 (Data + 24); Public = Le32 (Data + 28); Private = Le32 (Data + 32);
  if ((Name != 36) || (Public < 2) || (Public > sizeof (Blob->Public)) ||
      (Private < 2) || (Private > sizeof (Blob->Private)) ||
      (Size != TPM_OPS_HEADER_SIZE + Name + Public + Private)) {
    return EFI_COMPROMISED_DATA;
  }
  Blob->NameSize = (UINT16)Name; Blob->PublicSize = (UINT16)Public; Blob->PrivateSize = (UINT16)Private;
  Blob->Pcr0 = Pcr0;
  CopyMem (Blob->ParentName, Data + TPM_OPS_HEADER_SIZE, Name);
  CopyMem (Blob->Public, Data + TPM_OPS_HEADER_SIZE + Name, Public);
  CopyMem (Blob->Private, Data + TPM_OPS_HEADER_SIZE + Name + Public, Private);
  return ValidBlob (Blob) && (!Pcr0 || Pcr0Public (Blob)) ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}
