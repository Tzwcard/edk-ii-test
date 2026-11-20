/* Mock NVMe transport only. No firmware or real disk access. */
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
// Include implementation to exercise private parsers without exporting them.
#include "../MyPkg/Application/NVMe/InfoFetch.c"

EFI_GUID gEfiNvmExpressPassThruProtocolGuid = { 1, 0, 0, { 0 } };
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
static EFI_NVM_EXPRESS_PASS_THRU_MODE Mode;
static EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL Protocol;
static unsigned Calls, OpalCalls, EfCalls, Outstanding, AllocationCalls, FailAllocation;
static UINT8 Advertised = 3;
static BOOLEAN SecuritySupported = TRUE;
static unsigned Fault;
static EFI_HANDLE TestHandle = (EFI_HANDLE)(UINTN)1;

enum {
  Normal, IdentifyCompletion, ListCompletion, OpalCompletion, EfCompletion,
  ListOversize, EfShort, ListMalformed, OpalMalformed, EfMalformed,
  LargeDiscovery, HugeDiscovery, ChangingDiscovery, ShortIdentify, Warning,
  MissingProtocol, MissingMode, MissingPassThru, ListTimeout, OpalTimeout,
  EmptyList, ZeroTransfer, ShortDiscovery, OpalShortTransfer, TruncatedFeature
};

VOID *EFIAPI ZeroMem (VOID *Data, UINTN Size) { return memset (Data, 0, Size); }
VOID *EFIAPI CopyMem (VOID *To, CONST VOID *From, UINTN Size) { return memcpy (To, From, Size); }
UINTN EFIAPI Print (CONST CHAR16 *Format, ...) { (void)Format; return 0; }
VOID *EFIAPI AllocateAlignedPages (UINTN Pages, UINTN Alignment) {
  VOID *Buffer;
  AllocationCalls++;
  assert (Pages != 0 && Alignment >= EFI_PAGE_SIZE && (Alignment & (Alignment - 1)) == 0);
  if (AllocationCalls == FailAllocation) { return NULL; }
  // aligned_alloc requires size to be a multiple of alignment.
  Buffer = aligned_alloc (Alignment, ALIGN_VALUE (Pages * EFI_PAGE_SIZE, Alignment));
  assert (Buffer != NULL); Outstanding++; return Buffer;
}
VOID EFIAPI FreeAlignedPages (VOID *Buffer, UINTN Pages) {
  assert (Pages != 0 && Outstanding != 0); Outstanding--; free (Buffer);
}

static EFI_STATUS EFIAPI GetProtocol (EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface) {
  assert (Handle == TestHandle && Guid == &gEfiNvmExpressPassThruProtocolGuid);
  if (Fault == MissingProtocol) { return EFI_NOT_FOUND; }
  *Interface = &Protocol; return EFI_SUCCESS;
}

static UINT32 Discovery (UINT8 *Buffer, UINT8 LockingFlags) {
  UINT32 Offset = sizeof (TCG_LEVEL0_DISCOVERY_HEADER);
  PutBe16 (Buffer + Offset, TCG_FEATURE_LOCKING);
  Buffer[Offset + 2] = 0x30; Buffer[Offset + 3] = 12;
  Buffer[Offset + 4] = LockingFlags;
  Offset += 16;
  PutBe16 (Buffer + Offset, TCG_FEATURE_OPAL_SSC_V2_0_0);
  Buffer[Offset + 2] = 0x10; Buffer[Offset + 3] = (UINT8)(sizeof (OPAL_SSCV2_FEATURE_DESCRIPTOR) - 4);
  Offset += sizeof (OPAL_SSCV2_FEATURE_DESCRIPTOR);
  PutBe32 (Buffer, Offset - 4);
  return Offset;
}

