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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.content.ComponentName;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.os.IBinder;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.rule.ServiceTestRule;

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

    /* Exercise the receiver callback without sending a protected broadcast. */
    @Test
    public void bootBroadcastNeverRestartsAStoppedRouter() {
        Context context = ApplicationProvider.getApplicationContext();
        var state = context.getSharedPreferences("router_state", Context.MODE_PRIVATE);
        var receiverContext = new ReceiverContext(context);
        var boot = new Intent(Intent.ACTION_BOOT_COMPLETED);
        var receiver = new BootUpReceiver();
        boolean previousAutostart = AutoStartController.isEnabled(context);
        try {
            assertTrue(AutoStartController.setEnabled(context, true, false));
            assertTrue(state.edit()
                               .putBoolean("should_run", false)
                               .putString("run_phase", "stopped")
                               .putLong("graceful_deadline_epoch_ms", 0L)
                               .commit());
            receiver.onReceive(receiverContext, boot);
            assertNull("stopped router must not request service start", receiverContext.request);
            assertFalse("the persisted run must stay stopped",
                    RouterService.isStartRequested(context));
            assertEquals("stopped", state.getString("run_phase", null));

            // A running intent with the same enabled marker reaches the
            // interceptor and carries the sticky-restore command.
            assertTrue(state.edit()
                               .putBoolean("should_run", true)
                               .putString("run_phase", "running")
                               .commit());
            receiver.onReceive(receiverContext, boot);
            assertNotNull("running intent must request service restore", receiverContext.request);
            assertEquals(new ComponentName(context, RouterService.class),
                    receiverContext.request.getComponent());
            assertNull("restore must not carry an explicit start action",
                    receiverContext.request.getAction());
        } finally {
            state.edit().putBoolean("should_run", false).putString("run_phase", "stopped").commit();
            AutoStartController.setEnabled(context, previousAutostart, false);
        }
    }

    private static final class ReceiverContext extends ContextWrapper {
        private Intent request;

        ReceiverContext(Context base) {
            super(base);
        }

        @Override
        public Context getApplicationContext() {
            return this;
        }

        @Override
        public ComponentName startForegroundService(Intent intent) {
            return startService(intent);
        }

        @Override
        public ComponentName startService(Intent intent) {
            assertNull("receiver must request service start at most once", request);
            request = new Intent(intent);
            return intent.getComponent();
        }
    }
}
