/* Mock transport and blob parser tests; never opens a real TPM. */
#include "TpmSeal.h"
#include <IndustryStandard/Tpm20.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
static unsigned Mode;
static unsigned Calls;
static unsigned Stalls;
static UINT8 Fuzz[4096];

VOID *EFIAPI ZeroMem (VOID *Data, UINTN Size) { return memset (Data, 0, Size); }
VOID *EFIAPI CopyMem (VOID *To, CONST VOID *From, UINTN Size) { return memcpy (To, From, Size); }
INTN EFIAPI CompareMem (CONST VOID *Left, CONST VOID *Right, UINTN Size) { return memcmp (Left, Right, Size); }
static UINT32 be32 (CONST UINT8 *Data) {
  return ((UINT32)Data[0] << 24) | ((UINT32)Data[1] << 16) | ((UINT32)Data[2] << 8) | Data[3];
}
static VOID put32 (UINT8 *Data, UINT32 Value) {
  Data[0] = (UINT8)(Value >> 24); Data[1] = (UINT8)(Value >> 16);
  Data[2] = (UINT8)(Value >> 8); Data[3] = (UINT8)Value;
}
static EFI_STATUS EFIAPI Stall (UINTN Time) { assert (Time == 100000); Stalls++; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Capability (EFI_TCG2_PROTOCOL *This, EFI_TCG2_BOOT_SERVICE_CAPABILITY *Cap) {
  (void)This;
  Cap->TPMPresentFlag = TRUE; Cap->MaxCommandSize = 4096; Cap->MaxResponseSize = 4096;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Submit (
  EFI_TCG2_PROTOCOL *This, UINT32 Size, UINT8 *Command, UINT32 Capacity, UINT8 *Response
  )
{
  UINT32 Code = be32 (Command + 6);
  (void)This;
  Calls++;
  assert (be32 (Command + 2) == Size && Capacity == 4096);
  assert (Code == TPM_CC_CreatePrimary || Code == TPM_CC_FlushContext || Code == TPM_CC_Create ||
          Code == TPM_CC_Load || Code == TPM_CC_Unseal);
  if (Mode == 1) { return EFI_TIMEOUT; }
  memset (Response, 0, Capacity);
  Response[0] = 0x80; Response[1] = 1; put32 (Response + 2, 10);
  if (Code == TPM_CC_FlushContext) {
    assert ((be32 (Command + 10) & 0xFF000000U) == 0x80000000U);
    return EFI_SUCCESS;
  }
  if (Mode == 2) { put32 (Response + 6, 0x98E); }
  if (Mode == 3) { put32 (Response + 6, TPM_RC_RETRY); }
  if (Mode == 4) { put32 (Response + 2, 4097); }
  if (Mode == 5) { Response[0] = 0xFF; Response[1] = 0xFF; }
  if (Mode == 6) {
    // A returned transient handle with malformed session auth must be flushed.
    Response[1] = 2; put32 (Response + 2, 23); put32 (Response + 10, 0x80000001);
    put32 (Response + 14, 0); Response[22] = 1;
  }
  if (Mode == 7) { memcpy (Response, Fuzz, Capacity); }
  return EFI_SUCCESS;
}

int main (void)
{
  TPM_OPS_BLOB Blob;
  TPM_OPS_BLOB Decoded;
  UINT8 Record[TPM_OPS_BLOB_MAX];
  UINT8 Copy[TPM_OPS_BLOB_MAX];
  UINT8 Secret[128];
  UINT8 Auth[8] = {1,2,3,4,5,6,7,8};
  UINTN Size;
  UINTN Needed;
  UINT32 Rc;
  EFI_STATUS Status;
  EFI_TCG2_PROTOCOL Protocol = { 0 };
  unsigned Seed = 12345;
  unsigned Test;
  UINTN Index;

  Services.Stall = Stall;
  Protocol.GetCapability = Capability;
  Protocol.SubmitCommand = Submit;
  memset (&Blob, 0, sizeof (Blob));
  Blob.NameSize = 36; Blob.ParentName[1] = 34; Blob.ParentName[3] = 0x0B;
  Blob.PublicSize = 16; Blob.Public[1] = 14;
  Blob.PrivateSize = 18; Blob.Private[1] = 16;
  Needed = 0;
  assert (TpmOpsEncode (&Blob, NULL, &Needed) == EFI_BUFFER_TOO_SMALL && Needed == 110);
  Size = sizeof (Record);
  assert (TpmOpsEncode (&Blob, Record, &Size) == EFI_SUCCESS && Size == Needed);
  assert (memcmp (Record, "TPMWRAP1", 8) == 0 && Record[12] == 40);
  assert (TpmOpsDecode (Record, Size, &Decoded) == EFI_SUCCESS);
  assert (memcmp (&Blob, &Decoded, sizeof (Blob)) == 0);
  assert (TpmOpsDecode (Record, Size - 1, &Decoded) == EFI_COMPROMISED_DATA);
  assert (TpmOpsDecode (NULL, Size, &Decoded) == EFI_INVALID_PARAMETER);
  memcpy (Copy, Record, Size); Copy[16] = 0;
  assert (TpmOpsDecode (Copy, Size, &Decoded) == EFI_COMPROMISED_DATA);
  memcpy (Copy, Record, Size); memset (Copy + 28, 0xFF, 4);
  assert (TpmOpsDecode (Copy, Size, &Decoded) == EFI_COMPROMISED_DATA);
  memcpy (Copy, Record, Size); Copy[40] = 0xFF;
  assert (TpmOpsDecode (Copy, Size, &Decoded) == EFI_COMPROMISED_DATA);

  assert (TpmOpsWrap (&Protocol, Auth, 8, Auth, 0, &Decoded, &Rc) == EFI_INVALID_PARAMETER);
  assert (Calls == 0);
  Mode = 1; Calls = Stalls = 0;
  assert (TpmOpsWrap (&Protocol, Auth, 8, Auth, 8, &Decoded, &Rc) == EFI_TIMEOUT && Calls == 1 && Stalls == 0);
  Mode = 2; Calls = Stalls = 0;
  assert (TpmOpsWrap (&Protocol, Auth, 8, Auth, 8, &Decoded, &Rc) == EFI_DEVICE_ERROR && Calls == 1 && Rc == 0x98E);
  Mode = 3; Calls = Stalls = 0;
  assert (TpmOpsWrap (&Protocol, Auth, 8, Auth, 8, &Decoded, &Rc) == EFI_NOT_READY && Calls == 50 && Stalls == 49);
  for (Mode = 4; Mode <= 6; Mode++) {
    Calls = Stalls = 0;
    assert (TpmOpsWrap (&Protocol, Auth, 8, Auth, 8, &Decoded, &Rc) == EFI_COMPROMISED_DATA);
    assert (Calls == (Mode == 6 ? 2 : 1));
  }
  Mode = 2; memset (Secret, 0xA5, sizeof (Secret)); Size = sizeof (Secret);
  assert (TpmOpsUnwrap (&Protocol, &Blob, Auth, 8, Secret, &Size, &Rc) == EFI_DEVICE_ERROR && Size == 0);
  for (Index = 0; Index < sizeof (Secret); Index++) { assert (Secret[Index] == 0); }

  for (Test = 0; Test < 10000; Test++) {
    for (Index = 0; Index < sizeof (Copy); Index++) {
      Seed = Seed * 1664525U + 1013904223U;
      Copy[Index] = (UINT8)(Seed >> 24);
    }
    if (Test & 1) { memcpy (Copy, Record, 40); }
    (void)TpmOpsDecode (Copy, Test % sizeof (Copy), &Decoded);
    for (Index = 0; Index < sizeof (Fuzz); Index++) {
      Seed = Seed * 1664525U + 1013904223U;
      Fuzz[Index] = (UINT8)(Seed >> 24);
    }
    if (Test & 1) {
      Fuzz[0] = 0x80; Fuzz[1] = 2;
      put32 (Fuzz + 2, 10 + Test % 4087); put32 (Fuzz + 6, 0);
    }
    Mode = 7;
    Status = TpmOpsWrap (&Protocol, Auth, 8, Auth, 8, &Decoded, &Rc);
    assert (EFI_ERROR (Status));
  }
  puts ("PASS: blob codec, response bounds, auth failures without retry, bounded busy retry and cleanup.");
  puts ("PASS: 10000 malformed records and 10000 mocked TPM responses under sanitizers; no hardware access.");
  return 0;
}