static EFI_STATUS EFIAPI PassThru (
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *This, UINT32 Namespace,
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET *Packet, EFI_EVENT Event
  )
{
  UINT8 *Buffer = Packet->TransferBuffer;
  UINT8 Secp;
  UINT32 Specific;
  UINT32 Requested = Packet->TransferLength;
  UINT32 Offset;
  assert (This == &Protocol && Namespace == 0 && Event == NULL);
  assert (Packet->QueueType == NVME_ADMIN_QUEUE && Packet->CommandTimeout == NVME_COMMAND_TIMEOUT);
  assert (((UINTN)Buffer % (Mode.IoAlign > EFI_PAGE_SIZE ? Mode.IoAlign : EFI_PAGE_SIZE)) == 0);
  assert (Packet->NvmeCmd->Nsid == 0 && Requested != 0);
  Calls++;
  if (Packet->NvmeCmd->Cdw0.Opcode == NVME_ADMIN_IDENTIFY_CMD) {
    assert (Requested == 4096 && Packet->NvmeCmd->Cdw10 == 1 && Packet->NvmeCmd->Flags == CDW10_VALID);
    memcpy (Buffer + 4, "SERIAL-TEST", 11); memcpy (Buffer + 24, "MODEL-TEST", 10);
    memcpy (Buffer + 64, "FW01", 4);
    PutLe16 (Buffer + 256, SecuritySupported ? 1 : 0);
    if (Fault == IdentifyCompletion) { Packet->NvmeCompletion->DW3 = 1U << 17; }
    if (Fault == ShortIdentify) { Packet->TransferLength--; }
    if (Fault == Warning) { return EFI_WARN_UNKNOWN_GLYPH; }
    return EFI_SUCCESS;
  }
  // No Security Send or other mutating opcode is permitted by this mock.
  assert (Packet->NvmeCmd->Cdw0.Opcode == NVME_ADMIN_SECURITY_RECEIVE_CMD);
  assert (Packet->NvmeCmd->Flags == (CDW10_VALID | CDW11_VALID));
  assert (Packet->NvmeCmd->Cdw11 == Requested);
  Secp = (UINT8)(Packet->NvmeCmd->Cdw10 >> 24);
  Specific = (Packet->NvmeCmd->Cdw10 >> 8) & 0xFFFF;
  if (Secp == TCG_SECURITY_PROTOCOL_INFO) {
    assert (Specific == 0 && Requested == 512);
    if (Advertised & 1) { Buffer[8 + Buffer[7]++] = 1; }
    if (Advertised & 2) { Buffer[8 + Buffer[7]++] = 0xEF; }
    if (Fault == ListCompletion) { Packet->NvmeCompletion->DW3 = 2U << 25; }
    if (Fault == ListOversize) { Packet->TransferLength = 4097; }
    if (Fault == ListMalformed) { PutBe16 (Buffer + 6, 511); }
    if (Fault == ListTimeout) { return EFI_TIMEOUT; }
    if (Fault == EmptyList) { Buffer[7] = 0; }
    if (Fault == ZeroTransfer) { Packet->TransferLength = 0; }
    return EFI_SUCCESS;
  }
  if (Secp == TCG_OPAL_SECURITY_PROTOCOL_1) {
    OpalCalls++;
    // Direct NVMe SPSP is host numeric 1; no extra SwapBytes16.
    assert (Specific == 1 && Requested >= 4096 && Requested <= 65536);
    Offset = Discovery (Buffer, BIT0 | BIT1 | BIT3); // Enabled but NOT locked.
    if (Fault == OpalCompletion) { Packet->NvmeCompletion->DW3 = 1U << 17; }
    if (Fault == OpalTimeout) { return EFI_TIMEOUT; }
    if (Fault == OpalMalformed) { PutBe32 (Buffer, 2); }
    if (Fault == TruncatedFeature) { PutBe32 (Buffer, sizeof (TCG_LEVEL0_DISCOVERY_HEADER) - 1); }
    if (Fault == ShortDiscovery) { Packet->TransferLength = 20; }
    if (Fault == OpalShortTransfer) { Packet->TransferLength = Offset - 1; }
    if (Fault == HugeDiscovery) { PutBe32 (Buffer, 0xFFFFFFFFU); }
    if (Fault == ChangingDiscovery) { PutBe32 (Buffer, Requested + 508); }
    if (Fault == LargeDiscovery) {
      PutBe32 (Buffer, 8192 - 4);
      // Zero-length unknown descriptors fill the remainder (multiple of 4).
      assert (Offset % 4 == 0);
    }
    return EFI_SUCCESS;
  }
  assert (Secp == TCG_SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASS && Specific == 0 && Requested == 16);
  EfCalls++;
  PutBe16 (Buffer, 14); PutBe16 (Buffer + 2, 0x0102); PutBe16 (Buffer + 4, 0x0304);
  PutBe16 (Buffer + 6, 0xFFFE); Buffer[8] = 1; Buffer[9] = 0x3F;
  if (Fault == EfCompletion) { Packet->NvmeCompletion->DW3 = 1U << 17; }
  if (Fault == EfShort) { Packet->TransferLength = 15; }
  if (Fault == EfMalformed) { Buffer[0] = 1; }
  return EFI_SUCCESS;
}

