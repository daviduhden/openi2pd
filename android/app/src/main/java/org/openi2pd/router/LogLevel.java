/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LogLevel.
 *
 * Typed native log severity: classifies one native level token and supplies
 * the minimum-severity views used by the log screen.
 */

package org.openi2pd.router;

import java.util.Locale;
import java.util.Optional;

/**
 * Severity of one native router log line and the log screen's filter views.
 *
 * <p>{@link #classify(String)} maps a native level token with the same
 * substrings the daemon log ring emits: {@code error}, {@code fatal},
 * {@code crit} and {@code fail} are errors; {@code warn} is a warning;
 * {@code debug} and {@code trace} are debug; informational and unknown tokens
 * are informational.</p>
 *
 * <p>The constants double as filter views. {@link #ALL} and {@link #DEBUG} are
 * unfiltered, while {@link #INFO}, {@link #WARN} and {@link #ERROR} are minimum
 * thresholds, so the warning view also shows errors. A higher
 * {@link #severity()} is more severe.</p>
 */
public enum LogLevel {
    /** Unfiltered view: every retained line passes. */
    ALL(-1),
    /** Debug and trace lines; also an unfiltered view. */
    DEBUG(0),
    /** Informational lines and unknown native tokens. */
    INFO(1),
    /** Warnings and every more severe line. */
    WARN(2),
    /** Errors, fatal and critical lines. */
    ERROR(3);

    /*
     * Cached matches keep classification allocation-free apart from the
     * lowercase conversion; the retained list is re-filtered on every chip or
     * query change. Enum constants are initialised before these fields.
     */
    private static final Optional<LogLevel> ERROR_MATCH = Optional.of(ERROR);
    private static final Optional<LogLevel> WARN_MATCH = Optional.of(WARN);
    private static final Optional<LogLevel> DEBUG_MATCH = Optional.of(DEBUG);
    private static final Optional<LogLevel> NO_MATCH = Optional.empty();

    private final int severity;

    LogLevel(int severity) {
        this.severity = severity;
    }

    /** Numeric severity rank; a higher value is more severe. */
    public int severity() {
        return severity;
    }

    /**
     * Lowest severity rank this view compares against. Unfiltered views bypass
     * the comparison, and {@link #ALL} keeps a rank below every classified
     * line.
     */
    public int minimum() {
        return severity;
    }

    /**
     * Whether this view ignores severity, as {@link #ALL} and {@link #DEBUG}
     * do.
     */
    public boolean unfiltered() {
        return this == ALL || this == DEBUG;
    }

    /** Whether a line classified as {@code level} passes this view. */
    public boolean accepts(LogLevel level) {
        return switch (this) {
            case ALL, DEBUG -> true;
            case INFO, WARN, ERROR -> level != null && level.severity() >= minimum();
        };
    }

    /**
     * Classifies one native level token. {@code null} and unknown tokens are
     * informational, which is also how the daemon's {@code none} state is
     * presented.
     */
    public static LogLevel classify(String nativeLevel) {
        String value = nativeLevel == null ? "" : nativeLevel.toLowerCase(Locale.ROOT);
        return matched(value).orElse(INFO);
    }

    /**
     * Returns the most severe token family present in an already lowercased
     * value. Failure tokens are checked first because a line may mention a
     * lower severity while reporting a failure.
     */
    private static Optional<LogLevel> matched(String value) {
        if (value.contains("error") || value.contains("fatal")
                || value.contains("crit") || value.contains("fail")) {
            return ERROR_MATCH;
        }
        if (value.contains("warn")) {
            return WARN_MATCH;
        }
        if (value.contains("debug") || value.contains("trace")) {
            return DEBUG_MATCH;
        }
        return NO_MATCH;
    }
}
