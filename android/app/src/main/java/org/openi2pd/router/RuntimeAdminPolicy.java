/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RuntimeAdminPolicy.
 *
 * Pure-JVM argument bounds, bounded tunnel-service row decoding and typed
 * outcome sets for the runtime administration bridge. Deliberately free of
 * Android imports so a plain JUnit test can pin every limit and spelling
 * without a device, an emulator or the native library.
 *
 */

package org.openi2pd.router;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Closed bounds and decoders shared by the runtime-administration bridge.
 *
 * <p>These values are the Java side of the JNI contract: native code applies
 * the same limits before converting a string to UTF-8, and the daemon applies
 * them again when it parses the typed protocol verb. Keeping one source avoids
 * a UI field that accepts more than the boundary will forward.</p>
 */
final class RuntimeAdminPolicy {
    /** Unique configured tunnel service name. */
    static final int MAX_TUNNEL_NAME = 64;
    /** Address-book name (stored names and the add/replace host label). */
    static final int MAX_ADDRESS_NAME = 255;
    /** Full base64 destination. */
    static final int MAX_DESTINATION = 1024;
    /** Address-book subscription URL. */
    static final int MAX_SUBSCRIPTION_URL = 512;
    /** Base64 of a 32-byte router identity hash. */
    static final int MAX_ROUTER_HASH = 44;
    /** Rows kept from the bounded tunnel snapshot. */
    static final int MAX_TUNNEL_ROWS = 200;
    /** Rows kept from the bounded subscription snapshot. */
    static final int MAX_SUBSCRIPTION_ROWS = 32;

    /** One configured client or server tunnel; public labels only. */
    record TunnelService(
            String name, String kind, boolean running, String endpoint, String destination) {}

    private RuntimeAdminPolicy() {
        // Static helpers.
    }

    /**
     * A tunnel service name is one printable, whitespace-free token. The native
     * parser rejects a space outright, so the UI must not offer one either.
     */
    static boolean isValidTunnelName(String name) {
        if (name == null || name.isEmpty() || name.length() > MAX_TUNNEL_NAME) {
            return false;
        }
        for (int i = 0; i < name.length(); ++i) {
            char ch = name.charAt(i);
            if (ch <= 32 || ch == 127) {
                return false;
            }
        }
        return true;
    }

    /**
     * A host label of at most {@link #MAX_ADDRESS_NAME} characters with no
     * space. The native boundary re-applies the same limit, and the address
     * book applies the full host-name and destination rules.
     */
    static boolean isValidAddressName(String name) {
        return name != null && !name.isEmpty() && name.length() <= MAX_ADDRESS_NAME
                && name.indexOf(' ') < 0;
    }

    /**
     * A full base64 destination of at most {@link #MAX_DESTINATION} characters.
     * Only the size and the absent space are checked here; the daemon verifies
     * the base64 identity itself.
     */
    static boolean isValidDestination(String destination) {
        return destination != null && !destination.isEmpty()
                && destination.length() <= MAX_DESTINATION && destination.indexOf(' ') < 0;
    }

    /** The address book performs the scheme/host validation; bound the size here. */
    static boolean isValidSubscriptionUrl(String url) {
        return url != null && !url.isEmpty() && url.length() <= MAX_SUBSCRIPTION_URL
                && url.indexOf(' ') < 0;
    }

    /** A router hash is exactly one base64 identity hash; the daemon re-verifies it. */
    static boolean isValidRouterHash(String hash) {
        if (hash == null || hash.isEmpty() || hash.length() > MAX_ROUTER_HASH) {
            return false;
        }
        for (int i = 0; i < hash.length(); ++i) {
            char ch = hash.charAt(i);
            boolean allowed = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
                    || (ch >= '0' && ch <= '9') || ch == '+' || ch == '/' || ch == '=' || ch == '-'
                    || ch == '~';
            if (!allowed) {
                return false;
            }
        }
        return true;
    }

