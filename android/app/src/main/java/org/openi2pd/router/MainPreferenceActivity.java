package org.openi2pd.router;

import android.os.Build;
import android.os.Bundle;
import android.preference.CheckBoxPreference;
import android.preference.EditTextPreference;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.util.Log;
import android.widget.Toast;

import org.apache.commons.configuration2.INIConfiguration;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.StringReader;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Predicate;

/**
 * Editor for the daemon's {@code i2pd.conf}.
 *
 * <p>The file lives in app-specific storage and is written atomically. All
 * values are validated before they are inserted into the INI file so a value
 * cannot inject a new option or section.</p>
 */
public class MainPreferenceActivity extends PreferenceActivity {
    private static final String TAG = "i2pdPrefs";
    private static final String CONFIG_FILE_NAME = "i2pd.conf";
    private static final String OLD_FILE_PREFIX = "__old";
    private static final long MAX_CONFIG_BYTES = 1024L * 1024L;
    private static final int MAX_VALUE_LENGTH = 512;
    private static final int MAX_TRANSIT_TUNNELS = 1_000_000;

    private File configFile;
    private INIConfiguration properties;
    private boolean configWritable;
    private String originalConfigText = "";

    private enum ValueKind {
        TEXT,
        ADDRESS,
        PORT,
        KEY_NAME,
        POSITIVE_INTEGER,
        FORWARD_NAME
    }

    private record TextOption(String preferenceKey, String configKey, String defaultValue,
                              ValueKind kind) {
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        try {
            configFile = DaemonWrapper.resolveDataFile(this, CONFIG_FILE_NAME);
        } catch (IOException | RuntimeException | LinkageError e) {
            Log.e(TAG, "refusing invalid configuration path", e);
            Toast.makeText(this, R.string.config_path_invalid, Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        properties = readConfiguration();
        backupConfiguration();
        addPreferencesFromResource(R.xml.preferences_i2pd);
        removeEmptySectionsIfNeeded();
        bindPreferences();
    }

    private INIConfiguration readConfiguration() {
        INIConfiguration configuration = new INIConfiguration();
        if (!configFile.isFile()) {
            configWritable = true;
            return configuration;
        }
        try {
            String text = readBoundedText(configFile);
            originalConfigText = text;
            configuration.read(new StringReader(text));
            configWritable = true;
        } catch (Exception e) {
            configWritable = false;
            Log.e(TAG, "unable to read configuration", e);
            Toast.makeText(this, R.string.config_read_failed, Toast.LENGTH_LONG).show();
        }
        return configuration;
    }

    private static String readBoundedText(File file) throws IOException {
        long length = file.length();
        if (length > MAX_CONFIG_BYTES) {
            throw new IOException("configuration file is too large");
        }
        StringBuilder text = new StringBuilder((int) Math.min(length, 64L * 1024L));
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            char[] buffer = new char[4096];
            int read;
            while ((read = reader.read(buffer)) != -1) {
                text.append(buffer, 0, read);
                if (text.length() > MAX_CONFIG_BYTES) {
                    throw new IOException("configuration file is too large");
                }
            }
        }
        return text.toString();
    }

    private void backupConfiguration() {
        if (!configWritable || !configFile.isFile()) {
            return;
        }
        File backup = new File(configFile.getParentFile(), configFile.getName() + OLD_FILE_PREFIX);
        if (backup.exists()) {
            return;
        }
        try (FileInputStream in = new FileInputStream(configFile);
             FileOutputStream out = new FileOutputStream(backup)) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        } catch (IOException e) {
            Log.e(TAG, "unable to back up configuration", e);
        }
    }

    private void removeEmptySectionsIfNeeded() {
        if (!configWritable || !configFile.isFile()) {
            return;
        }
        String serialized = serializeConfiguration();
        if (serialized == null || serialized.equals(originalConfigText)) {
            return;
        }
        if (writeFile(serialized)) {
            originalConfigText = serialized;
        }
    }

