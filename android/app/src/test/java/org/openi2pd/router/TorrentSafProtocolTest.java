/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentSafProtocolTest.
 *
 * JVM-only contracts of the SAF broker protocol: caller binding, one-shot
 * export capabilities, one-shot generation retirement, bounded admission,
 * handle ownership, range overflow, path bounds and the watchdog arming order.
 * It also pins the client bind-retry budget, which is framework-free. No
 * provider, Binder stub or device is required. Generated stubs, provider IO and
 * process death are covered by the instrumentation suite or manual device CI.
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import org.junit.Test;

public class TorrentSafProtocolTest {
    private static final int APP_UID = 10123;
    private static final TorrentSafStorageService.Caller OWNER =
            new TorrentSafStorageService.Caller(APP_UID, 4242);
    private static final TorrentSafStorageService.Caller SIBLING =
            new TorrentSafStorageService.Caller(APP_UID, 4343);
    private static final TorrentSafStorageService.Caller FOREIGN_UID =
            new TorrentSafStorageService.Caller(APP_UID + 1, 4242);
    private static final String URI = "content://org.openi2pd.router.test.saf/document/payload";

    @Test
    public void wireContractIsPinnedToVersionTwo() {
        assertEquals(2, TorrentSafStorageService.API_VERSION);
        // Admission, handle and root bounds are part of the wire contract.
        assertEquals(8, TorrentSafStorageService.MAX_OPERATIONS);
        assertEquals(4096, TorrentSafStorageService.MAX_HANDLES);
        assertEquals(128, TorrentSafStorageService.MAX_ROOTS);
        assertEquals(8, TorrentSafStorageService.MAX_EXPORTS);
        // One export cannot live longer than one truncated open attempt.
        assertTrue(TorrentSafStorageService.EXPORT_TTL_MS > 0);
        // A provider is cancelled first and fenced only afterwards.
        assertTrue(TorrentSafStorageService.CANCEL_DEADLINE_MS
                < TorrentSafStorageService.KILL_DEADLINE_MS);
    }

    @Test
    public void preparedExportTokenIsSingleUse() {
        TorrentSafStorageService.ExportGrants grants = new TorrentSafStorageService.ExportGrants();
        assertTrue(grants.prepare(77, OWNER, URI, 16384, "doc:1", 1000));
        assertEquals(1, grants.size());
        TorrentSafStorageService.ExportGrants.Grant grant = grants.consume(77, OWNER, 1000);
        assertNotNull(grant);
        assertEquals(URI, grant.uri());
        assertEquals(16384, grant.length());
        assertEquals("doc:1", grant.documentId());
        assertEquals(OWNER, grant.owner());
        // The capability is consumed exactly once; a replay opens nothing.
        assertNull(grants.consume(77, OWNER, 1001));
        assertEquals(0, grants.size());
    }

    @Test
    public void exportTokenIsBoundToTheRegisteringProcess() {
        TorrentSafStorageService.ExportGrants grants = new TorrentSafStorageService.ExportGrants();
        assertTrue(grants.prepare(11, OWNER, URI, 4096, "doc:1", 0));
        // A sibling process of the same UID cannot spend the capability, and the
        // refusal does not burn the owner's token.
        assertNull(grants.consume(11, SIBLING, 0));
        assertNull(grants.consume(11, FOREIGN_UID, 0));
        assertEquals(1, grants.size());
        assertNotNull(grants.consume(11, OWNER, 0));
    }

    @Test
    public void expiredExportTokenIsRefused() {
        TorrentSafStorageService.ExportGrants grants = new TorrentSafStorageService.ExportGrants();
        assertTrue(grants.prepare(5, OWNER, URI, 1024, "doc:1", 0));
        long ttl = TorrentSafStorageService.EXPORT_TTL_MS;
        assertNotNull(grants.consume(5, OWNER, ttl - 1));
        assertTrue(grants.prepare(6, OWNER, URI, 1024, "doc:1", 0));
        // Expiry prunes on access: the token is gone before the identity check.
        assertNull(grants.consume(6, OWNER, ttl));
        assertEquals(0, grants.size());
    }

