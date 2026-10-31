/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: MainActivityNavigationTest.
 *
 * Espresso smoke test that pins the persisted run flag to false,
 * launches {@link MainActivity}, opens the log screen from the
 * dashboard and verifies that Back returns home. The empty-state
 * assertion guards a former View/TextView cast crash; no native daemon
 * is started.
 *
 */

package org.openi2pd.router;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.Espresso.pressBack;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.assertion.ViewAssertions.matches;

import android.Manifest;
import android.content.Context;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SdkSuppress;
import androidx.test.rule.GrantPermissionRule;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Instrumentation navigation smoke test. It deliberately persists
 * {@code should_run=false} before launch so opening the activity
 * exercises the service binding and screen transitions without starting
 * the native daemon.
 *
 * <p>This test is prepared for a device/emulator CI job; it is not
 * executed by the static local workflow.</p>
 */
@RunWith(AndroidJUnit4.class)
@SdkSuppress(minSdkVersion = 33)
public final class MainActivityNavigationTest {
    @Rule
    public final GrantPermissionRule notificationPermission =
            GrantPermissionRule.grant(Manifest.permission.POST_NOTIFICATIONS);

    @Before
    public void keepRouterStopped() {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences("router_state", Context.MODE_PRIVATE)
                .edit()
                .putBoolean("should_run", false)
                .commit();
    }

    @Test
    public void dashboardQuickActionOpensLogsAndBackReturnsHome() {
        try (ActivityScenario<MainActivity> ignored = ActivityScenario.launch(MainActivity.class)) {
            onView(withId(R.id.status_title)).check(matches(isDisplayed()));

            onView(withId(R.id.button_dashboard_logs)).perform(click());
            onView(withId(R.id.logs_search)).check(matches(isDisplayed()));
            // Regression guard for the former View/TextView empty-state
            // crash.
            onView(withId(R.id.logs_empty_text)).check(matches(isDisplayed()));

            pressBack();
            onView(withId(R.id.status_title)).check(matches(isDisplayed()));
        }
    }
}
