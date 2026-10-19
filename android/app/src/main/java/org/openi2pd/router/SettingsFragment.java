/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SettingsFragment.
 *
 * {@link PreferenceFragmentCompat} over a validated subset of {@code i2pd.conf};
 * keys naming key material or passwords are deliberately absent. File reads
 * and writes run on a single daemon background executor with results applied
 * back on the main thread, and each change reports whether it is hot, needs a
 * restart or needs a tunnel reload.
 *
 */

package org.openi2pd.router;

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.lifecycle.ViewModelProvider;
import androidx.preference.EditTextPreference;
import androidx.preference.ListPreference;
import androidx.preference.PreferenceFragmentCompat;
import androidx.preference.SwitchPreferenceCompat;

import com.google.android.material.dialog.MaterialAlertDialogBuilder;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Grouped, validated settings backed by {@link RouterConfigStore}.
 *
 * <p>Only configuration keys are shown. Key files, passwords and other secret
 * material are intentionally not exposed. Changes report whether they are hot,
 * require a restart, or require a tunnel reload.</p>
 *
 * <p>The configuration file is read and written on one background executor.
 * Preferences are only updated back on the main thread, so opening or editing
 * settings never performs file I/O on the UI thread.</p>
 */
public class SettingsFragment extends PreferenceFragmentCompat {
    private static final ExecutorService CONFIG_IO = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "i2pd-settings-io");
        thread.setDaemon(true);
        return thread;
    });

    private enum Kind { TEXT, ADDRESS, PORT, POSITIVE_INTEGER }

    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private RouterViewModel viewModel;
    private RouterConfigStore store;
    private Map<String, String> configValues = Collections.emptyMap();
    private boolean restartDialogShown;
    private boolean viewDestroyed;
    private int loadGeneration;

    @Override
    public void onCreatePreferences(@Nullable Bundle savedInstanceState, @Nullable String rootKey) {
        setPreferencesFromResource(R.xml.preferences_root, rootKey);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        viewModel = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);
        viewDestroyed = false;
        loadPreferences();
    }

    @Override
    public void onDestroyView() {
        viewDestroyed = true;
        loadGeneration++;
        restartDialogShown = false;
        mainHandler.removeCallbacksAndMessages(null);
        super.onDestroyView();
    }

    @Override
    public void onResume() {
        super.onResume();
        refreshAutostart();
    }

    private void loadPreferences() {
        Context appContext = requireContext().getApplicationContext();
        if (appContext == null) {
            appContext = requireContext();
        }
        final Context context = appContext;
        final int generation = ++loadGeneration;

        CONFIG_IO.execute(() -> {
            RouterConfigStore localStore = RouterConfigStore.get(context);
            Map<String, String> values = new HashMap<>();
            values.put("ipv4", localStore.getString("ipv4", "true"));
            values.put("ipv6", localStore.getString("ipv6", "false"));
            values.put("ntcp2.enabled", localStore.getString("ntcp2.enabled", "true"));
            values.put("ntcp2.published", localStore.getString("ntcp2.published", "true"));
            values.put("ssu2.enabled", localStore.getString("ssu2.enabled", "true"));
            values.put("notransit", localStore.getString("notransit", "false"));
            values.put("floodfill", localStore.getString("floodfill", "false"));
            values.put(
                    "limits.transittunnels", localStore.getString("limits.transittunnels", "3000"));
            values.put("bandwidth", localStore.getString("bandwidth", "L"));
            values.put("log", localStore.getString("log", "info"));
            values.put("httpproxy.enabled", localStore.getString("httpproxy.enabled", "true"));
            values.put("httpproxy.address", localStore.getString("httpproxy.address", "127.0.0.1"));
            values.put("httpproxy.port", localStore.getString("httpproxy.port", "4444"));
            values.put("socksproxy.enabled", localStore.getString("socksproxy.enabled", "false"));
            values.put(
                    "socksproxy.address", localStore.getString("socksproxy.address", "127.0.0.1"));
            values.put("socksproxy.port", localStore.getString("socksproxy.port", "4447"));
            values.put("sam.enabled", localStore.getString("sam.enabled", "false"));
            values.put("sam.address", localStore.getString("sam.address", "127.0.0.1"));
            values.put("sam.port", localStore.getString("sam.port", "7656"));
            values.put("upnp.enabled", localStore.getString("upnp.enabled", "true"));
            values.put("upnp.name", localStore.getString("upnp.name", "openi2pd"));
            values.put("port", localStore.getString("port", "auto"));
            boolean autostart = AutoStartController.isEnabled(context);

            mainHandler.post(() -> {
                if (viewDestroyed || !isAdded() || generation != loadGeneration) {
                    return;
                }
                store = localStore;
                configValues = values;
                bindPreferences(autostart);
            });
        });
    }

    private void bindPreferences(boolean autostart) {
        bindAutostart(autostart);

        bindSwitch("ipv4EnablePreference", "ipv4", true);
        bindSwitch("ipv6EnablePreference", "ipv6", false);
        bindSwitch("ntcp2EnablePreference", "ntcp2.enabled", true);
        bindSwitch("ntcp2PublishPreference", "ntcp2.published", true);
        bindSwitch("ssuEnablePreference", "ssu2.enabled", true);
        bindDisabledPort();

        bindSwitch("noTransitPreference", "notransit", false);
        bindSwitch("floodfillPreference", "floodfill", false);
        bindText("transitTunnelPreference", "limits.transittunnels", "3000", Kind.POSITIVE_INTEGER);

        bindList("bandwidthPreference", "bandwidth", "L");
        bindList("logLevelPreference", "log", "info");

        bindSwitch("httpProxyEnablePreference", "httpproxy.enabled", true);
        bindText("httpProxyAddressPreference", "httpproxy.address", "127.0.0.1", Kind.ADDRESS);
        bindText("httpProxyPortPreference", "httpproxy.port", "4444", Kind.PORT);

        bindSwitch("socksProxyEnablePreference", "socksproxy.enabled", false);
        bindText("socksProxyAddressPreference", "socksproxy.address", "127.0.0.1", Kind.ADDRESS);
        bindText("socksProxyPortPreference", "socksproxy.port", "4447", Kind.PORT);

        bindSwitch("samEnablePreference", "sam.enabled", false);
        bindText("samAddressPreference", "sam.address", "127.0.0.1", Kind.ADDRESS);
        bindText("samPortPreference", "sam.port", "7656", Kind.PORT);

        bindSwitch("upnpEnablePreference", "upnp.enabled", true);
        bindText("upnpForwardNamePreference", "upnp.name", "openi2pd", Kind.TEXT);
    }

    private void bindAutostart(boolean enabled) {
        SwitchPreferenceCompat preference = findPreference("autostartEnabled");
        if (preference == null) {
            return;
        }
        preference.setChecked(enabled);
        preference.setOnPreferenceChangeListener((changed, newValue) -> {
            if (!(newValue instanceof Boolean enabledValue)) {
                return false;
            }
            boolean previous = preference.isChecked();
            Context appContext = requireContext().getApplicationContext();
            final int generation = loadGeneration;
            CONFIG_IO.execute(() -> {
                boolean applied = AutoStartController.setEnabled(appContext, enabledValue, false);
                mainHandler.post(() -> {
                    if (viewDestroyed || !isAdded() || generation != loadGeneration) {
                        return;
                    }
                    if (!applied) {
                        Toast.makeText(requireContext(), R.string.pref_autostart_failed,
                                     Toast.LENGTH_SHORT)
                                .show();
                        preference.setChecked(previous);
                        return;
                    }
                    if (enabledValue) {
                        AutoStartController.openVendorAutostartSettings(requireContext());
                    }
                });
            });
            return true;
        });
    }

    private void refreshAutostart() {
        if (getPreferenceScreen() == null) {
            return;
        }
        SwitchPreferenceCompat preference = findPreference("autostartEnabled");
        if (preference == null) {
            return;
        }
        Context appContext = requireContext().getApplicationContext();
        CONFIG_IO.execute(() -> {
            boolean enabled = AutoStartController.isEnabled(appContext);
            mainHandler.post(() -> {
                if (viewDestroyed || !isAdded()) {
                    return;
                }
                SwitchPreferenceCompat current = findPreference("autostartEnabled");
                if (current != null) {
                    current.setChecked(enabled);
                }
            });
        });
    }

    private void bindSwitch(@NonNull String key, @NonNull String configKey, boolean defaultValue) {
        SwitchPreferenceCompat preference = findPreference(key);
        if (preference == null) {
            return;
        }
        preference.setChecked(
                RouterConfigStore.parseBoolean(configValues.get(configKey), defaultValue));
        preference.setOnPreferenceChangeListener((changed, newValue) -> {
            if (!(newValue instanceof Boolean value)) {
                return false;
            }
            boolean previous = preference.isChecked();
            persist(configKey, Boolean.toString(value),
                    () -> showApplyMode(configKey), () -> preference.setChecked(previous));
            return true;
        });
    }

    private void bindList(
            @NonNull String key, @NonNull String configKey, @NonNull String defaultValue) {
        ListPreference preference = findPreference(key);
        if (preference == null) {
            return;
        }
        String current = configValues.get(configKey);
        if (current != null) {
            preference.setValue(current);
        }
        preference.setSummaryProvider(ListPreference.SimpleSummaryProvider.getInstance());
        preference.setOnPreferenceChangeListener((changed, newValue) -> {
            if (newValue == null) {
                return false;
            }
            String value = newValue.toString().trim();
            if (!RouterConfigStore.isSafeValue(value)) {
                Toast.makeText(requireContext(), R.string.pref_save_failed, Toast.LENGTH_SHORT)
                        .show();
                return false;
            }
            String previous = preference.getValue();
            persist(configKey, value,
                    () -> showApplyMode(configKey), () -> preference.setValue(previous));
            return true;
        });
    }

    private void bindText(@NonNull String key, @NonNull String configKey,
            @NonNull String defaultValue, @NonNull Kind kind) {
        EditTextPreference preference = findPreference(key);
        if (preference == null) {
            return;
        }
        String current = configValues.get(configKey);
        preference.setText(current == null ? defaultValue : current);
        preference.setSummaryProvider(EditTextPreference.SimpleSummaryProvider.getInstance());
        preference.setOnPreferenceChangeListener((changed, newValue) -> {
            String value = newValue == null ? "" : newValue.toString().trim();
            if (!isValid(value, kind)) {
                Toast.makeText(requireContext(), validationMessage(kind), Toast.LENGTH_SHORT)
                        .show();
                return false;
            }
            String previous = preference.getText();
            persist(configKey, value,
                    () -> showApplyMode(configKey), () -> preference.setText(previous));
            return true;
        });
    }

    private void bindDisabledPort() {
        EditTextPreference preference = findPreference("portPreference");
        if (preference == null) {
            return;
        }
        String port = configValues.get("port");
        preference.setText(port == null ? "auto" : port);
        preference.setEnabled(false);
        preference.setSummary(R.string.pref_port_summary);
    }

    private void persist(@NonNull String key, @NonNull String value, @NonNull Runnable onSuccess,
            @NonNull Runnable onFailure) {
        RouterConfigStore localStore = store;
        if (localStore == null) {
            onFailure.run();
            return;
        }
        final int generation = loadGeneration;
        CONFIG_IO.execute(() -> {
            boolean saved = localStore.setOption(key, value);
            mainHandler.post(() -> {
                if (viewDestroyed || !isAdded() || generation != loadGeneration) {
                    return;
                }
                if (saved) {
                    onSuccess.run();
                } else {
                    Toast.makeText(requireContext(), R.string.pref_save_failed, Toast.LENGTH_SHORT)
                            .show();
                    onFailure.run();
                }
            });
        });
    }

    private boolean isValid(@NonNull String value, @NonNull Kind kind) {
        if (value.isEmpty() || value.length() > 512 || !RouterConfigStore.isSafeValue(value)) {
            return false;
        }
        return switch (kind) {
            case PORT -> isPort(value);
            case ADDRESS -> isAddress(value);
            case POSITIVE_INTEGER -> isPositiveInteger(value);
            case TEXT -> true;
        };
    }

    private static boolean isPort(@NonNull String value) {
        if ("auto".equals(value)) {
            return true;
        }
        if (value.length() > 5) {
            return false;
        }
        for (int i = 0; i < value.length(); i++) {
            if (!Character.isDigit(value.charAt(i))) {
                return false;
            }
        }
        int port = Integer.parseInt(value);
        return port >= 0 && port <= 65535;
    }

    private static boolean isPositiveInteger(@NonNull String value) {
        if (value.isEmpty() || value.length() > 7) {
            return false;
        }
        for (int i = 0; i < value.length(); i++) {
            if (!Character.isDigit(value.charAt(i))) {
                return false;
            }
        }
        int parsed = Integer.parseInt(value);
        return parsed > 0 && parsed <= 1_000_000;
    }

    private static boolean isAddress(@NonNull String value) {
        if (value.length() > 255) {
            return false;
        }
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            boolean allowed = Character.isLetterOrDigit(c) || c == '.' || c == ':' || c == '-'
                    || c == '_' || c == '[' || c == ']' || c == '%';
            if (!allowed) {
                return false;
            }
        }
        return true;
    }

    @StringRes
    private int validationMessage(@NonNull Kind kind) {
        return switch (kind) {
            case PORT -> R.string.pref_invalid_port;
            case ADDRESS -> R.string.pref_invalid_address;
            case POSITIVE_INTEGER -> R.string.pref_invalid_positive;
            case TEXT -> R.string.pref_invalid_value;
        };
    }

    private void showApplyMode(@NonNull String configKey) {
        RouterConfigStore localStore = store;
        if (localStore == null) {
            return;
        }
        RouterConfigStore.ApplyMode mode = localStore.applyModeFor(configKey);
        if (mode == null) {
            return;
        }
        switch (mode) {
        case HOT ->
            Toast.makeText(requireContext(), R.string.pref_apply_hot, Toast.LENGTH_SHORT).show();
        case RELOAD_TUNNELS ->
            Toast.makeText(requireContext(), R.string.pref_apply_reload, Toast.LENGTH_SHORT).show();
        case RESTART -> {
            Toast.makeText(requireContext(), R.string.pref_apply_restart, Toast.LENGTH_SHORT)
                    .show();
            showRestartDialog();
        }
        }
    }

    private void showRestartDialog() {
        if (restartDialogShown || !isAdded() || viewModel == null) {
            return;
        }
        restartDialogShown = true;
        MaterialAlertDialogBuilder builder = new MaterialAlertDialogBuilder(requireContext());
        builder.setTitle(R.string.pref_restart_required_title);
        builder.setMessage(R.string.pref_restart_required_message);
        builder.setNegativeButton(R.string.pref_later, null);
        builder.setPositiveButton(
                R.string.pref_restart_now, (dialog, which) -> viewModel.restartRouter());
        builder.setOnDismissListener(dialog -> restartDialogShown = false);
        builder.show();
    }
}