    @Test
    public void exportTokensAreBoundedAndValidated() {
        TorrentSafStorageService.ExportGrants grants = new TorrentSafStorageService.ExportGrants();
        for (int i = 1; i <= TorrentSafStorageService.MAX_EXPORTS; ++i)
            assertTrue("token " + i, grants.prepare(i, OWNER, URI, 1024, "doc:1", 0));
        assertFalse(grants.prepare(99, OWNER, URI, 1024, "doc:1", 0));
        assertFalse(grants.prepare(1, OWNER, URI, 1024, "doc:1", 0));
        // Invalid arguments never allocate a capability or truncate anything.
        assertFalse(grants.prepare(0, OWNER, URI, 1024, "doc:1", 0));
        assertFalse(grants.prepare(101, OWNER, URI, 0, "doc:1", 0));
        assertFalse(grants.prepare(
                102, OWNER, URI, TorrentSafStorageService.MAX_EXPORT + 1, "doc:1", 0));
        assertFalse(grants.prepare(103, OWNER, null, 1024, "doc:1", 0));
        assertFalse(grants.prepare(104, OWNER, URI, 1024, "", 0));
        assertFalse(grants.prepare(105, OWNER, URI, 1024, null, 0));
        assertFalse(grants.prepare(106, null, URI, 1024, "doc:1", 0));
        assertEquals(TorrentSafStorageService.MAX_EXPORTS, grants.size());
    }

    @Test
    public void brokerGenerationIsNonZeroAndRandomised() {
        assertTrue(new TorrentSafStorageService.BrokerIdentity(0).generation() > 0);
        assertTrue(new TorrentSafStorageService.BrokerIdentity(-1).generation() > 0);
        assertNotEquals(new TorrentSafStorageService.BrokerIdentity(1).generation(),
                new TorrentSafStorageService.BrokerIdentity(2).generation());
        assertTrue(new TorrentSafStorageService.BrokerIdentity(Long.MIN_VALUE).generation() > 0);
    }

    @Test
    public void retirementIsOneShotAndGenerationFenced() {
        TorrentSafStorageService.BrokerIdentity identity =
                new TorrentSafStorageService.BrokerIdentity(0x1234);
        long generation = identity.generation();
        assertFalse(identity.retired());
        // A stale, zero or unrelated generation cannot fence the process.
        assertFalse(identity.claimRetirement(0));
        assertFalse(identity.claimRetirement(generation + 1));
        assertFalse(identity.claimRetirement(-generation));
        assertFalse(identity.retired());
        assertTrue(identity.claimRetirement(generation));
        assertTrue(identity.retired());
        // A duplicated retirement frame is ignored, so a delayed oneway call
        // cannot fence a replacement broker that reused the same object.
        assertFalse(identity.claimRetirement(generation));
        assertFalse(identity.firstRetirement());
    }

    @Test
    public void admissionIsBounded() {
        TorrentSafStorageService.Admission<String> admission =
                new TorrentSafStorageService.Admission<>();
        for (int i = 1; i <= TorrentSafStorageService.MAX_OPERATIONS; ++i)
            assertNotNull("slot " + i, admission.admit(i, OWNER, "value-" + i));
        assertEquals(TorrentSafStorageService.MAX_OPERATIONS, admission.size());
        // Over the limit the caller gets a typed fallback instead of an
        // exception, and nothing is evicted to make room.
        assertNull(admission.admit(99, OWNER, "overflow"));
        assertEquals(TorrentSafStorageService.MAX_OPERATIONS, admission.size());
        assertNull(admission.release(99));
        assertNotNull(admission.release(1));
        assertNull(admission.find(1));
        assertNotNull(admission.admit(99, OWNER, "reused slot"));
    }

    @Test
    public void admissionRejectsDuplicatesAndForeignOwners() {
        TorrentSafStorageService.Admission<String> admission =
                new TorrentSafStorageService.Admission<>();
        assertNull(admission.admit(0, OWNER, "zero"));
        assertNull(admission.admit(-1, OWNER, "negative"));
        assertNull(admission.admit(1, null, "no owner"));
        assertNotNull(admission.admit(1, OWNER, "first"));
        assertNull(admission.admit(1, SIBLING, "duplicate"));
        TorrentSafStorageService.Admission.Slot<String> slot = admission.find(1);
        assertNotNull(slot);
        assertEquals(OWNER, slot.owner());
        assertEquals("first", slot.value());
        assertTrue(admission.registered(1, "first"));
        assertFalse(admission.registered(1, "second"));
        assertFalse(admission.registered(2, "first"));
    }

