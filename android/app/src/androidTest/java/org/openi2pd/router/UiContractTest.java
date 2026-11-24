/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: UiContractTest.
 *
 * Read-only layout/accessibility contracts for the native Material
 * screens. Prepared for the manual device lane; no daemon or service is
 * started.
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.res.Configuration;
import android.content.res.TypedArray;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

import androidx.core.view.ViewCompat;
import androidx.test.annotation.UiThreadTest;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.color.MaterialColors;

import org.junit.Test;
import org.junit.runner.RunWith;

/*
 * Read-only contract checks for the native Material 3 screens.
 *
 * <p>Asserts one-screen-reader-stop rows, visible touch targets, the
 * `w600dp` navigation rail, the semantic brand color roles in both
 * light and dark themes, metric label/value association and the log
 * filter/summary wiring. The daemon and the service are never
 * started, so this says nothing about live router behaviour; device
 * and TalkBack evidence remains the manual UI lane.</p>
 */
@RunWith(AndroidJUnit4.class)
public final class UiContractTest {
    private Context themed() {
        return new android.view.ContextThemeWrapper(
                ApplicationProvider.getApplicationContext(), R.style.Theme_OpenI2Pd);
    }

    private View inflate(int layout) {
        return LayoutInflater.from(themed()).inflate(layout, null, false);
    }

    @Test
    @UiThreadTest
    public void dashboardExposesStatusDetailAndReseed() {
        View view = inflate(R.layout.fragment_dashboard);
        assertNotNull(view.findViewById(R.id.status_detail));
        assertNotNull(view.findViewById(R.id.button_dashboard_reseed));
    }

    @Test
    @UiThreadTest
    public void statisticsExposesNowAndPeakLabels() {
        View view = inflate(R.layout.fragment_statistics);
        assertNotNull(view.findViewById(R.id.stat_download_now));
        assertNotNull(view.findViewById(R.id.stat_upload_now));
        assertNotNull(view.findViewById(R.id.stat_chart_peak));
    }

    @Test
    @UiThreadTest
    public void logsFiltersWrapAndSummaryIsAnnounced() {
        View view = inflate(R.layout.fragment_logs);
        View filters = view.findViewById(R.id.logs_filter_group);
        assertNotNull(filters);
        assertTrue(!(filters.getParent() instanceof android.widget.HorizontalScrollView));
        TextView summary = view.findViewById(R.id.logs_summary);
        assertEquals(View.ACCESSIBILITY_LIVE_REGION_POLITE, summary.getAccessibilityLiveRegion());
    }

    @Test
    @UiThreadTest
    public void logRowIsOneScreenReaderStop() {
        View row = inflate(R.layout.item_log);
        assertTrue(row.isScreenReaderFocusable());
        for (int id : new int[] {R.id.log_time, R.id.log_level, R.id.log_message}) {
            assertEquals("log child must stay out of the accessibility tree",
                    View.IMPORTANT_FOR_ACCESSIBILITY_NO,
                    row.findViewById(id).getImportantForAccessibility());
        }
    }

    @Test
    public void themeExposesBrandRolesInBothModes() {
        Context day = resolution(Configuration.UI_MODE_NIGHT_NO);
        int daySurface =
                MaterialColors.getColor(day, com.google.android.material.R.attr.colorSurface, 0);
        assertEquals(day.getColor(R.color.brand_surface), daySurface);

        Context night = resolution(Configuration.UI_MODE_NIGHT_YES);
        int nightSurface =
                MaterialColors.getColor(night, com.google.android.material.R.attr.colorSurface, 0);
        assertEquals(night.getColor(R.color.brand_surface), nightSurface);
        assertTrue(
                "the night surface must differ from the day surface", daySurface != nightSurface);
    }

