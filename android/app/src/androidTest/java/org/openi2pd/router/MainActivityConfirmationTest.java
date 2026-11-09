/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: foreground confirmation regression, manual device CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;

import android.Manifest;
import android.content.Context;
import android.content.DialogInterface;
import android.os.Build;
import android.widget.Button;
import androidx.appcompat.app.AlertDialog;
import androidx.lifecycle.Lifecycle;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/* Uses harmless counters; no confirmation submits a router command. */
@RunWith(AndroidJUnit4.class)
public final class MainActivityConfirmationTest {
    @Before
    public void keepRouterStoppedAndAvoidPermissionPrompts() {
        Context context = ApplicationProvider.getApplicationContext();
        assertTrue(context.getSharedPreferences("router_state", Context.MODE_PRIVATE)
                .edit().putBoolean("should_run", false).remove("pending_autostart").commit());
        var automation = InstrumentationRegistry.getInstrumentation().getUiAutomation();
        if (Build.VERSION.SDK_INT >= 33)
            automation.grantRuntimePermission(
                    context.getPackageName(), Manifest.permission.POST_NOTIFICATIONS);
        if (Build.VERSION.SDK_INT >= 37)
            automation.grantRuntimePermission(
                    context.getPackageName(), "android.permission.ACCESS_LOCAL_NETWORK");
    }

    @Test
    public void replacementRevokesOldButtonsAndCurrentButtonRunsOnce() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                int[] submitted = {0};
                AlertDialog old = harmless(activity, () -> submitted[0]++);
                assertNotNull(old);
                Button oldButton = old.getButton(DialogInterface.BUTTON_POSITIVE);
                AlertDialog current = harmless(activity, () -> submitted[0]++);
                assertNotNull(current);
                assertFalse(old.isShowing());
                oldButton.performClick();
                assertEquals(0, submitted[0]);
                Button currentButton = current.getButton(DialogInterface.BUTTON_POSITIVE);
                currentButton.performClick();
                currentButton.performClick();
                assertEquals(1, submitted[0]);
                assertFalse(current.isShowing());
            });
        }
    }

    @Test
    public void recreationRetiresOldWindowBeforeSuccessorCanConfirm() {
        var submitted = new AtomicInteger();
        var old = new AtomicReference<AlertDialog>();
        var button = new AtomicReference<Button>();
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                old.set(harmless(activity, () -> submitted.incrementAndGet()));
                assertNotNull(old.get());
                button.set(old.get().getButton(DialogInterface.BUTTON_POSITIVE));
            });
            scenario.recreate();
            scenario.onActivity(activity -> {
                assertFalse(old.get().isShowing());
                button.get().performClick();
                assertEquals(0, submitted.get());
                var successor = harmless(activity, () -> submitted.incrementAndGet());
                assertNotNull(successor);
                successor.getButton(DialogInterface.BUTTON_POSITIVE).performClick();
                assertEquals(1, submitted.get());
            });
        }
    }

    @Test
    public void foregroundReturnCannotReviveStoppedWindow() {
        var submitted = new AtomicInteger();
        var old = new AtomicReference<AlertDialog>();
        var button = new AtomicReference<Button>();
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                old.set(harmless(activity, () -> submitted.incrementAndGet()));
                assertNotNull(old.get());
                button.set(old.get().getButton(DialogInterface.BUTTON_POSITIVE));
            });
            scenario.moveToState(Lifecycle.State.CREATED);
            scenario.moveToState(Lifecycle.State.RESUMED);
            scenario.onActivity(activity -> {
                assertFalse(old.get().isShowing());
                button.get().performClick();
                assertEquals(0, submitted.get());
            });
        }
    }

    private static AlertDialog harmless(MainActivity activity, Runnable action) {
        return activity.confirm(R.string.confirm_exit_title, R.string.confirm_exit_message,
                R.string.action_continue, action);
    }
}
