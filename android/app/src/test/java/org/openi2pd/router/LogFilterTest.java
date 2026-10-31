/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: LogFilterTest.
 *
 * Pure JVM minimum-severity, native-token classification and query
 * filter regressions. No Android class is loaded, so no emulator is
 * required.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Locale;

public class LogFilterTest {
    @Test
    public void minimumSeverityIncludesStrongerLevels() {
        assertTrue(LogFilter.passes(LogLevel.INFO, "warn"));
        assertTrue(LogFilter.passes(LogLevel.INFO, "error"));
        assertFalse(LogFilter.passes(LogLevel.INFO, "debug"));
        assertTrue(LogFilter.passes(LogLevel.WARN, "error"));
        assertFalse(LogFilter.passes(LogLevel.WARN, "info"));
        assertTrue(LogFilter.passes(LogLevel.ALL, "debug"));
        assertTrue(LogFilter.passes(LogLevel.DEBUG, "notice"));
    }

    @Test
    public void nativeTokensClassifyToTheirSeverity() {
        assertSame(LogLevel.INFO, LogLevel.classify(null));
        assertSame(LogLevel.INFO, LogLevel.classify(""));
        assertSame(LogLevel.INFO, LogLevel.classify("none"));
        assertSame(LogLevel.INFO, LogLevel.classify("INFO"));
        assertSame(LogLevel.INFO, LogLevel.classify("info"));
        assertSame(LogLevel.INFO, LogLevel.classify("notice"));
        assertSame(LogLevel.WARN, LogLevel.classify("warn"));
        assertSame(LogLevel.WARN, LogLevel.classify("WARNING"));
        assertSame(LogLevel.WARN, LogLevel.classify("warn: peer timeout"));
        assertSame(LogLevel.ERROR, LogLevel.classify("error"));
        assertSame(LogLevel.ERROR, LogLevel.classify("ERROR"));
        assertSame(LogLevel.ERROR, LogLevel.classify("fatal"));
        assertSame(LogLevel.ERROR, LogLevel.classify("critical"));
        assertSame(LogLevel.ERROR, LogLevel.classify("failed"));
        assertSame(LogLevel.DEBUG, LogLevel.classify("debug"));
        assertSame(LogLevel.DEBUG, LogLevel.classify("DEBUG"));
        assertSame(LogLevel.DEBUG, LogLevel.classify("TRACE"));
    }

    @Test
    public void unfilteredViewsAcceptEveryLevel() {
        assertTrue(LogLevel.ALL.unfiltered());
        assertTrue(LogLevel.DEBUG.unfiltered());
        assertFalse(LogLevel.INFO.unfiltered());
        assertFalse(LogLevel.WARN.unfiltered());
        assertFalse(LogLevel.ERROR.unfiltered());

        assertTrue(LogFilter.passes(LogLevel.ALL, null));
        assertTrue(LogFilter.passes(LogLevel.DEBUG, null));
        assertTrue(LogFilter.passes(LogLevel.DEBUG, "trace"));
        assertTrue(LogFilter.passes(null, "debug"));
        assertTrue(LogLevel.ALL.accepts(null));
        assertTrue(LogLevel.DEBUG.accepts(LogLevel.INFO));
    }

    @Test
    public void minimumRanksAscendWithSeverity() {
        assertTrue(LogLevel.ALL.minimum() < LogLevel.DEBUG.minimum());
        assertTrue(LogLevel.DEBUG.minimum() < LogLevel.INFO.minimum());
        assertTrue(LogLevel.INFO.minimum() < LogLevel.WARN.minimum());
        assertTrue(LogLevel.WARN.minimum() < LogLevel.ERROR.minimum());
        assertEquals(LogLevel.ERROR.minimum(), LogLevel.ERROR.severity());
        assertEquals(0, LogLevel.DEBUG.severity());
        assertEquals(1, LogLevel.INFO.severity());
        assertEquals(2, LogLevel.WARN.severity());
        assertEquals(3, LogLevel.ERROR.severity());
    }

