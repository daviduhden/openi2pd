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
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

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
    }
}