static void ResetMock (unsigned NewFault) {
  assert (Outstanding == 0);
  memset (&Mode, 0, sizeof (Mode)); memset (&Protocol, 0, sizeof (Protocol));
  Mode.IoAlign = 1; Protocol.Mode = &Mode; Protocol.PassThru = PassThru;
  Services.HandleProtocol = GetProtocol;
  Fault = NewFault; Calls = OpalCalls = EfCalls = AllocationCalls = FailAllocation = 0;
  SecuritySupported = TRUE; Advertised = 3;
  if (Fault == MissingMode) { Protocol.Mode = NULL; }
  if (Fault == MissingPassThru) { Protocol.PassThru = NULL; }
}

static NVME_DRIVE_INFO Fetch (EFI_STATUS Expected) {
  NVME_DRIVE_INFO Info;
  memset (&Info, 0xA5, sizeof (Info));
  assert (NVMeGetControllerInfo (&Info, TestHandle) == Expected);
  assert (Outstanding == 0 && Info.LBA == 0);
  return Info;
}

static void TransportTests (void) {
  NVME_DRIVE_INFO Info;
  unsigned Test;
  ResetMock (Normal);
  assert (NVMeGetControllerInfo (NULL, TestHandle) == EFI_INVALID_PARAMETER && Calls == 0);
  assert (NVMeGetControllerInfo (&Info, NULL) == EFI_INVALID_PARAMETER && Info.Ok == 0 && Calls == 0);
  Info = Fetch (EFI_SUCCESS);
  assert (Info.Ok == BIT0 && Calls == 4 && strcmp (Info.Serial, "SERIAL-TEST") == 0);
  assert (Info.IsEnabled == 3 && Info.IsLocked == NVME_INFO_ARMED_ATA_SECURITY);
  assert (Info.AtaFrozen);
  assert (Info.OpalSSCVersionFlag == NVME_INFO_SSC_VERSION_FLAG_V2);

  ResetMock (Normal); SecuritySupported = FALSE;
  Info = Fetch (EFI_SUCCESS);
  assert (Info.Ok == BIT0 && Calls == 1);
  ResetMock (Normal); Advertised = 2;
  Info = Fetch (EFI_SUCCESS);
  assert (OpalCalls == 0 && EfCalls == 1 && Info.Ok == BIT0);
  ResetMock (EmptyList);
  Info = Fetch (EFI_SUCCESS); assert (Info.Ok == BIT0 && Calls == 2 && Info.Ability == 0);
  ResetMock (Normal); Mode.IoAlign = 3;
  Info = Fetch (EFI_UNSUPPORTED); assert (Calls == 0 && AllocationCalls == 0 && Info.Ok == 0);
  ResetMock (Normal); Mode.IoAlign = 8192;
  Info = Fetch (EFI_SUCCESS); assert (Info.Ok == BIT0);
  ResetMock (Normal); FailAllocation = 1;
  Info = Fetch (EFI_OUT_OF_RESOURCES); assert (Calls == 0 && Info.Ok == 0);

  for (Test = IdentifyCompletion; Test <= TruncatedFeature; Test++) {
    EFI_STATUS Expected = EFI_COMPROMISED_DATA;
    ResetMock (Test);
    switch (Test) {
      case IdentifyCompletion: case ListCompletion: case OpalCompletion: case EfCompletion: case Warning:
        Expected = EFI_DEVICE_ERROR; break;
      case MissingProtocol: Expected = EFI_NOT_FOUND; break;
      case MissingMode: case MissingPassThru: Expected = EFI_UNSUPPORTED; break;
      case ListTimeout: case OpalTimeout: Expected = EFI_TIMEOUT; break;
      case EmptyList: Expected = EFI_SUCCESS; break;
      default: break;
    }
    Info = Fetch (Expected);
    if (Test == OpalCompletion || Test == OpalMalformed || Test == OpalTimeout || Test == HugeDiscovery ||
        Test == LargeDiscovery || Test == ChangingDiscovery || Test == ShortDiscovery || Test == OpalShortTransfer || Test == TruncatedFeature) {
      assert (EfCalls == 1 && Info.Ok == 0);
      assert (Info.OpalSSCVersionFlag == 0);
      assert (Info.SecurityFeature == NVME_INFO_SEC_FEATURE_ATA_SECURITY);
    }
    if (Test == EfShort || Test == EfMalformed || Test == EfCompletion) {
      assert (Info.Ok == 0 && !Info.AtaFrozen && Info.OpalSSCVersionFlag == NVME_INFO_SSC_VERSION_FLAG_V2);
    }
    if (Test == ListMalformed || Test == ListOversize || Test == ListTimeout || Test == ListCompletion || Test == ZeroTransfer) {
      assert (OpalCalls == 0 && EfCalls == 0 && Info.Ok == 0);
      assert (Info.Ability == 0);
    }
    if (Test == ChangingDiscovery || Test == LargeDiscovery) { assert (OpalCalls == 1 && AllocationCalls == 1); }
  }
  ResetMock (Normal);
  {
    UINT8 Buffer[4096]; UINT32 Length = 4097;
    assert (NVMeSecurityReceive (&Protocol, 0, 0, Buffer, &Length) == EFI_INVALID_PARAMETER);
    assert (Length == 0 && Calls == 0);
  }
}

