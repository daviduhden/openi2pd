/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentRowTest.
 *
 * Pure-JVM tests for the bounded TORRENTS snapshot decoder and its
 * accessibility labels. No Android API, emulator or native library is
 * involved.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.LinkedHashMap;
import java.util.Map;

public final class TorrentRowTest {
    private static Map<String, String> row() {
        Map<String, String> fields = new LinkedHashMap<>();
        fields.put("torrent.0.id", "7");
        fields.put("torrent.0.name", "example");
        fields.put("torrent.0.status", "Seeding");
        fields.put("torrent.0.error", "");
        fields.put("torrent.0.hash", "0123456789abcdef0123456789abcdef01234567");
        fields.put("torrent.0.magnet", "magnet:?xt=urn:btih:0123&dn=example");
        fields.put("torrent.0.total", "1024");
        fields.put("torrent.0.wanted", "1024");
        fields.put("torrent.0.have", "1024");
        fields.put("torrent.0.private", "1");
        fields.put("torrent.0.downloaded", "1024");
        fields.put("torrent.0.uploaded", "2048");
        fields.put("torrent.0.downRate", "0");
        fields.put("torrent.0.upRate", "12");
        fields.put("torrent.0.peers", "3");
        fields.put("torrent.0.seeders", "1");
        fields.put("torrent.0.leechers", "2");
        fields.put("torrent.0.percent", "100.000000");
        fields.put("torrent.0.eta", "-1");
        return fields;
    }

    @Test
    public void decodesBoundedSnapshot() {
        TorrentRow row = TorrentRow.from(row(), "torrent.0.");
        assertEquals("7", row.id());
        assertEquals("example", row.name());
        assertEquals("Seeding", row.status());
        assertTrue(row.privateTorrent());
        assertTrue(row.isComplete());
        assertFalse(row.hasError());
        assertEquals(1024L, row.have());
        assertEquals(2048L, row.uploaded());
        assertEquals(3, row.peers());
        assertEquals(100, row.percent());
        assertEquals(-1, row.etaSeconds());
    }

    @Test
    public void rejectsMissingOrNonNumericIdentifier() {
        Map<String, String> fields = row();
        fields.put("torrent.0.id", "");
        assertThrows(IllegalArgumentException.class, () -> TorrentRow.from(fields, "torrent.0."));
        fields.put("torrent.0.id", "12x");
        assertThrows(IllegalArgumentException.class, () -> TorrentRow.from(fields, "torrent.0."));
        fields.put("torrent.0.id", "12345678901");
        assertThrows(IllegalArgumentException.class, () -> TorrentRow.from(fields, "torrent.0."));
    }

    @Test
    public void sanitizesHashMagnetAndCounters() {
        Map<String, String> fields = row();
        fields.put("torrent.0.hash", "not-a-hash");
        fields.put("torrent.0.magnet", "https://example.invalid");
        fields.put("torrent.0.have", "-5");
        fields.put("torrent.0.percent", "250.000000");
        fields.put("torrent.0.eta", "soon");
        fields.put("torrent.0.peers", "9999999999");
        TorrentRow row = TorrentRow.from(fields, "torrent.0.");
        assertEquals("", row.hash());
        assertEquals("", row.magnet());
        assertEquals(0L, row.have());
        assertEquals(100, row.percent());
        assertEquals(-1, row.etaSeconds());
        assertEquals(0, row.peers());
    }

    @Test
    public void roundsFractionalPercent() {
        Map<String, String> fields = row();
        fields.put("torrent.0.percent", "42.500000");
        assertEquals(43, TorrentRow.from(fields, "torrent.0.").percent());
        fields.put("torrent.0.percent", "0.400000");
        assertEquals(0, TorrentRow.from(fields, "torrent.0.").percent());
        fields.put("torrent.0.percent", "NaN");
        assertEquals(0, TorrentRow.from(fields, "torrent.0.").percent());
        fields.put("torrent.0.percent", "1e999");
        assertEquals(100, TorrentRow.from(fields, "torrent.0.").percent());
    }

    @Test
    public void truncatesOversizedText() {
        Map<String, String> fields = row();
        fields.put("torrent.0.name", repeat('n', 600));
        fields.put("torrent.0.status", repeat('s', 100));
        fields.put("torrent.0.error", repeat('e', 400));
        TorrentRow row = TorrentRow.from(fields, "torrent.0.");
        assertEquals(512, row.name().length());
        assertEquals(64, row.status().length());
        assertEquals(256, row.error().length());
        assertTrue(row.hasError());
    }

    @Test
    public void labelsCarryStateAndPrivateBadge() {
        TorrentRow row = TorrentRow.from(row(), "torrent.0.");
        assertEquals("example\nSeeding · 100% · private", row.displayLabel("private"));
        assertEquals("example, Seeding, 100%, private", row.accessibilityLabel("private"));
        assertEquals("example, Seeding, 100%", row.accessibilityLabel(""));
    }

    private static String repeat(char value, int count) {
        StringBuilder text = new StringBuilder(count);
        for (int i = 0; i < count; ++i) {
            text.append(value);
        }
        return text.toString();
    }
}
