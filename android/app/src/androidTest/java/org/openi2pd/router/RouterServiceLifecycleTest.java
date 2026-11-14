/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: RouterServiceLifecycleTest.
 *
 * Instrumentation tests that bind the non-exported {@link
 * RouterService} through {@code ServiceTestRule}, check the binder is
 * the in-process {@code LocalBinder} and unbind again, and prove that a
 * boot broadcast cannot resurrect a stopped run intent. The tests clear
 * the persisted run flag first so binding never starts the daemon or
 * opens sockets.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.SystemClock;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.rule.ServiceTestRule;

import org.junit.Assume;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.openi2pd.router.receivers.BootUpReceiver;

/*
 * Instrumentation lifecycle tests. They intentionally never start the
 * native daemon, open sockets or require a network. A device/emulator
 * must still run them in CI.
 *
 * <p>The tests clear the persisted run flag before binding so a
 * previous test or manual session cannot make {@link
 * RouterService#onCreate()} start the router. The literal keys match the
 * service's private preference contract; this is test-only and does not
 * expose the service to UI code.</p>
 */
@RunWith(AndroidJUnit4.class)
public final class RouterServiceLifecycleTest {
    @Rule public final ServiceTestRule serviceRule = new ServiceTestRule();

    @Test
    public void bindsNonExportedRouterServiceAndReturnsLocalBinder() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences("router_state", Context.MODE_PRIVATE)
                .edit()
                .remove("should_run")
                .commit();

        Intent intent = new Intent(context, RouterService.class);
        IBinder binder = serviceRule.bindService(intent);

        assertNotNull("RouterService returned no binder", binder);
        assertTrue("unexpected binder type", binder instanceof RouterService.LocalBinder);
        RouterService service = ((RouterService.LocalBinder) binder).getService();
        assertNotNull("LocalBinder returned no service", service);

        serviceRule.unbindService();
    }

    /*
     * A boot broadcast must deliver the restore command, not an explicit
     * start command: the persisted stop is terminal, so the service
     * reconciles to "stay stopped" and stops itself again. The previous
     * implementation sent {@code ACTION_START}, which persisted a
     * running intent before the service saw it and resurrected a router
     * the operator had stopped (or a drain whose deadline had passed).
     *
     * <p>Broadcast delivery is asynchronous, so the assertion polls for
     * a bounded window instead of assuming the receiver already ran; it
     * never starts the daemon on a correct build.</p>
     */
    @Test
    public void bootBroadcastNeverRestartsAStoppedRouter() {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences("router_state", Context.MODE_PRIVATE)
                .edit()
                .putBoolean("should_run", false)
                .putString("run_phase", "stopped")
                .putLong("graceful_deadline_epoch_ms", 0L)
                .commit();

        try {
            context.sendBroadcast(new Intent(Intent.ACTION_BOOT_COMPLETED)
                            .setComponent(new ComponentName(context, BootUpReceiver.class)));
        } catch (SecurityException denied) {
            // A platform that refuses to let the app send its own
            // protected broadcast cannot exercise the receiver; the JVM
            // matrix in BootResumePolicyTest still pins the rule.
            Assume.assumeNoException("protected broadcast refused", denied);
        }

        long deadline = SystemClock.elapsedRealtime() + 2_000L;
        while (SystemClock.elapsedRealtime() < deadline) {
            assertFalse("a boot broadcast must not request a run",
                    RouterService.isStartRequested(context));
            assertFalse(
                    "a boot broadcast must not start the daemon", RouterService.isRouterRunning());
            SystemClock.sleep(50L);
        }
        String phase = context.getSharedPreferences("router_state", Context.MODE_PRIVATE)
                               .getString("run_phase", null);
        assertTrue("the persisted stop must be unchanged, was " + phase, "stopped".equals(phase));
    }
}