static void ParserTests (void) {
  UINT8 Buffer[256] = { 0 };
  NVME_DRIVE_INFO Info = { 0 };
  NVME_DRIVE_INFO Before;
  UINT32 Size;
  UINT32 Flags;
  UINT32 End;
  // Every EF low flag combination; unsupported bits must not leak state.
  for (Flags = 0; Flags < 64; Flags++) {
    memset (Buffer, 0, sizeof (Buffer)); PutBe16 (Buffer, 14); Buffer[8] = 1; Buffer[9] = (UINT8)Flags;
    assert (NVMeATASecurityParse (&Info, Buffer, 16));
    assert (Info.AtaFrozen == ((Flags & BIT0) != 0 && (Flags & BIT3) != 0));
    assert (!!(Info.IsEnabled & NVME_INFO_ENABLED_ATA_SECURITY) == ((Flags & BIT0) != 0 && (Flags & BIT1) != 0));
    assert (!!(Info.IsLocked & NVME_INFO_ARMED_ATA_SECURITY) == ((Flags & BIT0) != 0 && (Flags & BIT2) != 0));
  }
  Before = Info; Buffer[0] = 1;
  assert (!NVMeATASecurityParse (&Info, Buffer, 16) && memcmp (&Info, &Before, sizeof (Info)) == 0);
  Buffer[0] = 0;
  for (Size = 0; Size < 16; Size++) { assert (!NVMeATASecurityParse (&Info, Buffer, Size)); }
  memset (Buffer, 0, sizeof (Buffer));
  End = Discovery (Buffer, BIT0 | BIT1 | BIT3);
  assert (NVMeOPALDiscoveryParse (&Info, Buffer, End));
  assert ((Info.SecurityFeature & NVME_INFO_SEC_FEATURE_OPAL_SSC) && !(Info.IsLocked & NVME_INFO_ARMED_OPAL_SSC));
  // Preserve user's two-flag predicate even WITHOUT an OPAL SSC descriptor.
  PutBe32 (Buffer, sizeof (TCG_LEVEL0_DISCOVERY_HEADER) + 16 - 4);
  assert (NVMeOPALDiscoveryParse (&Info, Buffer, End));
  assert ((Info.SecurityFeature & NVME_INFO_SEC_FEATURE_OPAL_SSC) && Info.OpalSSCVersionFlag == 0);
  Before = Info;
  // Valid locking descriptor followed by 1..3 trailing bytes: transactional failure.
  for (Size = 1; Size <= 3; Size++) {
    PutBe32 (Buffer, sizeof (TCG_LEVEL0_DISCOVERY_HEADER) + 16 + Size - 4);
    assert (!NVMeOPALDiscoveryParse (&Info, Buffer, sizeof (TCG_LEVEL0_DISCOVERY_HEADER) + 16 + Size));
    assert (memcmp (&Info, &Before, sizeof (Info)) == 0);
  }
  // A declared descriptor length extending beyond the response is rejected.
  memset (Buffer, 0, sizeof (Buffer)); End = Discovery (Buffer, BIT2);
  Buffer[sizeof (TCG_LEVEL0_DISCOVERY_HEADER) + 3] = 255;
  assert (!NVMeOPALDiscoveryParse (&Info, Buffer, End));
  assert (memcmp (&Info, &Before, sizeof (Info)) == 0);
  // Complete Discovery without a Locking descriptor must not claim a known state.
  memset (Buffer, 0, sizeof (Buffer));
  PutBe32 (Buffer, sizeof (TCG_LEVEL0_DISCOVERY_HEADER) - 4);
  assert (NVMeOPALDiscoveryParse (&Info, Buffer, sizeof (TCG_LEVEL0_DISCOVERY_HEADER)));
  assert (!(Info.IsEnabled & NVME_INFO_ENABLED_OPAL_SSC));
  // Exact-sized heap allocations expose any access beyond declared parser Size.
  for (Size = 0; Size < 10000; Size++) {
    UINT32 Length = Size % 256;
    UINT8 *Fuzz = malloc (Length == 0 ? 1 : Length);
    static UINT32 State = 12345;
    UINT32 Index;
    for (Index = 0; Index < Length; Index++) {
      State = State * 1664525U + 1013904223U; Fuzz[Index] = (UINT8)(State >> 24);
    }
    if (Length >= sizeof (TCG_LEVEL0_DISCOVERY_HEADER) && (Size & 1)) { PutBe32 (Fuzz, Length - 4); }
    Before = Info;
    if (!NVMeOPALDiscoveryParse (&Info, Fuzz, Length)) { assert (memcmp (&Info, &Before, sizeof (Info)) == 0); }
    Before = Info;
    if (!NVMeATASecurityParse (&Info, Fuzz, Length)) { assert (memcmp (&Info, &Before, sizeof (Info)) == 0); }
    Before = Info;
    if (!NVMeTCGSecurityInfoParse (&Info, Fuzz, Length)) { assert (memcmp (&Info, &Before, sizeof (Info)) == 0); }
    free (Fuzz);
  }
}

int main (void) {
  TransportTests (); ParserTests ();
  puts ("PASS: Identify + protocol/OPAL/EF requests, NVMe completion, lengths, alignment and error propagation.");
  puts ("PASS: EF flags, locked bit, preserved two-flag predicate, oversized discovery rejected without reallocation.");
  puts ("PASS: transactional parsers and 10000 exact-sized malformed records under ASan/UBSan; no hardware access.");
  return 0;
}
