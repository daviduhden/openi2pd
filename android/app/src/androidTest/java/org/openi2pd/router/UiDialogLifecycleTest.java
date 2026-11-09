/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: view-owned Material dialog regressions, manual device CI.
 */
package org.openi2pd.router;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.Espresso.pressBack;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.RootMatchers.isDialog;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.*;

import android.Manifest;
import android.content.Context;
import android.os.Build;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import androidx.fragment.app.Fragment;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/*
 * Opens only local forms and never submits administrative commands.
 * The router stays stopped. Each fragment instance loses and recreates
 * its view, exercising the case where isAdded becomes true again but
 * old dialog authority must remain retired.
 */
@RunWith(AndroidJUnit4.class)
public final class UiDialogLifecycleTest {
    @Before
    public void keepRouterStoppedAndAvoidStartupPermissionPrompts() {
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
    public void addressBookDialogRetiresBeforeSameFragmentViewRecreation() {
        assertDialogRetirement(new AddressBookFragment(), R.string.address_book_add);
    }

    @Test
    public void runtimeShareDialogRetiresBeforeSameFragmentViewRecreation() {
        assertDialogRetirement(new RuntimeAdminFragment(), R.string.runtime_admin_share_action);
    }

    @Test
    public void nestedMaterialChoicesKeepParentUntilViewRetires() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                var dialogs = new UiDialogEpoch();
                dialogs.openView();
                var token = dialogs.token();
                var parent = new com.google.android.material.dialog.MaterialAlertDialogBuilder(activity)
                        .setTitle(R.string.torrent_create_saf_title).create();
                var child = new com.google.android.material.dialog.MaterialAlertDialogBuilder(activity)
                        .setTitle(R.string.torrent_piece_size_title).create();
                var parentRegistration = dialogs.replace(token, parent::dismiss);
                parent.setOnDismissListener(ignored -> dialogs.dismissed(parentRegistration));
                parent.show();
                var childRegistration = dialogs.nest(token, child::dismiss);
                child.setOnDismissListener(ignored -> dialogs.dismissed(childRegistration));
                child.show();
                assertTrue(parent.isShowing());
                assertTrue(child.isShowing());
                dialogs.retireView();
                assertFalse(parent.isShowing());
                assertFalse(child.isShowing());
                assertFalse(dialogs.maySubmit(token, true, false));
            });
        }
    }

    private void assertDialogRetirement(Fragment fragment, int actionLabel) {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                activity.getSupportFragmentManager().beginTransaction()
                        .replace(R.id.fragment_container, fragment, "dialog-lifecycle").commitNow();
                clickLabel(fragment.requireView(), activity.getString(actionLabel));
            });
            onView(withText(R.string.address_book_save)).inRoot(isDialog())
                    .check(matches(isDisplayed()));
            scenario.onActivity(activity -> {
                var manager = activity.getSupportFragmentManager();
                manager.beginTransaction().detach(fragment).commitNow();
                manager.beginTransaction().attach(fragment).commitNow();
            });
            onView(withText(R.string.address_book_save)).check(doesNotExist());
            scenario.onActivity(activity ->
                    clickLabel(fragment.requireView(), activity.getString(actionLabel)));
            onView(withText(R.string.address_book_save)).inRoot(isDialog())
                    .check(matches(isDisplayed()));
            pressBack();
        }
    }

    private static void clickLabel(View root, String label) {
        View action = findClickableLabel(root, label);
        assertNotNull("missing dialog action", action);
        assertTrue(action.performClick());
    }

    private static View findClickableLabel(View view, String label) {
        if (view instanceof TextView text && view.isClickable()
                && label.contentEquals(text.getText()))
            return view;
        if (view instanceof ViewGroup group)
            for (int i = 0; i < group.getChildCount(); ++i) {
                View child = findClickableLabel(group.getChildAt(i), label);
                if (child != null)
                    return child;
            }
        return null;
    }
}
