/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentControlTest.
 *
 * Pure-JVM tests for the torrent control protocol decoder and magnet builder,
 * covering UTF-8 escapes, declared-length mismatches, duplicate fields and
 * rejected input. No native library is loaded.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import java.nio.charset.StandardCharsets;
import org.junit.Test;

public class TorrentControlTest {
    private String frame(String payload) {
        return "I2PDTUI/1 OK " + payload.getBytes(StandardCharsets.UTF_8).length + "\n" + payload;
    }

    @Test
    public void decodesBoundedUtf8AndEscapes() {
        assertEquals(
                "pièce\nfile", TorrentControl.fields(frame("name=pièce\\nfile\n")).get("name"));
        assertEquals("\\x", TorrentControl.unescape("\\x"));
        assertEquals(0, TorrentControl.fields(frame("")).size());
    }

    @Test
    public void rejectsMalformedLengthsAndDuplicateKeys() {
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.fields("I2PDTUI/1 OK 999999\nx=y\n"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.fields("I2PDTUI/1 OK 4\nx=y\nextra"));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.fields(frame("x=y\nx=z\n")));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.fields("I2PDTUI/1 OK -1\n"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.fields("I2PDTUI/1 ERR 4 4\nnope"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.fields(frame("result=error\nmessage=Invalid magnet\n")));
    }

    @Test
    public void magnetPreventsControlInjectionAndBoundsArguments() {
        String magnet = "magnet:?xt=urn:btih:"
                + "a".repeat(40);
        assertEquals("TORRENT-ADD-MAGNET 0 " + magnet, TorrentControl.magnet(0, magnet));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.magnet(0, magnet + "\nQUIT"));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.magnet(0, magnet + "\u0000"));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.magnet(0, magnet + "é"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.magnet(0, magnet + "a".repeat(2048)));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.magnet(-1, magnet));
    }

    @Test
    public void trackerCommandsRemainI2pOnlyAndRejectControlInjection() {
        String tracker = "http://tracker.example.i2p/announce";
        assertEquals("TORRENT-TRACKER-ADD 0 7 " + tracker,
                TorrentControl.tracker(true, 0, "7", tracker));
        assertEquals("TORRENT-TRACKER-REMOVE 0 7 " + tracker,
                TorrentControl.tracker(false, 0, "7", tracker));
        assertEquals("TORRENT-TRACKER-ADD 0 7 udp://tracker.i2p:6969/announce",
                TorrentControl.tracker(true, 0, "7", "udp://tracker.i2p:6969/announce"));
        for (String invalid :
                new String[] {"http://example.com/announce", "http://127.0.0.1/announce",
                        "http://tracker.i2p:0/announce", "http://user:secret@tracker.i2p/announce",
                        "http://tracker.i2p/announce\nQUIT", "http://tracker.i2p./announce",
                        "https://tracker.i2p/announce", "http://tracker.i2p/announce#secret"}) {
            assertThrows(IllegalArgumentException.class,
                    () -> TorrentControl.tracker(true, 0, "7", invalid));
        }
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.tracker(true, 0, "2147483648", tracker));
    }

    @Test
    public void creationAcceptsOnlyImmediateStorageChildren() {
        assertEquals("TORRENT-CREATE 0 My files", TorrentControl.create(0, "My files"));
        for (String invalid : new String[] {"", ".", "..", "../escape", "/absolute", "nested/file",
                     "nested\\file", "name\nQUIT"}) {
            assertThrows(IllegalArgumentException.class, () -> TorrentControl.create(0, invalid));
        }
    }

    @Test
    public void commentsPreserveUnicodeWithoutPublishingAnImplicitIdentity() {
        assertEquals("TORRENT-COMMENT-ADD 0 7 5 |Useful \uD83D\uDE00",
                TorrentControl.comment(0, "7", 5, "", "Useful \uD83D\uDE00"));
        assertEquals("TORRENT-COMMENT-ADD 0 7 0 pseudonym|Text",
                TorrentControl.comment(0, "7", 0, "pseudonym", "Text"));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.comment(0, "7", 0, "", ""));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.comment(0, "7", 6, "", "Text"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.comment(0, "7", 0, "a|b", "Text"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.comment(0, "7", 0, "", "Text\nQUIT"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.comment(0, "7", 0, "", "\uD800"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.comment(0, "7", 0, "x".repeat(33), "Text"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.comment(0, "7", 0, "", "x".repeat(513)));
    }
}
