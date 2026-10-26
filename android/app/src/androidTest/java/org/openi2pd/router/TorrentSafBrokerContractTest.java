/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: end-to-end contract regressions for the real SAF broker process.
 *
 * Manual device CI only. The broker is hosted in ":torrent_storage", so these
 * cases exercise the generated Binder stubs, the caller binding, generation
 * fenced retirement and a real rebind after process death.
 *
 * Deliberately not automated here:
 * - A foreign-UID denial needs a helper APK with a different UID; the test app
 *   shares the application UID with the broker by construction.
 * - Payload IO through a granted tree needs a real DocumentsProvider grant.
 *   The in-tree fixture is declared by the instrumentation APK under
 *   android.permission.MANAGE_DOCUMENTS, so neither this process nor the broker
 *   can obtain a URI grant for it; multi-file nested open/read/write, deleted
 *   documents, same-length replacement, no-seek providers and revocation after
 *   open are therefore exercised at the provider boundary by
 *   TorrentSafProviderCancellationTest. The broker-side scenario stays manual:
 *   select a real tree, then run the same nested sequence across a broker
 *   retirement.
 * - A slow provider that ignores cancellation needs the same real grant. The
 *   15 s cancel / 30 s fence stages are pinned by TorrentSafProtocolTest; the
 *   device scenario stays manual: open a handle through a granted tree, start a
 *   blocking provider call and verify that an unrelated handle still answers
 *   after 16 s and that the process has not been replaced.
 */
package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.SystemClock;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.After;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Manual device-CI contract for the real SAF broker process.
 *
 * <p>Hosts nothing itself: it binds {@code :torrent_storage}, checks the
 * reported generation/API version, that a foreign operation id or wrong owner
 * is refused, that retirement is one-shot and generation-fenced, and that a
 * rebind after broker death reaches a fresh generation. Payload IO through a
 * real tree grant, foreign-UID denial and a provider that ignores cancellation
 * stay manual scenarios; the provider-boundary cases live in
 * {@link TorrentSafProviderCancellationTest}.</p>
 */
@RunWith(AndroidJUnit4.class)
public final class TorrentSafBrokerContractTest {
    private static final long BIND_TIMEOUT_MS = 10000;
    private static final long RETIRE_TIMEOUT_MS = 10000;
    private static final long UNKNOWN = 0x7fff_ffffL;
    private static final String NO_GRANT = "content://org.openi2pd.router.test.saf/document/null";

    private Context context;
    private ServiceConnection connection;
    private IBinder binder;
    private ITorrentSafStorage broker;
    private long operation = 1;

    @Before
    public void setUp() {
        context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    }

    @After
    public void tearDown() {
        if (connection != null && context != null) {
            try {
                context.unbindService(connection);
            } catch (IllegalArgumentException gone) {
            }
            connection = null;
        }
        binder = null;
        broker = null;
    }

