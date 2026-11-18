/* Host tests for the same parser linked into TCGOpalCheck.efi. */
#include "Discovery.h"
#include "AtaSecurity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
SetLength (UINT8 *Buffer, UINT32 Total)
{
  UINT32 Length = Total - 4;
  Buffer[0] = (UINT8)(Length >> 24);
  Buffer[1] = (UINT8)(Length >> 16);
  Buffer[2] = (UINT8)(Length >> 8);
  Buffer[3] = (UINT8)Length;
}

int
main (void)
{
  UINT8 Buffer[256] = {0};
  OPAL_DISCOVERY_INFO Info;
  ATA_PASSWORD_STATUS Ata;
  unsigned int State = 12345;
  unsigned int I;
  unsigned int J;

  assert (!ParseDiscovery (NULL, 0, &Info));
  assert (!ParseDiscovery (Buffer, sizeof (Buffer), NULL));
  assert (!ParseDiscovery (Buffer, 47, &Info));
  assert (!ParseDiscovery (Buffer, sizeof (Buffer), &Info));

  SetLength (Buffer, 48);
  assert (ParseDiscovery (Buffer, sizeof (Buffer), &Info));
  assert (!Info.Opal1 && !Info.Opal2 && !Info.HasLocking);

  /* OPAL 2 descriptor followed by a Locking descriptor. */
  Buffer[48] = 2; Buffer[49] = 3; Buffer[50] = 0x10; Buffer[51] = 16;
  Buffer[68] = 0; Buffer[69] = 2; Buffer[70] = 0x10; Buffer[71] = 12;
  Buffer[72] = 0x3F;
  SetLength (Buffer, 84);
  assert (ParseDiscovery (Buffer, 84, &Info));
  assert (Info.Opal2 && !Info.Opal1 && Info.HasLocking);
  assert (Info.LockingFlags == 0x3F && Info.FeatureCount == 2);
  assert (!ParseDiscovery (Buffer, 83, &Info));

  Buffer[49] = 0; /* OPAL 1 */
  assert (ParseDiscovery (Buffer, 84, &Info) && Info.Opal1 && !Info.Opal2);
  Buffer[48] = 3; Buffer[49] = 1; /* Opalite, not full OPAL */
  assert (ParseDiscovery (Buffer, 84, &Info) && Info.Opalite && !Info.Opal1 && !Info.Opal2);
  Buffer[49] = 2; /* Pyrite, not OPAL */
  assert (ParseDiscovery (Buffer, 84, &Info) && Info.Pyrite1 && !Info.Opal2);
  Buffer[49] = 3;
  assert (ParseDiscovery (Buffer, 84, &Info) && Info.Pyrite2);
  Buffer[48] = 0xFE; Buffer[49] = 0xFE; /* unknown features are skipped */
  assert (ParseDiscovery (Buffer, 84, &Info) && !Info.Opal1 && !Info.Opal2);

  Buffer[48] = 2; Buffer[49] = 3; Buffer[51] = 255;
  assert (!ParseDiscovery (Buffer, 84, &Info)); /* payload overrun */
  Buffer[51] = 15; SetLength (Buffer, 67);
  assert (!ParseDiscovery (Buffer, 67, &Info)); /* short known descriptor */
  SetLength (Buffer, 49);
  assert (!ParseDiscovery (Buffer, 49, &Info)); /* partial descriptor header */
  memset (Buffer, 0xFF, 4);
  assert (!ParseDiscovery (Buffer, sizeof (Buffer), &Info)); /* length overflow */

  /* Reject duplicate Locking descriptors instead of reporting ambiguous bits. */
  memset (Buffer, 0, sizeof (Buffer));
  SetLength (Buffer, 80);
  Buffer[49] = 2; Buffer[51] = 12;
  Buffer[65] = 2; Buffer[67] = 12;
  assert (!ParseDiscovery (Buffer, 80, &Info));

  /* EF status is a separate 16-byte, big-endian SAT response. */
  memset (Buffer, 0, sizeof (Buffer));
  Buffer[1] = 14;
  Buffer[2] = 0x12; Buffer[3] = 0x34;
  Buffer[4] = 0x56; Buffer[5] = 0x78;
  Buffer[6] = 0xAB; Buffer[7] = 0xCD;
  Buffer[8] = 1;
  Buffer[9] = 0x3F;
  assert (ParseAtaPasswordStatus (Buffer, 16, &Ata));
  assert (Ata.EraseTime == 0x1234 && Ata.EnhancedEraseTime == 0x5678);
  assert (Ata.MasterPasswordIdentifier == 0xABCD && Ata.MaximumSecurity);
  assert (Ata.Supported && Ata.Enabled && Ata.Locked && Ata.Frozen);
  assert (Ata.AttemptsExceeded && Ata.EnhancedEraseSupported);
  assert (!ParseAtaPasswordStatus (NULL, 16, &Ata));
  assert (!ParseAtaPasswordStatus (Buffer, 16, NULL));
  assert (!ParseAtaPasswordStatus (Buffer, 15, &Ata));
  Buffer[1] = 0;
  assert (!ParseAtaPasswordStatus (Buffer, 16, &Ata)); /* zero-filled response */
  Buffer[0] = 14; /* little-endian length is not a valid wire response */
  assert (!ParseAtaPasswordStatus (Buffer, 16, &Ata));
  Buffer[0] = 0; Buffer[1] = 15;
  assert (!ParseAtaPasswordStatus (Buffer, 16, &Ata)); /* unknown layout */
  Buffer[1] = 14; Buffer[8] = 0;
  for (I = 0; I < 6; I++) {
    Buffer[9] = (UINT8)(1U << I);
    assert (ParseAtaPasswordStatus (Buffer, 16, &Ata));
    assert (Ata.Supported == (I == 0));
    assert (Ata.Enabled == (I == 1));
    assert (Ata.Locked == (I == 2));
    assert (Ata.Frozen == (I == 3));
    assert (Ata.AttemptsExceeded == (I == 4));
    assert (Ata.EnhancedEraseSupported == (I == 5));
  }

  /* Deterministic malformed-device corpus, run under ASan/UBSan. */
  for (I = 0; I < 10000; I++) {
    for (J = 0; J < sizeof (Buffer); J++) {
      State = State * 1664525U + 1013904223U;
      Buffer[J] = (UINT8)(State >> 24);
    }
    if ((I & 1) == 0) {
      SetLength (Buffer, 48 + I % 209);
    }
    (void)ParseDiscovery (Buffer, I % 257, &Info);
    (void)ParseAtaPasswordStatus (Buffer, I % 257, &Ata);
  }

  puts ("PASS: OPAL / EF status parser cases and 10000 malformed-input checks each.");
  return 0;
}
