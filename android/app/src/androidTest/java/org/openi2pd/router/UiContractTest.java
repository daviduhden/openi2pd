/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: UiContractTest.
 *
 * Read-only layout/accessibility contracts for the native Material screens.
 * Prepared for the manual device lane; no daemon or service is started.
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.res.Configuration;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

import androidx.core.view.ViewCompat;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.color.MaterialColors;

import org.junit.Test;
import org.junit.runner.RunWith;

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
    public void dashboardExposesStatusDetailAndReseed() {
        View view = inflate(R.layout.fragment_dashboard);
        assertNotNull(view.findViewById(R.id.status_detail));
        assertNotNull(view.findViewById(R.id.button_dashboard_reseed));
    }

    @Test
    public void statisticsExposesNowAndPeakLabels() {
        View view = inflate(R.layout.fragment_statistics);
        assertNotNull(view.findViewById(R.id.stat_download_now));
        assertNotNull(view.findViewById(R.id.stat_upload_now));
        assertNotNull(view.findViewById(R.id.stat_chart_peak));
    }

    @Test
    public void logsFiltersWrapAndSummaryIsAnnounced() {
        View view = inflate(R.layout.fragment_logs);
        View filters = view.findViewById(R.id.logs_filter_group);
        assertNotNull(filters);
        assertTrue(!(filters.getParent() instanceof android.widget.HorizontalScrollView));
        TextView summary = view.findViewById(R.id.logs_summary);
        assertEquals(View.ACCESSIBILITY_LIVE_REGION_POLITE, summary.getAccessibilityLiveRegion());
    }

    @Test
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
    public void wideLayoutUsesNavigationRail() {
        View wide = inflateWide(R.layout.activity_main, 800, 600);
        assertNotNull(wide.findViewById(R.id.nav_rail));
        assertNull(wide.findViewById(R.id.nav_bottom));
        View compact = inflate(R.layout.activity_main);
        assertNotNull(compact.findViewById(R.id.nav_bottom));
        assertNull(compact.findViewById(R.id.nav_rail));
    }

    @Test
    public void programmaticRowMeetsTouchTargetAndSpeaksOnce() {
        Context context = themed();
        MaterialButton button = UiFactory.button(context);
        int target = context.getResources().getDimensionPixelSize(R.dimen.touch_target);
        assertTrue(button.getMinHeight() >= target);

        UiFactory.applyRowSemantics(button, "example, Seeding, 100%");
        assertEquals("example, Seeding, 100%", button.getContentDescription());
        assertEquals("example, Seeding, 100%", ViewCompat.getStateDescription(button));
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
