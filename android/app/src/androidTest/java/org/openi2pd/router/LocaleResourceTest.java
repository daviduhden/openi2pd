/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: LocaleResourceTest.
 *
 * Instrumentation test that builds locale-overridden contexts for
 * supported Spanish, Russian and Simplified Chinese catalogs, checks
 * unsupported locales fall back to English, and verifies the active UI
 * locale without mutating the process locale.
 *
 * It is intended for the manual device job.
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

/*
 * The Android product ships English, Spanish, Russian and Simplified
 * Chinese UI catalogs. Unsupported locales must fall back to English;
 * supported locales must resolve a translated catalog.
 *
 * <p>The test uses createConfigurationContext rather than changing the
 * process/device default locale and is prepared for the manual device
 * job.</p>
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
            R.string.notification_channel_name,
            R.string.notification_channel_description,
            R.string.torrent_creation_failed_unknown,
            R.string.torrent_create_source_invalid,
            R.string.log_row_description,
            R.string.torrent_status_downloading,
            R.string.torrent_status_seeding,
            R.string.torrent_error_random,
            R.string.operation_permission_denied,
            R.string.operation_timed_out,
            R.string.torrent_invalid_tracker,
            R.string.router_status_firewalled,
    };

    @Test
    public void supportedLocalesUseTheirCatalogsAndOthersFallBack() {
        Context base = ApplicationProvider.getApplicationContext();
        Context englishContext = localized(base, "en");
        String[] english = new String[SAMPLE_STRINGS.length];
        for (int i = 0; i < SAMPLE_STRINGS.length; i++) {
            english[i] = englishContext.getString(SAMPLE_STRINGS[i]);
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

        // Traditional Chinese is deliberately not shipped: a zh-TW or
        // zh-Hant device must fall back to English rather than show
        // Simplified text as if it were Traditional.
        for (String tag : new String[] {"zh-TW", "zh-Hant", "zh-HK"}) {
            Configuration configuration = new Configuration(base.getResources().getConfiguration());
            configuration.setLocale(Locale.forLanguageTag(tag));
            Context localized = base.createConfigurationContext(configuration);
            for (int i = 0; i < SAMPLE_STRINGS.length; i++) {
                assertEquals("Traditional Chinese locale " + tag + " must fall back to English",
                        english[i], localized.getString(SAMPLE_STRINGS[i]));
            }
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

    @Test
    public void russianPluralCategoriesSelectDistinctForms() {
        Context base = ApplicationProvider.getApplicationContext();
        Configuration configuration = new Configuration(base.getResources().getConfiguration());
        configuration.setLocale(Locale.forLanguageTag("ru-RU"));
        Context ru = base.createConfigurationContext(configuration);
        // Compare templates, not formatted counts. Different digits
        // would make even an incorrect one-form catalog pass.
        String one = ru.getResources().getQuantityString(R.plurals.torrent_count, 1);
        String few = ru.getResources().getQuantityString(R.plurals.torrent_count, 2);
        String many = ru.getResources().getQuantityString(R.plurals.torrent_count, 5);
        assertTrue("Russian one/few forms must differ: " + one + " / " + few, !one.equals(few));
        assertTrue("Russian few/many forms must differ: " + few + " / " + many, !few.equals(many));
        assertEquals(one, ru.getResources().getQuantityString(R.plurals.torrent_count, 21));
        assertEquals(few, ru.getResources().getQuantityString(R.plurals.torrent_count, 22));
        assertEquals(many, ru.getResources().getQuantityString(R.plurals.torrent_count, 11));
        // Spanish has one/other; Simplified Chinese uses one text form.
        Configuration esConfig = new Configuration(base.getResources().getConfiguration());
        esConfig.setLocale(Locale.forLanguageTag("es-ES"));
        Context es = base.createConfigurationContext(esConfig);
        assertTrue("Spanish plural forms must differ",
                !es.getResources()
                        .getQuantityString(R.plurals.torrent_count, 1)
                        .equals(es.getResources().getQuantityString(R.plurals.torrent_count, 2)));
        Context zh = localized(base, "zh-CN");
        String chinese = zh.getResources().getQuantityString(R.plurals.torrent_count, 1);
        assertEquals(chinese, zh.getResources().getQuantityString(R.plurals.torrent_count, 2));
        assertEquals(chinese, zh.getResources().getQuantityString(R.plurals.torrent_count, 5));
    }

    @Test
    public void presentationVocabularyLocalizesWithoutChangingOpaqueData() {
        Context base = ApplicationProvider.getApplicationContext();
        String opaque = "資料-Русский-español-\uD83D\uDE80";
        for (String tag : new String[] {"en", "es-ES", "ru-RU", "zh-CN"}) {
            Context context = localized(base, tag);
            assertEquals(context.getString(R.string.torrent_status_downloading),
                    UiText.torrentStatus(context, "downloading"));
            assertEquals(context.getString(R.string.torrent_error_random),
                    UiText.diagnostic(context, "Random source failure"));
            assertEquals(context.getString(R.string.operation_permission_denied),
                    UiText.diagnostic(
                            context, RouterErrorText.describe(new SecurityException(opaque))));
            assertEquals(context.getString(R.string.router_status_firewalled),
                    UiText.routerStatus(context, "Firewalled"));
            assertEquals(opaque, UiText.diagnostic(context, opaque));
            assertEquals("future-state", UiText.torrentStatus(context, "future-state"));
            String formatted = context.getString(R.string.torrent_file_progress, opaque, "999");
            assertTrue(formatted.contains(opaque) && formatted.contains("999"));
            assertEquals(UiFormat.count(context, 12345), UiText.counter(context, "12345"));
            assertEquals(
                    java.text.NumberFormat
                            .getPercentInstance(
                                    context.getResources().getConfiguration().getLocales().get(0))
                            .format(0.42),
                    UiText.percent(context, 42));
        }
    }

    @Test
    public void unsupportedLocalePresentationUsesEnglishResources() {
        Context base = ApplicationProvider.getApplicationContext();
        Context english = localized(base, "en");
        for (String tag : new String[] {"zh-Hant", "zh-TW", "fr-FR", "de-DE"}) {
            Context context = localized(base, tag);
            assertEquals(english.getString(R.string.torrent_status_seeding),
                    UiText.torrentStatus(context, "seeding"));
            assertEquals(english.getString(R.string.operation_cancelled),
                    UiText.diagnostic(context, "Operation cancelled"));
        }
    }

    private static Context localized(Context base, String tag) {
        Configuration configuration = new Configuration(base.getResources().getConfiguration());
        configuration.setLocale(Locale.forLanguageTag(tag));
        return base.createConfigurationContext(configuration);
    }

    @Test
    public void explicitLocaleVariantsHaveCanonicalPreferenceValues() {
        assertEquals("es", UiText.languageChoice(Locale.forLanguageTag("es-MX")));
        assertEquals("ru", UiText.languageChoice(Locale.forLanguageTag("ru-RU")));
        assertEquals("zh-CN", UiText.languageChoice(Locale.forLanguageTag("zh-Hans")));
        assertEquals("zh-CN", UiText.languageChoice(Locale.forLanguageTag("zh-SG")));
        assertEquals("en", UiText.languageChoice(Locale.forLanguageTag("zh-Hant-CN")));
        assertEquals("en", UiText.languageChoice(Locale.forLanguageTag("zh-Latn-CN")));
        assertEquals("en", UiText.languageChoice(Locale.forLanguageTag("zh-TW")));
        assertEquals("en", UiText.languageChoice(Locale.forLanguageTag("de-DE")));
    }
}
