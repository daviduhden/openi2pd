/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: EnglishOnlyResourceTest.
 *
 * Instrumentation test that builds locale-overridden contexts for German,
 * Spanish, French, Turkish and Arabic and asserts a sample of string resources
 * still resolves to the English values. It never mutates the process locale and
 * is intended for the manual device job.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;

import android.content.Context;
import android.content.res.Configuration;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.Locale;

/**
 * The Android product ships English-only resources. A non-English device
 * locale must not select a translation catalog; strings fall back to
 * res/values and numeric formatting in UiFormat is pinned to English.
 *
 * <p>This test uses createConfigurationContext rather than changing the
 * process/device default locale. It is prepared for the manual device job and
 * is not run by the static local workflow.</p>
 */
@RunWith(AndroidJUnit4.class)
public final class EnglishOnlyResourceTest {
    private static final int[] ENGLISH_STRINGS = {
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
    public void nonEnglishLocalesFallBackToEnglishResources() {
        Context base = ApplicationProvider.getApplicationContext();
        String[] expected = new String[ENGLISH_STRINGS.length];
        for (int i = 0; i < ENGLISH_STRINGS.length; i++) {
            expected[i] = base.getString(ENGLISH_STRINGS[i]);
        }

        for (String tag : new String[] {"de-DE", "es-ES", "fr-FR", "tr-TR", "ar-EG"}) {
            Configuration configuration = new Configuration(base.getResources().getConfiguration());
            configuration.setLocale(Locale.forLanguageTag(tag));
            Context localized = base.createConfigurationContext(configuration);
            for (int i = 0; i < ENGLISH_STRINGS.length; i++) {
                assertEquals("locale " + tag + " changed string resource " + i, expected[i],
                        localized.getString(ENGLISH_STRINGS[i]));
            }
        }
    }
}