    @Test
    public void handleOwnershipNeedsTheExactUidAndPid() {
        TorrentSafStorageService.Caller same = new TorrentSafStorageService.Caller(APP_UID, 4242);
        assertTrue(TorrentSafStorageService.sameOwner(OWNER, same));
        assertFalse(TorrentSafStorageService.sameOwner(OWNER, SIBLING));
        assertFalse(TorrentSafStorageService.sameOwner(OWNER, FOREIGN_UID));
        assertFalse(TorrentSafStorageService.sameOwner(null, OWNER));
        assertFalse(TorrentSafStorageService.sameOwner(OWNER, null));
    }

    @Test
    public void transfersNeverWrapOrExceedTheChunkBound() {
        assertTrue(TorrentSafStorageService.validRange(16384, 0, 16384));
        assertFalse(TorrentSafStorageService.validRange(16384, 1, 16384));
        assertFalse(TorrentSafStorageService.validRange(16384, 0, 16385));
        assertFalse(TorrentSafStorageService.validRange(Long.MAX_VALUE, Long.MAX_VALUE, 1));
        assertFalse(TorrentSafStorageService.validRange(0, 0, 1));
        assertFalse(TorrentSafStorageService.validRange(-1, 0, 0));
    }

    @Test
    public void relativePathsStayBounded() {
        assertTrue(TorrentSafStorageService.validPath("torrent/part-00001.bin"));
        assertTrue(TorrentSafStorageService.validPath("torrent/sub/part-00002.bin"));
        assertTrue(TorrentSafStorageService.validPath("torrent/empty.bin"));
        assertFalse(TorrentSafStorageService.validPath(null));
        assertFalse(TorrentSafStorageService.validPath(""));
        assertFalse(TorrentSafStorageService.validPath("/absolute"));
        assertFalse(TorrentSafStorageService.validPath("/torrent/part-00001.bin"));
        assertFalse(TorrentSafStorageService.validPath("a/../b"));
        assertFalse(TorrentSafStorageService.validPath("torrent/../part-00001.bin"));
        assertFalse(TorrentSafStorageService.validPath("torrent/./part-00001.bin"));
        assertFalse(TorrentSafStorageService.validPath("a//b"));
        assertFalse(TorrentSafStorageService.validPath("torrent/"));
        assertFalse(TorrentSafStorageService.validPath("a/\u0000b"));
        assertFalse(TorrentSafStorageService.validPath("torrent/part-00001.bin\n"));
        assertFalse(TorrentSafStorageService.validPath("torrent/part-00001.bin\t"));
        assertFalse(TorrentSafStorageService.validPath(repeat("x", 256)));
        assertFalse(TorrentSafStorageService.validPath(repeat("a/", 33) + "file"));
        assertFalse(TorrentSafStorageService.validPath(repeat("d/", 5000)));
    }

    @Test
    public void providerDeadlinesStartOnlyWhileHoldingTheProvider() {
        // An operation that is still queued behind ioLock has not touched a
        // document; charging it would let one stalled provider abort or fence
        // every later caller. The stages therefore arm only after the lock.
        assertFalse(TorrentSafStorageService.providerDeadlineApplies(false, true));
        assertFalse(TorrentSafStorageService.providerDeadlineApplies(true, false));
        assertFalse(TorrentSafStorageService.providerDeadlineApplies(false, false));
        assertTrue(TorrentSafStorageService.providerDeadlineApplies(true, true));
        assertEquals(15_000, TorrentSafStorageService.CANCEL_DEADLINE_MS);
        assertEquals(30_000, TorrentSafStorageService.KILL_DEADLINE_MS);
    }

