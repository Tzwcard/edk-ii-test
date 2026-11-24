/** @file
  TPM 2.0 sealed-data example. No PCR writes, persistent handles or TPM NV indexes.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef TPM_OPS_SEAL_H_
#define TPM_OPS_SEAL_H_
#include <Uefi.h>
#include <Protocol/Tcg2Protocol.h>

#define TPM_OPS_AUTH_MAX     32
#define TPM_OPS_SECRET_MAX   128
#define TPM_OPS_BLOB_MAX     2200
#define TPM_OPS_HEADER_SIZE  40

typedef struct {
  BOOLEAN Pcr0; // v2: policy-only SHA256 PCR0 + PolicyCommandCode(Unseal).
  UINT16 NameSize;
  UINT16 PublicSize;
  UINT16 PrivateSize;
  UINT8  ParentName[68];
  UINT8  Public[1024];
  UINT8  Private[1024];
} TPM_OPS_BLOB;

// Auth is 1..32 raw bytes; no encoding/KDF. Parent uses empty owner auth.
EFI_STATUS TpmOpsWrap (
  IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST UINT8 *Secret, IN UINTN SecretSize,
  IN CONST UINT8 *Auth, IN UINTN AuthSize, OUT TPM_OPS_BLOB *Blob, OUT UINT32 *TpmRc
  );
EFI_STATUS TpmOpsUnwrap (
  IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST TPM_OPS_BLOB *Blob,
  IN CONST UINT8 *Auth, IN UINTN AuthSize, OUT UINT8 *Secret,
  IN OUT UINTN *SecretSize, OUT UINT32 *TpmRc
  );
EFI_STATUS TpmOpsWrapPcr0 (
  IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST UINT8 *Secret, IN UINTN SecretSize,
  OUT TPM_OPS_BLOB *Blob, OUT UINT32 *TpmRc
  );
EFI_STATUS TpmOpsUnwrapPcr0 (
  IN EFI_TCG2_PROTOCOL *Tcg2, IN CONST TPM_OPS_BLOB *Blob,
  OUT UINT8 *Secret, IN OUT UINTN *SecretSize, OUT UINT32 *TpmRc
  );
EFI_STATUS TpmOpsEncode (IN CONST TPM_OPS_BLOB *Blob, OUT UINT8 *Data, IN OUT UINTN *Size);
EFI_STATUS TpmOpsDecode (IN CONST UINT8 *Data, IN UINTN Size, OUT TPM_OPS_BLOB *Blob);
VOID TpmOpsWipe (IN OUT VOID *Data, IN UINTN Size);

#endif
