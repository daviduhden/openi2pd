/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterConfigStore.
 *
 * Singleton accessor for the app-private {@code i2pd.conf} with size-bounded
 * reads and atomic write + fsync + rename updates. The editor is line based so
 * comments and unknown options survive, and keys or values that name secrets
 * or could inject a section or option are rejected.
 *
 */

package org.openi2pd.router;

import android.content.Context;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.Objects;

/**
 * Central safe access to the daemon's {@code i2pd.conf}.
 *
 * <p>Only app-specific storage is accepted. Reads are size bounded and writes
 * are atomic (write + fsync + rename). The editor is line based so comments and
 * unknown options are preserved; it never serialises the whole INI through a
 * object model that could drop keys.</p>
 *
 * <p>Keys and values are validated to prevent section/option injection. Keys
 * that name key material, passwords or other secrets are refused outright:
 * this class must never become a secret store. The resulting configuration is
 * not used to launch an external process; only the in-process router reads
 * it.</p>
 */
public final class RouterConfigStore {
    private static final String TAG = "RouterConfigStore";
    private static final String CONFIG_FILE_NAME = "i2pd.conf";
    private static final String TEMP_SUFFIX = ".tmp";
    private static final long MAX_CONFIG_BYTES = 1024L * 1024L;
    private static final int MAX_KEY_LENGTH = 128;
    private static final int MAX_VALUE_LENGTH = 512;

    private static volatile RouterConfigStore instance;

    public enum ApplyMode {
        /** Takes effect without restarting the router. */
        HOT,
        /** The router must be restarted. */
        RESTART,
        /** Re-read tunnels.d / tunnels.conf without a full restart. */
        RELOAD_TUNNELS
    }

    private final Context appContext;
    private final File configFile;

    // Guarded by the synchronized getString/setOption methods. Repeated
    // getters for one settings screen then read the bounded file once.
    private String cachedText;
    private long cachedModified = Long.MIN_VALUE;
    private long cachedLength = -1L;

    private RouterConfigStore(Context context) {
        Context app = context.getApplicationContext();
        this.appContext = app == null ? context : app;
        this.configFile = resolveConfigFile(appContext);
    }

    /**
     * Process-wide store for the daemon's {@code i2pd.conf}, created once under
     * the class lock and bound to the application context.
     *
     * <p>The returned instance is a file-backed cache: reads and writes go to
     * the configuration file, so call them from a worker. The settings screen
     * keeps all access on one background executor and only touches views back
     * on the main thread.</p>
     */
    public static RouterConfigStore get(Context context) {
        Objects.requireNonNull(context, "context");
        RouterConfigStore local = instance;
        if (local == null) {
            synchronized (RouterConfigStore.class) {
                local = instance;
                if (local == null) {
                    local = new RouterConfigStore(context);
                    instance = local;
                }
            }
        }
        return local;
    }

    /** Read a string option, or {@code def} when absent/invalid. */
    public synchronized String getString(String key, String def) {
        if (!isSafeKey(key) || isSecretKey(key) || configFile == null) {
            return def;
        }
        key = canonicalKey(key);
        String text = readConfigText();
        if (text == null) {
            return def;
        }
        String value = findLastValue(text, key);
        return value == null ? def : value;
    }

    /** Read a boolean option, or {@code def} when absent/invalid. */
    public synchronized boolean getBoolean(String key, boolean def) {
        return parseBoolean(getString(key, null), def);
    }

    /**
     * Parse the INI boolean spellings accepted by i2pd. Kept static and
     * Android-free so the settings loader can parse values off the main thread
     * and so the rule is covered by a pure-JVM test.
     */
    public static boolean parseBoolean(String value, boolean def) {
        if (value == null) {
            return def;
        }
        return switch (value.trim().toLowerCase(Locale.ROOT)) {
            case "1", "true", "yes", "on" -> true;
            case "0", "false", "no", "off" -> false;
            default -> def;
        };
    }