    @Test
    @UiThreadTest
    public void wideLayoutUsesNavigationRail() {
        View wide = inflateWide(R.layout.activity_main, 800, 600);
        assertNotNull(wide.findViewById(R.id.nav_rail));
        assertNull(wide.findViewById(R.id.nav_bottom));
        View compact = inflate(R.layout.activity_main);
        assertNotNull(compact.findViewById(R.id.nav_bottom));
        assertNull(compact.findViewById(R.id.nav_rail));
    }

    @Test
    @UiThreadTest
    public void largeTextKeepsDashboardActionsReachable() {
        Context base = ApplicationProvider.getApplicationContext();
        Configuration configuration = new Configuration(base.getResources().getConfiguration());
        configuration.fontScale = 2.0f;
        configuration.screenWidthDp = 360;
        configuration.smallestScreenWidthDp = 360;
        Context context = new android.view.ContextThemeWrapper(
                base.createConfigurationContext(configuration), R.style.Theme_OpenI2Pd);
        View dashboard =
                LayoutInflater.from(context).inflate(R.layout.fragment_dashboard, null, false);
        int target = context.getResources().getDimensionPixelSize(R.dimen.touch_target);
        int width = Math.round(360 * context.getResources().getDisplayMetrics().density);
        dashboard.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));

        for (int id : new int[] {R.id.button_dashboard_logs, R.id.button_dashboard_settings,
                     R.id.button_dashboard_reseed}) {
            View action = dashboard.findViewById(id);
            assertNotNull(action);
            assertEquals(View.VISIBLE, action.getVisibility());
            assertTrue(action.isFocusable());
            assertTrue(action.getMeasuredHeight() >= target);
            assertTrue(action.getMeasuredWidth() >= target);
        }
    }

    @Test
    @UiThreadTest
    public void wideNavigationRailFollowsLayoutDirection() {
        for (int direction : new int[] {View.LAYOUT_DIRECTION_LTR, View.LAYOUT_DIRECTION_RTL}) {
            View root = inflateWide(R.layout.activity_main, 800, 600);
            root.setLayoutDirection(direction);
            float density = root.getResources().getDisplayMetrics().density;
            int width = Math.round(800 * density);
            int height = Math.round(600 * density);
            root.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY));
            root.layout(0, 0, width, height);

            View rail = root.findViewById(R.id.nav_rail);
            View content = root.findViewById(R.id.fragment_container);
            assertNotNull(rail);
            assertNotNull(content);
            assertTrue(rail.getWidth() > 0);
            assertTrue(content.getWidth() > 0);
            assertTrue(direction == View.LAYOUT_DIRECTION_RTL
                            ? content.getRight() <= rail.getLeft()
                            : rail.getRight() <= content.getLeft());
        }
    }

    @Test
    @UiThreadTest
    public void windowOwnsSurfaceWithoutDuplicateRootBackgrounds() {
        for (int mode :
                new int[] {Configuration.UI_MODE_NIGHT_NO, Configuration.UI_MODE_NIGHT_YES}) {
            Context context = resolution(mode);
            TypedArray attributes =
                    context.obtainStyledAttributes(new int[] {android.R.attr.windowBackground});
            try {
                Drawable background = attributes.getDrawable(0);
                assertTrue(background instanceof ColorDrawable);
                assertEquals(MaterialColors.getColor(
                                     context, com.google.android.material.R.attr.colorSurface, 0),
                        ((ColorDrawable) background).getColor());
            } finally {
                attributes.recycle();
            }

            // Resolve both layout variants independently of the device.
            for (int width : new int[] {400, 800}) {
                Configuration configuration =
                        new Configuration(context.getResources().getConfiguration());
                configuration.screenWidthDp = width;
                configuration.smallestScreenWidthDp = width;
                Context sized = new android.view.ContextThemeWrapper(
                        context.createConfigurationContext(configuration), R.style.Theme_OpenI2Pd);
                View root = LayoutInflater.from(sized).inflate(R.layout.activity_main, null, false);
                assertNull(root.getBackground());
            }
        }
    }

    @Test
    @UiThreadTest
    public void programmaticRowMeetsTouchTargetAndSpeaksOnce() {
        Context context = themed();
        MaterialButton button = UiFactory.button(context);
        int target = context.getResources().getDimensionPixelSize(R.dimen.touch_target);
        assertTrue(button.getMinHeight() >= target);

        UiFactory.applyRowSemantics(button, "example, Seeding, 100%");
        assertEquals("example, Seeding, 100%", button.getContentDescription());
        assertEquals("example, Seeding, 100%", ViewCompat.getStateDescription(button));
    }

    @Test
    @UiThreadTest
    public void metricLabelsAreAssociatedWithValueViews() {
        assertLabelFor(inflate(R.layout.fragment_dashboard), R.id.metric_uptime, R.id.metric_peers,
                R.id.metric_routers, R.id.metric_tunnels, R.id.metric_download, R.id.metric_upload,
                R.id.metric_reseed);
        assertLabelFor(inflate(R.layout.fragment_statistics), R.id.stat_download_rate,
                R.id.stat_upload_rate, R.id.stat_total_received, R.id.stat_total_sent,
                R.id.stat_total_transit, R.id.stat_tunnels, R.id.stat_uptime, R.id.stat_peers,
                R.id.stat_routers);
        assertLabelFor(inflate(R.layout.fragment_tunnels), R.id.text_tunnels_in,
                R.id.text_tunnels_out, R.id.text_tunnels_transit, R.id.text_tunnels_total);
        assertLabelFor(inflate(R.layout.fragment_network), R.id.text_ipv4, R.id.text_ipv6);
    }

    /*
     * Each label points to its value through labelFor, rather than the
     * reverse. Labels need no ID, so inspect the complete hierarchy and
     * require exactly one nonempty TextView label for each value.
     */
    private static void assertLabelFor(View root, int... valueIds) {
        for (int id : valueIds) {
            View value = root.findViewById(id);
            assertNotNull("missing metric value view " + id, value);
            int labels = 0;
            var pending = new java.util.ArrayDeque<View>();
            pending.add(root);
            while (!pending.isEmpty()) {
                View candidate = pending.remove();
                if (candidate.getLabelFor() == id) {
                    assertTrue("a metric must not label itself", candidate != value);
                    assertTrue("metric label must be a TextView", candidate instanceof TextView);
                    assertTrue("label text must not be empty",
                            ((TextView) candidate).getText().toString().trim().length() > 0);
                    ++labels;
                }
                if (candidate instanceof android.view.ViewGroup group)
                    for (int child = 0; child < group.getChildCount(); ++child)
                        pending.add(group.getChildAt(child));
            }
            assertEquals("expected exactly one android:labelFor for @id/" + id, 1, labels);
        }
    }

    private static Context resolution(int nightMode) {
        Configuration configuration = new Configuration(
                ApplicationProvider.getApplicationContext().getResources().getConfiguration());
        configuration.uiMode =
                (configuration.uiMode & ~Configuration.UI_MODE_NIGHT_MASK) | nightMode;
        return new android.view.ContextThemeWrapper(
                ApplicationProvider.getApplicationContext().createConfigurationContext(
                        configuration),
                R.style.Theme_OpenI2Pd);
    }

    private static View inflateWide(int layout, int widthDp, int smallestWidthDp) {
        Configuration configuration = new Configuration(
                ApplicationProvider.getApplicationContext().getResources().getConfiguration());
        configuration.screenWidthDp = widthDp;
        configuration.smallestScreenWidthDp = smallestWidthDp;
        Context wide = ApplicationProvider.getApplicationContext().createConfigurationContext(
                configuration);
        return LayoutInflater
                .from(new android.view.ContextThemeWrapper(wide, R.style.Theme_OpenI2Pd))
                .inflate(layout, null, false);
    }
}