    @Test
    public void bindRetryBudgetRecoversButStaysBounded() {
        TorrentSafStorage.BindBudget budget = new TorrentSafStorage.BindBudget();
        assertFalse(budget.exhausted());
        long previous = 0;
        for (int i = 0; i < TorrentSafStorage.BindBudget.MAX_ATTEMPTS; ++i) {
            long delay = budget.charge();
            assertTrue("attempt " + i + " must be charged", delay > 0);
            assertTrue("backoff must not shrink", delay >= previous);
            previous = delay;
        }
        assertEquals(TorrentSafStorage.BindBudget.MAX_ATTEMPTS, budget.attempts());
        assertFalse(budget.exhausted());
        assertEquals(-1, budget.charge());
        assertTrue("a permanently broken policy must stop", budget.exhausted());
        // A successful connection clears the latch, so a transient crash never
        // disables SAF storage until the application restarts.
        budget.connected(1_000);
        assertFalse(budget.exhausted());
        assertEquals("the first connection is a fresh episode", 0, budget.attempts());
        for (int i = 0; i < TorrentSafStorage.BindBudget.MAX_ATTEMPTS; ++i)
            assertTrue(budget.charge() > 0);
        assertEquals(-1, budget.charge());
        assertTrue(budget.exhausted());
        // A rapid reconnect does not restore the budget: a crash loop stays
        // bounded instead of spawning and killing a broker forever.
        budget.connected(2_000);
        assertFalse(budget.exhausted());
        assertEquals(TorrentSafStorage.BindBudget.MAX_ATTEMPTS, budget.attempts());
        assertEquals(-1, budget.charge());
        assertTrue(budget.exhausted());
        // An explicit retry is honoured once per stability window.
        assertTrue(budget.rearm(3_000));
        assertFalse(budget.exhausted());
        assertEquals(0, budget.attempts());
        for (int i = 0; i < TorrentSafStorage.BindBudget.MAX_ATTEMPTS; ++i)
            assertTrue(budget.charge() > 0);
        assertEquals(-1, budget.charge());
        assertTrue(budget.exhausted());
        assertFalse(budget.rearm(4_000));
        assertTrue(budget.exhausted());
        assertTrue(budget.rearm(3_000 + TorrentSafStorage.BindBudget.STABLE_BINDING_MS));
        assertFalse(budget.exhausted());
        // An explicit selection or daemon start always begins a new episode.
        budget.reset();
        assertEquals(0, budget.attempts());
        assertFalse(budget.exhausted());
        assertTrue(budget.charge() > 0);
    }

    @Test
    public void completedTransactionRestoresTheBudgetOnlyWhenStable() {
        TorrentSafStorage.BindBudget budget = new TorrentSafStorage.BindBudget();
        budget.connected(5_000);
        for (int i = 0; i < TorrentSafStorage.BindBudget.MAX_ATTEMPTS; ++i)
            assertTrue(budget.charge() > 0);
        assertEquals(TorrentSafStorage.BindBudget.MAX_ATTEMPTS, budget.attempts());
        // A rapid reconnect is not evidence of a healthy broker.
        budget.connected(6_000);
        assertEquals(TorrentSafStorage.BindBudget.MAX_ATTEMPTS, budget.attempts());
        budget.answered(6_000 + TorrentSafStorage.BindBudget.STABLE_BINDING_MS - 1);
        assertEquals(TorrentSafStorage.BindBudget.MAX_ATTEMPTS, budget.attempts());
        budget.answered(6_000 + TorrentSafStorage.BindBudget.STABLE_BINDING_MS);
        assertEquals(0, budget.attempts());
    }

    @Test
    public void exportAndFileBoundsAreRetained() {
        // A failed export legitimately changes the size, so deletion must be
        // size-independent; these are the declared-length bounds that still
        // apply to a prepared export.
        assertTrue(TorrentSafStorageService.MAX_EXPORT > 0);
        assertTrue(TorrentSafStorageService.MAX_FILE >= TorrentSafStorageService.MAX_EXPORT);
        assertEquals(1L << 40, TorrentSafStorageService.MAX_FILE);
        assertEquals(8L * 1024 * 1024, TorrentSafStorageService.MAX_EXPORT);
    }

    // String.repeat is not available on the API 29 floor this test mirrors.
    private static String repeat(String value, int count) {
        StringBuilder result = new StringBuilder(value.length() * count);
        for (int i = 0; i < count; ++i)
            result.append(value);
        return result.toString();
    }
}