    /**
     * Validate and write one option. Existing comments, sections and unknown
     * keys are preserved. Returns {@code false} for unsafe input, oversized
     * files or I/O failure.
     */
    public synchronized boolean setOption(String key, String value) {
        if (!isSafeKey(key) || !isSafeValue(value) || isSecretKey(key) || configFile == null) {
            return false;
        }
        key = canonicalKey(key);

        String text = readConfigText();
        if (text == null) {
            return false;
        }
        if (text.isEmpty() && !configFile.isFile()) {
            // First edit before the service copied the bundled assets. Seed
            // from the asset so the write does not hide the entire default
            // configuration from the router.
            String seed = readBundledConfig();
            if (seed != null) {
                text = seed;
            }
        }

        String updated = updateOption(text, key, value);
        if (updated == null) {
            return false;
        }
        byte[] bytes = updated.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > MAX_CONFIG_BYTES) {
            Log.e(TAG, "refusing to write oversized configuration");
            return false;
        }
        if (!writeAtomically(bytes)) {
            return false;
        }
        cachedText = updated;
        cachedModified = configFile.lastModified();
        cachedLength = bytes.length;
        return true;
    }

    /**
     * How a changed option becomes visible to the running router.
     *
     * <p>The log level is hot: {@link SettingsFragment} writes the exact same
     * closed value to the file and also hands it to the running logger through
     * {@link RouterService}'s typed worker action, so no restart is needed.
     * Every other option is conservatively {@link ApplyMode#RESTART} except
     * tunnel configuration files.</p>
     */
    public ApplyMode applyModeFor(String key) {
        if (key == null || key.isEmpty()) {
            return ApplyMode.RESTART;
        }
        String lower = canonicalKey(key.toLowerCase(Locale.ROOT));
        if ("loglevel".equals(lower)) {
            return ApplyMode.HOT;
        }
        if (lower.startsWith("tunnels.") || "tunnelsdir".equals(lower) || "tunnels".equals(lower)) {
            return ApplyMode.RELOAD_TUNNELS;
        }
        return ApplyMode.RESTART;
    }

    /**
     * Value validation shared by settings and direct callers.
     *
     * <p>Rejects empty values, control characters and the INI injection
     * characters {@code = # ;}. Whitespace at either end is rejected so a
     * value cannot accidentally become a different option. Brackets remain
     * legal inside a value so IPv6 literals can be written.</p>
     */
    public static boolean isSafeValue(String value) {
        if (value == null || value.isEmpty() || value.length() > MAX_VALUE_LENGTH) {
            return false;
        }
        char first = value.charAt(0);
        char last = value.charAt(value.length() - 1);
        if (Character.isWhitespace(first) || Character.isWhitespace(last)) {
            return false;
        }
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            // '[' and ']' are allowed inside a value (IPv6 literals); a
            // section header would have to start the line, which the writer
            // never produces for a value.
            if (c == '\r' || c == '\n' || c == '=' || c == '#' || c == ';'
                    || Character.isISOControl(c)) {
                return false;
            }
        }
        return true;
    }

    /**
     * Legacy settings used {@code log} for the router's {@code loglevel}
     * option. Keep that alias so an existing PreferenceFragment key continues
     * to edit the real INI key instead of adding an ignored {@code log} line.
     */
    private static String canonicalKey(String key) {
        return "log".equals(key) ? "loglevel" : key;
    }

    private static boolean isSafeKey(String key) {
        if (key == null || key.isEmpty() || key.length() > MAX_KEY_LENGTH) {
            return false;
        }
        if (key.charAt(0) == '.' || key.charAt(key.length() - 1) == '.') {
            return false;
        }
        if (key.contains("..")) {
            return false;
        }
        for (int i = 0; i < key.length(); i++) {
            char c = key.charAt(i);
            boolean allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            if (!allowed) {
                return false;
            }
        }
        return true;
    }

    /** Refuse to read or write key material/passwords through this store. */
    private static boolean isSecretKey(String key) {
        if (key == null) {
            return true;
        }
        String lower = key.toLowerCase(Locale.ROOT);
        return lower.contains("secret") || lower.contains("password")
                || lower.contains("passphrase") || lower.contains("privatekey")
                || lower.contains("private_key") || lower.contains("keystore")
                || lower.endsWith(".keys") || "keys".equals(lower) || lower.contains("auth");
    }

    private String readConfigText() {
        File file = configFile;
        if (file == null) {
            return null;
        }
        if (!file.isFile()) {
            cachedText = "";
            cachedModified = Long.MIN_VALUE;
            cachedLength = -1L;
            return "";
        }
        long length = file.length();
        if (length > MAX_CONFIG_BYTES) {
            Log.e(TAG, "configuration file is too large");
            return null;
        }
        long modified = file.lastModified();
        if (cachedText != null && modified == cachedModified && length == cachedLength) {
            return cachedText;
        }
        try (FileInputStream in = new FileInputStream(file)) {
            String text = readBounded(in);
            cachedText = text;
            cachedModified = modified;
            cachedLength = length;
            return text;
        } catch (IOException e) {
            Log.e(TAG, "unable to read configuration", e);
            return null;
        }
    }

    private String readBundledConfig() {
        try (InputStream in = appContext.getAssets().open(CONFIG_FILE_NAME)) {
            return readBounded(in);
        } catch (IOException e) {
            Log.w(TAG, "bundled configuration unavailable", e);
            return null;
        }
    }

    private static String readBounded(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream(4096);
        byte[] buffer = new byte[4096];
        int read;
        long total = 0L;
        while ((read = in.read(buffer)) != -1) {
            total += read;
            if (total > MAX_CONFIG_BYTES) {
                throw new IOException("configuration file is too large");
            }
            out.write(buffer, 0, read);
        }
        return out.toString(StandardCharsets.UTF_8.name());
    }

    /**
     * Return the last assignment for {@code flatKey}, matching both
     * {@code key = value} at top level and {@code [section] key = value}.
     * Boost.Program_options uses the last duplicate, so the same rule is used
     * here.
     */
    private static String findLastValue(String text, String flatKey) {
        String[] lines = text.split("\n", -1);
        String section = "";
        String value = null;
        for (String line : lines) {
            String trimmed = line.trim();
            if (trimmed.isEmpty() || trimmed.startsWith("#") || trimmed.startsWith(";")) {
                continue;
            }
            if (isSectionHeader(trimmed)) {
                section = sectionName(trimmed);
                continue;
            }
            int equals = line.indexOf('=');
            if (equals <= 0) {
                continue;
            }
            String key = line.substring(0, equals).trim();
            String flat = section.isEmpty() ? key : section + "." + key;
            if (flat.equals(flatKey)) {
                value = line.substring(equals + 1).trim();
            }
        }
        return value;
    }

    private static String updateOption(String text, String flatKey, String value) {
        String[] lines = text.split("\n", -1);
        String section = "";
        int match = -1;
        String matchKey = null;
        String matchIndent = "";

        for (int i = 0; i < lines.length; i++) {
            String line = lines[i];
            String trimmed = line.trim();
            if (trimmed.isEmpty() || trimmed.startsWith("#") || trimmed.startsWith(";")) {
                continue;
            }
            if (isSectionHeader(trimmed)) {
                section = sectionName(trimmed);
                continue;
            }
            int equals = line.indexOf('=');
            if (equals <= 0) {
                continue;
            }
            String key = line.substring(0, equals).trim();
            String flat = section.isEmpty() ? key : section + "." + key;
            if (flat.equals(flatKey)) {
                match = i;
                matchKey = key;
                matchIndent = leadingWhitespace(line);
            }
        }

        StringBuilder result =
                new StringBuilder(text.length() + flatKey.length() + value.length() + 8);
        if (match >= 0) {
            for (int i = 0; i < lines.length; i++) {
                if (i > 0) {
                    result.append('\n');
                }
                if (i == match) {
                    result.append(matchIndent).append(matchKey).append(" = ").append(value);
                } else {
                    result.append(lines[i]);
                }
            }
            return result.toString();
        }

        // New options must not be appended blindly at EOF: if the file ends
        // inside [section], the line would silently become section.key. Insert
        // before the first section (dotted keys are valid at top level), or at
        // EOF only when the file has no section.
        int insertAt = lines.length;
        for (int i = 0; i < lines.length; i++) {
            if (isSectionHeader(lines[i].trim())) {
                insertAt = i;
                break;
            }
        }
        boolean first = true;
        for (int i = 0; i <= lines.length; i++) {
            if (i == insertAt) {
                if (!first) {
                    result.append('\n');
                }
                result.append(flatKey).append(" = ").append(value);
                first = false;
            }
            if (i < lines.length) {
                if (!(i == lines.length - 1 && lines[i].isEmpty())) {
                    if (!first) {
                        result.append('\n');
                    }
                    result.append(lines[i]);
                    first = false;
                }
            }
        }
        if (result.length() > 0) {
            result.append('\n');
        }
        return result.toString();
    }

    private boolean writeAtomically(byte[] bytes) {
        File file = configFile;
        if (file == null) {
            return false;
        }
        File directory = file.getParentFile();
        if (directory == null || (!directory.isDirectory() && !directory.mkdirs())) {
            Log.e(TAG, "configuration directory is not available");
            return false;
        }
        File temporary = new File(directory, file.getName() + TEMP_SUFFIX);
        try {
            try (FileOutputStream out = new FileOutputStream(temporary)) {
                out.write(bytes);
                out.flush();
                out.getFD().sync();
            }
            // rename(2) is atomic and replaces an existing target on Android
            // app-private filesystems. Do not fall back to a non-atomic copy.
            if (!temporary.renameTo(file)) {
                Log.e(TAG, "atomic configuration replace failed");
                return false;
            }
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

    private static File resolveConfigFile(Context appContext) {
        File file = new File(DaemonWrapper.defaultDataDir(appContext), CONFIG_FILE_NAME);
        try {
            file = file.getCanonicalFile();
        } catch (IOException e) {
            Log.e(TAG, "unable to resolve configuration path", e);
            return null;
        }
        if (!isInsideAppStorage(appContext, file.getParentFile())) {
            Log.e(TAG, "configuration path escaped app storage");
            return null;
        }
        return file;
    }

    private static boolean isInsideAppStorage(Context context, File candidate) {
        if (candidate == null) {
            return false;
        }
        try {
            String path = candidate.getCanonicalPath();
            File internal = context.getFilesDir();
            if (internal != null) {
                String root = internal.getCanonicalPath();
                if (path.equals(root) || path.startsWith(root + File.separator)) {
                    return true;
                }
            }
            File external = context.getExternalFilesDir(null);
            if (external != null) {
                String root = external.getCanonicalPath();
                if (path.equals(root) || path.startsWith(root + File.separator)) {
                    return true;
                }
            }
        } catch (IOException e) {
            Log.e(TAG, "unable to validate storage path", e);
        }
        return false;
    }

    private static boolean isSectionHeader(String trimmed) {
        return trimmed.length() > 2 && trimmed.charAt(0) == '[' && trimmed.indexOf(']') > 1;
    }

    private static String sectionName(String trimmed) {
        int close = trimmed.indexOf(']');
        return close > 1 ? trimmed.substring(1, close).trim() : "";
    }

    private static String leadingWhitespace(String line) {
        int end = 0;
        while (end < line.length() && Character.isWhitespace(line.charAt(end))) {
            end++;
        }
        return line.substring(0, end);
    }
}