    @Test
    public void infoThresholdHidesDebugLines() {
        assertTrue(LogFilter.passes(LogLevel.INFO, "info"));
        assertTrue(LogFilter.passes(LogLevel.INFO, "none"));
        assertTrue(LogFilter.passes(LogLevel.INFO, null));
        assertTrue(LogFilter.passes(LogLevel.INFO, "warn"));
        assertTrue(LogFilter.passes(LogLevel.INFO, "error"));
        assertFalse(LogFilter.passes(LogLevel.INFO, "debug"));
        assertFalse(LogFilter.passes(LogLevel.INFO, "trace"));
        assertTrue(LogFilter.passes(LogLevel.INFO, ""));
    }

    @Test
    public void warnThresholdShowsWarningsAndErrors() {
        assertTrue(LogFilter.passes(LogLevel.WARN, "warn"));
        assertTrue(LogFilter.passes(LogLevel.WARN, "warning"));
        assertTrue(LogFilter.passes(LogLevel.WARN, "error"));
        assertTrue(LogFilter.passes(LogLevel.WARN, "critical"));
        assertFalse(LogFilter.passes(LogLevel.WARN, "info"));
        assertFalse(LogFilter.passes(LogLevel.WARN, "none"));
        assertFalse(LogFilter.passes(LogLevel.WARN, "debug"));
        assertFalse(LogFilter.passes(LogLevel.WARN, null));
    }

    @Test
    public void errorThresholdShowsFailuresOnly() {
        assertTrue(LogFilter.passes(LogLevel.ERROR, "error"));
        assertTrue(LogFilter.passes(LogLevel.ERROR, "FATAL"));
        assertTrue(LogFilter.passes(LogLevel.ERROR, "critical"));
        assertTrue(LogFilter.passes(LogLevel.ERROR, "failed"));
        assertFalse(LogFilter.passes(LogLevel.ERROR, "warn"));
        assertFalse(LogFilter.passes(LogLevel.ERROR, "info"));
        assertFalse(LogFilter.passes(LogLevel.ERROR, "debug"));
        assertFalse(LogFilter.passes(LogLevel.ERROR, null));
    }

    @Test
    public void queryMatchesMessageOrLevel() {
        LogEntry entry = new LogEntry(1L, 2L, "WARN", "Peer timeout");
        assertTrue(LogFilter.matchesQuery(entry, ""));
        assertTrue(LogFilter.matchesQuery(entry, null));
        assertTrue(LogFilter.matchesQuery(entry, "peer"));
        assertTrue(LogFilter.matchesQuery(entry, "PEER"));
        assertTrue(LogFilter.matchesQuery(entry, "timeout"));
        assertTrue(LogFilter.matchesQuery(entry, "warn"));
        assertFalse(LogFilter.matchesQuery(entry, "error"));

        LogEntry textOnly = new LogEntry(3L, 4L, "", "Tunnel built");
        assertTrue(LogFilter.matchesQuery(textOnly, "tunnel"));
        assertFalse(LogFilter.matchesQuery(textOnly, "warn"));
    }

    @Test
    public void classificationAndQueryIgnoreTheDefaultLocale() {
        Locale previous = Locale.getDefault();
        try {
            // A Turkish default lowercases "I" to a dotless i;
            // Locale.ROOT must still classify and match the native
            // ASCII tokens.
            Locale.setDefault(Locale.forLanguageTag("tr"));
            assertSame(LogLevel.ERROR, LogLevel.classify("CRITICAL"));
            assertSame(LogLevel.INFO, LogLevel.classify("INFO"));
            LogEntry entry = new LogEntry(5L, 6L, "CRITICAL", "Peer test failed");
            assertTrue(LogFilter.matchesQuery(entry, "critical"));
        } finally {
            Locale.setDefault(previous);
        }
    }
}
