/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LocaleResourceTest.
 *
 * Instrumentation test that builds locale-overridden contexts for supported
 * Spanish, Russian and Simplified Chinese catalogs, checks unsupported locales
 * fall back to English, and verifies the active UI locale without mutating the
 * process locale. It is intended for the manual device job.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.res.Configuration;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.Locale;

/**
 * The Android product ships English, Spanish, Russian and Simplified Chinese
 * UI catalogs. Unsupported locales must fall back to English; supported
 * locales must resolve a translated catalog. The test uses
 * createConfigurationContext rather than changing the process/device default
 * locale and is prepared for the manual device job.
 */
@RunWith(AndroidJUnit4.class)
public final class LocaleResourceTest {
    private static final int[] SAMPLE_STRINGS = {
            R.string.action_start,
            R.string.action_stop,
            R.string.action_exit,
            R.string.action_graceful_stop,
            R.string.action_cancel_graceful_stop,
            R.string.action_reload_tunnels_config,
            R.string.gracefulShutdownInProgress,
            R.string.uninitialized,
            R.string.starting,
            R.string.startedOkay,
            R.string.startFailed,
            R.string.stopped,
            R.string.services_http_proxy,
            R.string.services_socks_proxy,
            R.string.menu_item_battery_optimizations_str,
            R.string.battery_optimizations_enabled,
            R.string.battery_optimizations_enabled_dialog,
            R.string.os_version_does_not_support_battery_optimizations_show_os_dialog_api,
            R.string.tunnels_reloading,
            R.string.autostart_enabled,
    };

    @Test
    public void supportedLocalesUseTheirCatalogsAndOthersFallBack() {
        Context base = ApplicationProvider.getApplicationContext();
        String[] english = new String[SAMPLE_STRINGS.length];
        for (int i = 0; i < SAMPLE_STRINGS.length; i++) {
            english[i] = base.getString(SAMPLE_STRINGS[i]);
        }

        for (String tag : new String[] {"es-ES", "ru-RU", "zh-CN"}) {
            Configuration configuration = new Configuration(base.getResources().getConfiguration());
            configuration.setLocale(Locale.forLanguageTag(tag));
            Context localized = base.createConfigurationContext(configuration);
            boolean translated = false;
            for (int i = 0; i < SAMPLE_STRINGS.length; i++) {
                String value = localized.getString(SAMPLE_STRINGS[i]);
                if (!value.equals(english[i])) {
                    translated = true;
                }
            }
            assertTrue("locale " + tag + " did not select a translated catalog", translated);
        }

        for (String tag : new String[] {"de-DE", "fr-FR", "tr-TR", "ar-EG"}) {
            Configuration configuration = new Configuration(base.getResources().getConfiguration());
            configuration.setLocale(Locale.forLanguageTag(tag));
            Context localized = base.createConfigurationContext(configuration);
            for (int i = 0; i < SAMPLE_STRINGS.length; i++) {
                assertEquals("unsupported locale " + tag + " changed string resource " + i,
                        english[i], localized.getString(SAMPLE_STRINGS[i]));
            }
        }
    }
}
