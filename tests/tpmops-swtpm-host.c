/* Test-only TCG2 adapter for a private Unix-domain swtpm socket. Never /dev/tpm. */
#include "TpmSeal.h"
#include <IndustryStandard/Tpm20.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>

static int Socket = -1;
static unsigned Creates, Unseals;
static UINT32 Banks = EFI_TCG2_BOOT_HASH_ALG_SHA256;
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;

VOID *EFIAPI ZeroMem (VOID *Data, UINTN Size) { return memset (Data, 0, Size); }
VOID *EFIAPI CopyMem (VOID *To, CONST VOID *From, UINTN Size) { return memcpy (To, From, Size); }
INTN EFIAPI CompareMem (CONST VOID *Left, CONST VOID *Right, UINTN Size) { return memcmp (Left, Right, Size); }
static UINT32 be32 (CONST UINT8 *Data) {
  return ((UINT32)Data[0] << 24) | ((UINT32)Data[1] << 16) | ((UINT32)Data[2] << 8) | Data[3];
}
static EFI_STATUS EFIAPI Stall (UINTN Time) { usleep (Time); return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Capability (EFI_TCG2_PROTOCOL *This, EFI_TCG2_BOOT_SERVICE_CAPABILITY *Cap) {
  (void)This;
  Cap->TPMPresentFlag = TRUE; Cap->MaxCommandSize = 4096; Cap->MaxResponseSize = 4096;
  Cap->ActivePcrBanks = Banks;
  return EFI_SUCCESS;
}
static int transfer (void *Data, size_t Size, int Sending) {
  unsigned char *Byte = Data;
  while (Size != 0) {
    ssize_t Done = Sending ? send (Socket, Byte, Size, MSG_NOSIGNAL) : recv (Socket, Byte, Size, 0);
    if (Done <= 0) { return 0; }
    Byte += Done; Size -= (size_t)Done;
  }
  return 1;
}
static EFI_STATUS EFIAPI Submit (EFI_TCG2_PROTOCOL *This, UINT32 Size, UINT8 *Command,
  UINT32 Capacity, UINT8 *Response)
{
  UINT32 Length;
  (void)This;
  if (be32 (Command + 6) == TPM_CC_Create) { Creates++; }
  if (be32 (Command + 6) == TPM_CC_Unseal) { Unseals++; }
  if (!transfer (Command, Size, 1) || !transfer (Response, 10, 0)) { return EFI_DEVICE_ERROR; }
  Length = be32 (Response + 2);
  if (Length < 10 || Length > Capacity || !transfer (Response + 10, Length - 10, 0)) { return EFI_DEVICE_ERROR; }
  return EFI_SUCCESS;
}
static EFI_TCG2_PROTOCOL Protocol = { .GetCapability = Capability, .SubmitCommand = Submit };

int HostConnect (const char *Path) {
  struct sockaddr_un Address = { .sun_family = AF_UNIX };
  struct timeval Timeout = { .tv_sec = 10 };
  if (Socket != -1) { close (Socket); Socket = -1; }
  if (strlen (Path) >= sizeof (Address.sun_path)) { return 0; }
  strcpy (Address.sun_path, Path);
  Socket = socket (AF_UNIX, SOCK_STREAM, 0);
  if (Socket == -1 || connect (Socket, (struct sockaddr *)&Address, sizeof (Address)) != 0) { return 0; }
  setsockopt (Socket, SOL_SOCKET, SO_RCVTIMEO, &Timeout, sizeof (Timeout));
  setsockopt (Socket, SOL_SOCKET, SO_SNDTIMEO, &Timeout, sizeof (Timeout));
  Services.Stall = Stall;
  return 1;
}
void HostDisconnect (void) { if (Socket != -1) { close (Socket); Socket = -1; } }
unsigned HostCreates (void) { return Creates; }
unsigned HostUnseals (void) { return Unseals; }
void HostBanks (UINT32 Value) { Banks = Value; }
UINT64 HostWrap (UINT8 *Record, UINTN *Size, UINT32 *Rc) {
  TPM_OPS_BLOB Blob;
  static CONST UINT8 Secret[] = "TPMOps PCR0 test only";
  EFI_STATUS Status = TpmOpsWrapPcr0 (&Protocol, Secret, sizeof (Secret), &Blob, Rc);
  if (!EFI_ERROR (Status)) { Status = TpmOpsEncode (&Blob, Record, Size); }
  TpmOpsWipe (&Blob, sizeof (Blob));
  return Status;
}
UINT64 HostUnwrap (CONST UINT8 *Record, UINTN Size, UINT8 *Secret, UINTN *SecretSize, UINT32 *Rc) {
  TPM_OPS_BLOB Blob;
  EFI_STATUS Status = TpmOpsDecode (Record, Size, &Blob);
  if (!EFI_ERROR (Status)) { Status = TpmOpsUnwrapPcr0 (&Protocol, &Blob, Secret, SecretSize, Rc); }
  TpmOpsWipe (&Blob, sizeof (Blob));
  return Status;
}
