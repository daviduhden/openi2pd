/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: provider fault behavior regressions for manual device CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import android.content.pm.ProviderInfo;
import android.os.CancellationSignal;
import android.os.OperationCanceledException;
import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import java.util.concurrent.*;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class TorrentSafProviderCancellationTest {
    private TorrentSafFixtureProvider provider() {
        TorrentSafFixtureProvider provider = new TorrentSafFixtureProvider();
        ProviderInfo info = new ProviderInfo();
        info.authority = "org.openi2pd.router.test.saf";
        info.exported = true; info.grantUriPermissions = true;
        info.readPermission = info.writePermission = "android.permission.MANAGE_DOCUMENTS";
        provider.attachInfo(InstrumentationRegistry.getInstrumentation().getTargetContext(), info);
        return provider;
    }
    // These tests exercise real DocumentsProvider cancellation and descriptor behavior.
    // Persistable-grant acquisition and broker death/rebind remain separate end-to-end CI cases.
    @Test public void cooperativeProviderObservesCancellation() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.COOPERATIVE, "open");
        CancellationSignal signal = new CancellationSignal();
        ExecutorService worker = Executors.newSingleThreadExecutor();
        Future<ParcelFileDescriptor> call = worker.submit(() -> provider.openDocument("payload", "rw", signal));
        try {
            assertTrue(provider.awaitEntered()); signal.cancel();
            try { call.get(2, TimeUnit.SECONDS); fail("Expected provider cancellation"); }
            catch (ExecutionException canceled) { assertTrue(canceled.getCause() instanceof OperationCanceledException); }
            assertEquals(1, provider.cancellations());
        } finally { provider.unblock(); worker.shutdownNow(); }
    }
    @Test public void ignoredCancellationRequiresBrokerRetirement() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.IGNORE_CANCELLATION, "open");
        CancellationSignal signal = new CancellationSignal();
        ExecutorService worker = Executors.newSingleThreadExecutor();
        Future<ParcelFileDescriptor> call = worker.submit(() -> provider.openDocument("payload", "rw", signal));
        try {
            assertTrue(provider.awaitEntered()); signal.cancel();
            try { call.get(100, TimeUnit.MILLISECONDS); fail("Fixture must ignore cancellation"); }
            catch (TimeoutException expected) { assertEquals(1, provider.cancellations()); }
            provider.unblock();
            try (ParcelFileDescriptor fd = call.get(2, TimeUnit.SECONDS)) { assertNotNull(fd); }
        } finally { provider.unblock(); worker.shutdownNow(); }
    }
    @Test public void nonSeekableProviderCannotBecomeRandomAccessStorage() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.PIPE, "open");
        try (ParcelFileDescriptor fd = provider.openDocument("payload", "r", new CancellationSignal())) {
            try { Os.lseek(fd.getFileDescriptor(), 0, OsConstants.SEEK_SET); fail("Expected ESPIPE"); }
            catch (ErrnoException unsupported) { assertEquals(OsConstants.ESPIPE, unsupported.errno); }
        }
    }
    @Test public void revokedProviderFailsClosed() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.REVOKED, "open");
        try { provider.openDocument("payload", "rw", new CancellationSignal()); fail("Expected missing grant"); }
        catch (java.io.FileNotFoundException expected) { }
    }
}