    private void bindPreferences() {
        bindList("logLevelPreference", "log", "info");
        bindCheckBox("ipv4EnablePreference", "ipv4", "true");
        bindCheckBox("ipv6EnablePreference", "ipv6", "false");

        EditTextPreference port = requirePreference("portPreference", EditTextPreference.class);
        port.setText(properties.getString("port", "auto"));
        port.setOnPreferenceChangeListener((preference, newValue) -> {
            Toast.makeText(MainPreferenceActivity.this,
                    R.string.for_security_reasons_changes_are_not_allowed,
                    Toast.LENGTH_SHORT).show();
            return false;
        });

        bindList("bandwidthPreference", "bandwidth", "L");
        bindCheckBox("noTransitPreference", "notransit", "false");
        bindCheckBox("floodfillPreference", "floodfill", "false");
        bindCheckBox("ssuPreference", "ssu2.enabled", "true");

        bindCheckBox("ntcp2EnablePreference", "ntcp2.enabled", "true");
        bindCheckBox("ntcp2PublishPreference", "ntcp2.published", "true");

        bindCheckBox("httpProxyEnablePreference", "httpproxy.enabled", "true");
        bindText("httpProxyAddressPreference", "httpproxy.address", "127.0.0.1", ValueKind.ADDRESS);
        bindText("httpProxyPortPreference", "httpproxy.port", "4444", ValueKind.PORT);
        bindText("httpProxyKeysPreference", "httpproxy.keys", "transient", ValueKind.KEY_NAME);

        bindCheckBox("socksProxyEnablePreference", "socksproxy.enabled", "true");
        bindText("socksProxyAddressPreference", "socksproxy.address", "127.0.0.1", ValueKind.ADDRESS);
        bindText("socksProxyPortPreference", "socksproxy.port", "4447", ValueKind.PORT);
        bindText("socksProxyKeysPreference", "socksproxy.keys", "transient", ValueKind.KEY_NAME);

        bindCheckBox("samEnablePreference", "sam.enabled", "true");
        bindText("samAddressPreference", "sam.address", "127.0.0.1", ValueKind.ADDRESS);
        bindText("samPortPreference", "sam.port", "7656", ValueKind.PORT);

        bindCheckBox("upnpEnablePreference", "upnp.enabled", "true");
        bindText("upnpForwardNamePreference", "upnp.name", "I2Pd", ValueKind.FORWARD_NAME);

        bindText("transitTunnelPreference", "limits.transittunnels", "3000",
                ValueKind.POSITIVE_INTEGER);
    }

    private void bindList(String preferenceKey, String configKey, String defaultValue) {
        ListPreference preference = requirePreference(preferenceKey, ListPreference.class);
        String value = properties.getString(configKey, defaultValue);
        preference.setValue(value);
        preference.setOnPreferenceChangeListener((changed, newValue) -> {
            if (newValue == null) {
                return false;
            }
            return writeOption(configKey, newValue.toString());
        });
    }

    private void bindCheckBox(String preferenceKey, String configKey, String defaultValue) {
        CheckBoxPreference preference = requirePreference(preferenceKey, CheckBoxPreference.class);
        preference.setChecked(Boolean.parseBoolean(properties.getString(configKey, defaultValue)));
        preference.setOnPreferenceChangeListener((changed, newValue) -> {
            if (!(newValue instanceof Boolean value)) {
                return false;
            }
            return writeOption(configKey, value.toString());
        });
    }

    private void bindText(String preferenceKey, String configKey, String defaultValue,
                          ValueKind kind) {
        TextOption option = new TextOption(preferenceKey, configKey, defaultValue, kind);
        EditTextPreference preference =
                requirePreference(option.preferenceKey(), EditTextPreference.class);
        String value = properties.getString(option.configKey(), option.defaultValue());
        preference.setText(value);
        preference.setOnPreferenceChangeListener((changed, newValue) -> {
            String text = newValue == null ? "" : newValue.toString().trim();
            if (!isValidValue(text, option.kind())) {
                Toast.makeText(MainPreferenceActivity.this,
                        R.string.invalid_config_value, Toast.LENGTH_SHORT).show();
                return false;
            }
            return writeOption(option.configKey(), text);
        });
    }

    private static Predicate<String> validatorFor(ValueKind kind) {
        return switch (kind) {
            case PORT -> MainPreferenceActivity::isValidPort;
            case POSITIVE_INTEGER -> MainPreferenceActivity::isValidPositiveInteger;
            case ADDRESS -> value -> !value.isEmpty() && value.length() <= 255;
            case KEY_NAME -> value -> !value.isEmpty() && value.length() <= 255
                    && !value.contains("..");
            case FORWARD_NAME, TEXT -> value -> !value.isEmpty();
        };
    }

    private static boolean isValidValue(String value, ValueKind kind) {
        return isSafeConfigValue(value) && validatorFor(kind).test(value);
    }

