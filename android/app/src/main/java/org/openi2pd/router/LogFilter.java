/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LogFilter.
 *
 * Pure minimum-severity and query matching for the native log list.
 */

package org.openi2pd.router;

import java.util.Locale;

/**
 * Pure predicate helpers applied to the retained native log list.
 *
 * <p>A chip is a <em>minimum</em> severity, so {@code Warn} also shows errors;
 * only the explicit {@code All}/{@code Debug} views are unfiltered and skip
 * classification. Query matching lowercases with {@link Locale#ROOT} and uses
 * plain substring search, so no regular expression is compiled per line and a
 * hostile log line cannot turn the filter into a backtracking cost. Both
 * methods are side-effect free and safe on any thread; the caller owns the
 * list and its synchronization.</p>
 */
final class LogFilter {
    private LogFilter() {
    }

    static boolean passes(LogLevel minimum, String level) {
        // All/Debug are the unfiltered views; the other chips are minimum
        // severity thresholds, so Warn also shows errors. Unfiltered views
        // skip classification so a chip change does no per-line work.
        if (minimum == null || minimum.unfiltered()) {
            return true;
        }
        return minimum.accepts(LogLevel.classify(level));
    }

    static boolean matchesQuery(LogEntry entry, String normalizedQuery) {
        if (normalizedQuery == null || normalizedQuery.isEmpty()) {
            return true;
        }
        String needle = normalizedQuery.toLowerCase(Locale.ROOT);
        return containsIgnoringCase(entry.message(), needle)
                || containsIgnoringCase(entry.level(), needle);
    }

    // The caller already lowercased the needle, so only the candidate field is
    // converted and no regular expression is compiled per retained line.
    private static boolean containsIgnoringCase(String candidate, String lowercaseNeedle) {
        return candidate != null && candidate.toLowerCase(Locale.ROOT).contains(lowercaseNeedle);
    }
}
