/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: transient SAF grant lease regressions for manual JVM CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;

public final class TorrentSafGrantLeasesTest {
    private static final String URI = "content://fixture/tree/source";

    private static final class Provider {
        int retained;
        boolean mapped;
        final List<Integer> takes = new ArrayList<>();
        final List<Integer> releases = new ArrayList<>();

        TorrentSafGrantLeases.Token acquire(TorrentSafGrantLeases leases, int modes) {
            return leases.acquire(URI, modes, () -> retained, missing -> {
                takes.add(missing);
                retained |= missing;
            }, () -> mapped, released -> {
                releases.add(released);
                retained &= ~released;
            });
        }
    }

    @Test
    public void ownerFinishingCannotRevokeLaterBorrower() {
        var leases = new TorrentSafGrantLeases();
        var provider = new Provider();
        var creator = provider.acquire(leases, 1);
        var newServiceBorrower = provider.acquire(leases, 1);
        assertNotNull(creator);
        assertNotNull(newServiceBorrower);
        assertEquals(List.of(1), provider.takes);
        assertTrue(leases.retire(creator));
        leases.drain();
        assertTrue(provider.releases.isEmpty());
        assertEquals(1, provider.retained);
        assertTrue(leases.retire(newServiceBorrower));
        leases.drain();
        assertEquals(List.of(1), provider.releases);
    }

    @Test
    public void upgradeReleasesOnlyAcquiredModesAfterAllBorrowers() {
        var leases = new TorrentSafGrantLeases();
        var provider = new Provider();
        provider.retained = 1;
        var reader = provider.acquire(leases, 1);
        var exporter = provider.acquire(leases, 3);
        assertEquals(List.of(2), provider.takes);
        leases.retire(exporter);
        leases.drain();
        assertTrue(provider.releases.isEmpty());
        leases.retire(reader);
        leases.drain();
        assertEquals(List.of(2), provider.releases);
        assertEquals(1, provider.retained);
    }

    @Test
    public void payloadAdoptionTransfersOwnedModesAndDetachDefersRelease() {
        var leases = new TorrentSafGrantLeases();
        var provider = new Provider();
        var creator = provider.acquire(leases, 1);
        provider.mapped = true;
        leases.retire(creator);
        leases.drain();
        assertTrue(provider.releases.isEmpty());
        var borrower = provider.acquire(leases, 1);
        provider.mapped = false;
        assertTrue(leases.deferRelease(URI, 3));
        assertTrue(provider.releases.isEmpty());
        leases.retire(borrower);
        leases.drain();
        assertEquals(List.of(3), provider.releases);
        assertFalse(leases.deferRelease(URI, 3));
    }

    @Test
    public void failedMappingUpgradeDefersOnlyNewModesAndReadoptionKeepsThem() {
        var leases = new TorrentSafGrantLeases();
        var provider = new Provider();
        provider.retained = 1;
        var reader = provider.acquire(leases, 1);
        // Selection obtained write permission but failed to commit.
        provider.retained |= 2;
        assertTrue(leases.deferRelease(URI, 2));
        leases.retire(reader);
        leases.drain();
        assertEquals(List.of(2), provider.releases);
        assertEquals(1, provider.retained);

        var next = provider.acquire(leases, 1);
        assertTrue(leases.deferRelease(URI, 3));
        provider.mapped = true;
        leases.retire(next);
        leases.drain();
        assertEquals(List.of(2), provider.releases);
        assertEquals(1, provider.retained);
    }

    @Test
    public void foreignAndRepeatedRetirementCannotRevokeLiveLease() {
        var leases = new TorrentSafGrantLeases();
        var foreign = new TorrentSafGrantLeases();
        var provider = new Provider();
        var token = provider.acquire(leases, 1);
        assertFalse(foreign.retire(token));
        assertFalse(leases.retire(null));
        leases.drain();
        assertTrue(provider.releases.isEmpty());
        assertTrue(leases.retire(token));
        assertFalse(leases.retire(token));
        leases.drain();
        leases.drain();
        assertEquals(List.of(1), provider.releases);
    }

    @Test
    public void capIncludesPendingRetirementUntilDrainedAndBoundsUri() {
        var leases = new TorrentSafGrantLeases();
        var provider = new Provider();
        var tokens = new ArrayList<TorrentSafGrantLeases.Token>();
        for (int i = 0; i < TorrentSafGrantLeases.MAX_LEASES; i++) {
            var token = provider.acquire(leases, 1);
            assertNotNull(token);
            tokens.add(token);
        }
        assertNull(provider.acquire(leases, 1));
        tokens.forEach(token -> assertTrue(leases.retire(token)));
        assertNull(provider.acquire(leases, 1));
        leases.drain();
        assertNotNull(provider.acquire(leases, 1));
        for (String invalid : new String[] {null, "", "x".repeat(8193)})
            assertNull(leases.acquire(invalid, 1, () -> {
                fail("invalid URI must fail before provider access");
                return 0;
            }, modes -> fail(), () -> false, modes -> fail()));
    }

    @Test
    public void providerFailuresRemainBoundedAndNeverGrantFalseAuthority() {
        var leases = new TorrentSafGrantLeases();
        for (int i = 0; i < TorrentSafGrantLeases.MAX_LEASES + 1; i++)
            assertNull(leases.acquire(URI, 1, () -> 0,
                    modes -> { throw new SecurityException(); }, () -> false, modes -> fail()));
        var token = leases.acquire(URI, 1, () -> 0, modes -> {},
                () -> { throw new SecurityException(); }, modes -> fail());
        assertNotNull(token);
        leases.retire(token);
        leases.drain();
        // Failed reference lookup keeps the framework grant but releases
        // bounded local state, so a broken provider cannot fill the cap.
        var provider = new Provider();
        var recovered = provider.acquire(leases, 1);
        assertNotNull(recovered);
        leases.retire(recovered);
        leases.drain();
        assertEquals(List.of(1), provider.releases);
    }

    @Test
    public void failedFrameworkReleaseDropsBoundedLocalCleanupWithoutRetry() {
        var leases = new TorrentSafGrantLeases();
        int[] attempts = {0};
        for (int i = 0; i < TorrentSafGrantLeases.MAX_LEASES + 1; i++) {
            var token = leases.acquire(URI, 1, () -> 0, modes -> {},
                    () -> false, modes -> {
                        attempts[0]++;
                        throw new SecurityException();
                    });
            assertNotNull(token);
            assertTrue(leases.retire(token));
            leases.drain();
        }
        leases.drain();
        assertEquals(TorrentSafGrantLeases.MAX_LEASES + 1, attempts[0]);
    }
}