    private static boolean isSafeConfigValue(String value) {
        if (value == null || value.isEmpty() || value.length() > MAX_VALUE_LENGTH) {
            return false;
        }
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c == '\r' || c == '\n' || c == '=' || c == '[' || c == ']' || c == '#'
                    || c == ';' || Character.isISOControl(c)) {
                return false;
            }
        }
        return true;
    }

    private static boolean isValidPort(String value) {
        if ("auto".equals(value)) {
            return true;
        }
        if (value.isEmpty() || value.length() > 5) {
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

    private static boolean isValidPositiveInteger(String value) {
        if (value.isEmpty() || value.length() > 7) {
            return false;
        }
        for (int i = 0; i < value.length(); i++) {
            if (!Character.isDigit(value.charAt(i))) {
                return false;
            }
        }
        int parsed = Integer.parseInt(value);
        return parsed > 0 && parsed <= MAX_TRANSIT_TUNNELS;
    }

    private <T extends Preference> T requirePreference(String key, Class<T> type) {
        Preference preference = findPreference(key);
        if (preference == null) {
            throw new IllegalStateException("missing preference: " + key);
        }
        if (!type.isInstance(preference)) {
            throw new IllegalStateException("unexpected preference type for " + key);
        }
        return type.cast(preference);
    }

    private boolean writeOption(String option, String value) {
        if (!configWritable) {
            Toast.makeText(this, R.string.config_read_failed, Toast.LENGTH_SHORT).show();
            return false;
        }
        if (!isSafeConfigValue(option) || !isSafeConfigValue(value)) {
            Toast.makeText(this, R.string.invalid_config_value, Toast.LENGTH_SHORT).show();
            return false;
        }
        String serialized = serializeConfiguration();
        if (serialized == null) {
            Toast.makeText(this, R.string.config_write_failed, Toast.LENGTH_SHORT).show();
            return false;
        }
        serialized = removeOptionLine(serialized, option);
        String content = option + "=" + value + "\n" + serialized;
        if (!writeFile(content)) {
            Toast.makeText(this, R.string.config_write_failed, Toast.LENGTH_SHORT).show();
            return false;
        }
        try {
            properties.setProperty(option, value);
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to update in-memory configuration", e);
        }
        originalConfigText = content;
        return true;
    }

    private String serializeConfiguration() {
        try {
            StringWriter writer = new StringWriter();
            properties.write(writer);
            return removeEmptySections(writer.toString());
        } catch (Exception e) {
            Log.e(TAG, "unable to serialise configuration", e);
            return null;
        }
    }

    private boolean writeFile(String content) {
        byte[] bytes = content.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > MAX_CONFIG_BYTES) {
            Log.e(TAG, "refusing to write oversized configuration");
            return false;
        }
        File directory = configFile.getParentFile();
        if (directory == null || !directory.isDirectory()) {
            Log.e(TAG, "configuration directory is not available");
            return false;
        }
        File temporary = new File(directory, configFile.getName() + ".tmp");
        try {
            try (FileOutputStream out = new FileOutputStream(temporary)) {
                out.write(bytes);
                out.flush();
                out.getFD().sync();
            }
            replaceFile(temporary, configFile);
            return true;
        } catch (IOException e) {
            Log.e(TAG, "unable to write configuration", e);
            return false;
        } finally {
            if (temporary.exists() && !temporary.delete()) {
                Log.w(TAG, "unable to delete temporary configuration file");
            }
        }
    }

    private static void replaceFile(File source, File target) throws IOException {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            try {
                Files.move(source.toPath(), target.toPath(),
                        StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
                return;
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
                return;
            }
        }
        // java.nio.file is only guaranteed from API 26; rename(2) is atomic on
        // the app-private Linux filesystem used by older releases.
        if (!source.renameTo(target)) {
            throw new IOException("unable to replace configuration file");
        }
    }

    /**
     * Remove every assignment of {@code option} from the serialized file. This
     * is line based instead of a regular expression so dots in option names
     * cannot match arbitrary characters.
     */
    private static String removeOptionLine(String content, String option) {
        String[] lines = content.split("\n", -1);
        int limit = lines.length;
        if (limit > 0 && lines[limit - 1].isEmpty()) {
            limit--;
        }
        StringBuilder result = new StringBuilder(content.length());
        boolean first = true;
        for (int i = 0; i < limit; i++) {
            String line = lines[i];
            String trimmed = line.trim();
            if (trimmed.startsWith(option)) {
                int index = option.length();
                while (index < trimmed.length()
                        && Character.isWhitespace(trimmed.charAt(index))) {
                    index++;
                }
                if (index < trimmed.length() && trimmed.charAt(index) == '=') {
                    continue;
                }
            }
            if (!first) {
                result.append('\n');
            }
            result.append(line);
            first = false;
        }
        if (content.endsWith("\n")) {
            result.append('\n');
        }
        return result.toString();
    }

    private static String removeEmptySections(String content) {
        String[] lines = content.split("\n", -1);
        List<String> output = new ArrayList<>(lines.length);
        for (int i = 0; i < lines.length; i++) {
            String line = lines[i];
            if (!isSectionHeader(line)) {
                output.add(line);
                continue;
            }
            int next = i + 1;
            while (next < lines.length && lines[next].trim().isEmpty()) {
                next++;
            }
            if (next >= lines.length || isSectionHeader(lines[next])) {
                i = next - 1;
                continue;
            }
            output.add(line);
        }
        return String.join("\n", output);
    }

    private static boolean isSectionHeader(String line) {
        String trimmed = line.trim();
        return trimmed.length() > 2 && trimmed.charAt(0) == '[' && trimmed.endsWith("]");
    }
}