    /** True only for the exact success token of an administrative verb. */
    static boolean isOk(String outcome) {
        return "ok".equals(outcome);
    }

    /** True for the typed "the name already exists" add result. */
    static boolean isExists(String outcome) {
        return "exists".equals(outcome);
    }

    /** True only for the two tokens that mean a reseed job is now running. */
    static boolean isReseedActive(String outcome) {
        return "started".equals(outcome) || "already-running".equals(outcome);
    }

    /** True only for the tokens that report a real tunnel state change. */
    static boolean isTunnelStateChange(String outcome) {
        return "started".equals(outcome) || "stopped".equals(outcome)
                || "already-running".equals(outcome) || "already-stopped".equals(outcome);
    }

    /**
     * Decode {@code name|kind|running|endpoint|destination} rows.
     *
     * <p>Malformed, over-long and unnamed rows are skipped rather than shown
     * as partial state; the caller never sees more than {@link #MAX_TUNNEL_ROWS}
     * entries.</p>
     */
    static List<TunnelService> parseTunnelServices(String raw) {
        if (raw == null || raw.isEmpty()) {
            return Collections.emptyList();
        }
        List<TunnelService> services = new ArrayList<>();
        for (String line : raw.split("\n")) {
            if (line.isEmpty()) {
                continue;
            }
            String[] fields = line.split("\\|", -1);
            if (fields.length != 5 || !isValidTunnelName(fields[0])) {
                continue;
            }
            services.add(new TunnelService(fields[0], bound(fields[1], 16), "1".equals(fields[2]),
                    bound(fields[3], 128), bound(fields[4], 256)));
            if (services.size() >= MAX_TUNNEL_ROWS) {
                break;
            }
        }
        return services;
    }

    /**
     * Typed reseed snapshot. The fields stay strings because the daemon
     * protocol is textual and deliberately bounded; the UI converts the two
     * counters itself. {@link #UNKNOWN} is the explicit absent value instead
     * of a seven-element list whose size the caller must remember to check.
     */
    record ReseedStatus(String phase, String enabled, String attempts, String succeeded,
            String lastAttemptMs, String lastSuccessMs, String lastError) {
        static final ReseedStatus UNKNOWN = new ReseedStatus("", "", "", "", "", "", "");

        boolean isKnown() {
            return !phase.isEmpty();
        }
    }

    /**
     * Decode {@code phase|enabled|attempts|succeeded|lastAttemptMs|lastSuccessMs|lastError}.
     * An empty or malformed record yields {@link ReseedStatus#UNKNOWN} so the
     * UI shows the unknown state instead of invented counters.
     */
    static ReseedStatus parseReseedStatus(String raw) {
        if (raw == null || raw.isEmpty()) {
            return ReseedStatus.UNKNOWN;
        }
        String[] fields = raw.split("\\|", -1);
        if (fields.length != 7) {
            return ReseedStatus.UNKNOWN;
        }
        return new ReseedStatus(bound(fields[0], 16), bound(fields[1], 1), bound(fields[2], 9),
                bound(fields[3], 9), bound(fields[4], 16), bound(fields[5], 16),
                bound(fields[6], 128));
    }

    /** Decode bounded subscription rows, skipping empty or over-long values. */
    static List<String> parseSubscriptions(String raw) {
        if (raw == null || raw.isEmpty()) {
            return Collections.emptyList();
        }
        List<String> urls = new ArrayList<>();
        for (String line : raw.split("\n")) {
            String url = line.trim();
            if (!isValidSubscriptionUrl(url)) {
                continue;
            }
            urls.add(url);
            if (urls.size() >= MAX_SUBSCRIPTION_ROWS) {
                break;
            }
        }
        return urls;
    }

    private static String bound(String value, int maximum) {
        if (value == null) {
            return "";
        }
        return value.length() <= maximum ? value : value.substring(0, maximum);
    }
}