    /** Binds the real broker or skips when this build cannot host it. */
    private void bind() throws InterruptedException {
        final CountDownLatch connected = new CountDownLatch(1);
        final AtomicReference<IBinder> result = new AtomicReference<>();
        ServiceConnection fresh = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                result.set(service);
                connected.countDown();
            }
            @Override
            public void onServiceDisconnected(ComponentName name) {
            }
            @Override
            public void onNullBinding(ComponentName name) {
                connected.countDown();
            }
        };
        connection = fresh;
        boolean requested;
        try {
            requested = context.bindService(new Intent(context, TorrentSafStorageService.class),
                    fresh, Context.BIND_AUTO_CREATE);
        } catch (SecurityException denied) {
            Assume.assumeNoException("SAF broker binding denied", denied);
            return;
        }
        Assume.assumeTrue("SAF broker service is not declared in this build", requested);
        assertTrue("Broker did not connect within the budget",
                connected.await(BIND_TIMEOUT_MS, TimeUnit.MILLISECONDS));
        binder = result.get();
        // The broker stops itself when the native sandbox restriction is
        // unavailable, which is a device capability issue, not a regression.
        Assume.assumeNotNull("Broker refused to start (native restriction unavailable)", binder);
        broker = ITorrentSafStorage.Stub.asInterface(binder);
        assertNotNull(broker);
    }

    private long next() {
        return operation++;
    }

    private static boolean awaitDeath(IBinder target, long timeoutMs) {
        long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        while (SystemClock.elapsedRealtime() < deadline) {
            if (!target.isBinderAlive())
                return true;
            SystemClock.sleep(25);
        }
        return !target.isBinderAlive();
    }

    @Test
    public void brokerAnswersVersionedHandshake() throws Exception {
        bind();
        assertEquals(2, broker.apiVersion());
        long generation = broker.handshake();
        assertTrue("Broker generation must be a non-zero CSPRNG value", generation > 0);
        assertEquals(
                "The generation is stable for one broker process", generation, broker.handshake());
    }

    @Test
    public void foreignGenerationsCannotRetireTheBroker() throws Exception {
        bind();
        long generation = broker.handshake();
        // Zero, stale and negative generations are typed no-ops: a delayed or
        // replayed oneway frame must never fence a live broker.
        broker.retire(0L);
        broker.retire(generation + 1);
        broker.retire(-generation);
        SystemClock.sleep(250);
        assertTrue("Broker died for a foreign generation", binder.isBinderAlive());
        assertEquals(generation, broker.handshake());
    }

    @Test
    public void unknownCapabilitiesReturnTypedFallbacks() throws Exception {
        bind();
        long generation = broker.handshake();
        // None of these may throw across Binder or retire the process.
        assertEquals(0L, broker.openExport(0L, next()));
        assertEquals(0L, broker.openExport(UNKNOWN, next()));
        assertEquals(0L, broker.prepareExport(null, 1024, next()));
        assertEquals(0L, broker.prepareExport(NO_GRANT, 0, next()));
        assertEquals(0L, broker.prepareExport(NO_GRANT, 1024, next()));
        assertEquals(0L, broker.prepareExport(NO_GRANT, 8 * 1024 * 1024 + 1, next()));
        assertEquals(0L,
                broker.authorizeTree("content://org.openi2pd.router.test.saf/tree/null", next()));
        assertFalse(broker.finishExport(UNKNOWN, true, next()));
        assertFalse(broker.finishExport(UNKNOWN, false, next()));
        assertNull(broker.read(UNKNOWN, 0, 16, next()));
        assertNull(broker.read(UNKNOWN, -1, 16, next()));
        assertFalse(broker.write(UNKNOWN, 0, new byte[16], next()));
        assertFalse(broker.write(UNKNOWN, 0, new byte[16385], next()));
        assertFalse(broker.deleteFile(UNKNOWN, next()));
        assertFalse(broker.deleteByRelative(UNKNOWN, "part.bin", next()));
        assertFalse(broker.deleteByRelative(UNKNOWN, "../escape", next()));
        assertFalse(broker.removeEmptyDirectory(UNKNOWN, "dir", next()));
        assertFalse(broker.removeEmptyDirectory(UNKNOWN, "", next()));
        broker.closeFile(UNKNOWN);
        broker.revokeTree(UNKNOWN);
        broker.cancel(UNKNOWN);
        broker.cancel(0L);
        assertTrue("A refused capability must not kill the broker", binder.isBinderAlive());
        assertEquals(generation, broker.handshake());
    }

    @Test
    public void retireIsGenerationFencedAndRebindYieldsANewGeneration() throws Exception {
        bind();
        long generation = broker.handshake();
        broker.retire(generation);
        assertTrue("Broker ignored its own retirement", awaitDeath(binder, RETIRE_TIMEOUT_MS));
        tearDown();
        long start = SystemClock.elapsedRealtime();
        bind();
        // The client bounds the retiring state at 5 s per check; a cold broker
        // process start on a slow device still has to fit the bind budget.
        assertTrue("Rebind exceeded the bind budget",
                SystemClock.elapsedRealtime() - start <= BIND_TIMEOUT_MS);
        long replacement = broker.handshake();
        assertTrue(replacement > 0);
        assertNotEquals("A replacement broker must not reuse the fenced generation", generation,
                replacement);
        assertEquals(2, broker.apiVersion());
    }

    @Test
    public void reboundBrokerAnswersTheWholeTypedSurface() throws Exception {
        bind();
        long generation = broker.handshake();
        broker.retire(generation);
        assertTrue("Broker ignored its own retirement", awaitDeath(binder, RETIRE_TIMEOUT_MS));
        tearDown();
        bind();
        long replacement = broker.handshake();
        assertNotEquals("A resumed generation was reused", generation, replacement);
        // A resumed payload read/write would need a persisted tree grant for a
        // provider both processes can reach; the in-tree fixture is served by
        // the instrumentation APK under MANAGE_DOCUMENTS. The typed surface,
        // including the multi-file nested paths the engine uses, is what this
        // bounded case can pin.
        assertEquals(0L,
                broker.openFile(UNKNOWN, TorrentSafFixtureProvider.PART_ONE,
                        TorrentSafFixtureProvider.PART_ONE_SIZE, false, next()));
        assertEquals(0L,
                broker.openFile(UNKNOWN, TorrentSafFixtureProvider.PART_TWO,
                        TorrentSafFixtureProvider.PART_TWO_SIZE, true, next()));
        assertEquals(
                0L, broker.openFile(UNKNOWN, TorrentSafFixtureProvider.EMPTY, 0, false, next()));
        assertEquals(0L, broker.openFile(UNKNOWN, "../escape", 1, false, next()));
        assertNull(broker.read(UNKNOWN, 0, 16, next()));
        assertFalse(broker.write(UNKNOWN, 0, new byte[16], next()));
        assertFalse(broker.deleteByRelative(UNKNOWN, TorrentSafFixtureProvider.PART_TWO, next()));
        assertFalse(broker.removeEmptyDirectory(UNKNOWN, "torrent/sub", next()));
        broker.closeFile(UNKNOWN);
        broker.revokeTree(UNKNOWN);
        assertTrue("A refused capability must not kill the rebound broker", binder.isBinderAlive());
        assertEquals(replacement, broker.handshake());
    }
}
