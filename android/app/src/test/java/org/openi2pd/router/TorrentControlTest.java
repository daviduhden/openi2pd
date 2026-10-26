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
    public void recheckBuildsBoundedCommand() {
        assertEquals("TORRENT-RECHECK 0 7", TorrentControl.recheck(0, "7"));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.recheck(-1, "7"));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.recheck(0, "0"));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.recheck(0, "abc"));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.recheck(0, "2147483648"));
    }

    @Test
    public void removeAndMetainfoCommandsRejectInjection() {
        assertEquals("TORRENT-REMOVE 0 7", TorrentControl.remove(0, "7", false));
        assertEquals("TORRENT-REMOVE 0 7 files", TorrentControl.remove(0, "7", true));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.remove(0, "0", true));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.remove(-1, "7", true));
        assertEquals(
                "TORRENT-META-COMMENT 0 7 Useful", TorrentControl.metaComment(0, "7", "Useful"));
        assertEquals("TORRENT-META-CREATED-BY 0 7 openi2pd",
                TorrentControl.metaCreatedBy(0, "7", "openi2pd"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.metaComment(0, "7", "bad\ncontrol"));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.metaCreatedBy(0, "7", "x".repeat(256)));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.metaComment(0, "0", "text"));
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
                () -> TorrentControl.comment(0, "0", 5, "", "Text"));
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

    @Test
    public void commentIdCommands() {
        org.junit.Assert.assertEquals(
                "TORRENT-COMMENT-HIDE 2 7 42", TorrentControl.commentId(2, "7", "42", true));
        org.junit.Assert.assertEquals(
                "TORRENT-COMMENT-UNHIDE 2 7 42", TorrentControl.commentId(2, "7", "42", false));
        try {
            TorrentControl.commentId(2, "0", "42", true);
            throw new AssertionError("accepted torrent id 0");
        } catch (IllegalArgumentException expected) {
        }
        try {
            TorrentControl.commentId(2, "7", "0", false);
            throw new AssertionError("accepted comment id 0");
        } catch (IllegalArgumentException expected) {
        }
    }

    @Test
    public void commentPagingIsBoundedAndKeepsTheHistoricDetailLine() {
        // The daemon emits at most 16 comment records per TORRENT-DETAIL
        // response; offset 0 with the default page reproduces the historic
        // request line so older daemons keep working.
        assertEquals(16, TorrentControl.COMMENT_PAGE);
        assertEquals("TORRENT-DETAIL 2 7", TorrentControl.commentPage(2, "7", 0, 16));
        assertEquals("TORRENT-DETAIL 2 7 16 16", TorrentControl.commentPage(2, "7", 16, 16));
        assertEquals("TORRENT-DETAIL 2 7 240 1", TorrentControl.commentPage(2, "7", 240, 1));
        assertEquals("TORRENT-DETAIL 0 1 255 16", TorrentControl.commentPage(0, "1", 255, 16));
        // Both bounds are strict: the engine retains 256 records and the wire
        // page is 1..16, so nothing else is expressible.
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.commentPage(2, "7", -1, 16));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.commentPage(2, "7", 256, 16));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.commentPage(2, "7", 0, 0));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.commentPage(2, "7", 0, 17));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.commentPage(2, "7", 0, -1));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.commentPage(-1, "7", 0, 16));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.commentPage(2, "0", 0, 16));
        assertThrows(
                IllegalArgumentException.class, () -> TorrentControl.commentPage(2, "7\n", 0, 16));
    }

    @Test
    public void bulkCommandsAreFixedAndRejectNegativeSessions() {
        org.junit.Assert.assertEquals(
                "TORRENT-START-ALL 0", TorrentControl.bulk(0, TorrentControl.Bulk.START_ALL));
        org.junit.Assert.assertEquals(
                "TORRENT-STOP-ALL 2", TorrentControl.bulk(2, TorrentControl.Bulk.STOP_ALL));
        org.junit.Assert.assertEquals("TORRENT-REANNOUNCE-ALL 3",
                TorrentControl.bulk(3, TorrentControl.Bulk.REANNOUNCE_ALL));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.bulk(-1, TorrentControl.Bulk.START_ALL));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.bulk(0, null));
    }

    @Test
    public void singleLifecycleCommandsUseBoundedIds() {
        assertEquals(
                "TORRENT-START 0 7", TorrentControl.single(0, "7", TorrentControl.Action.START));
        assertEquals("TORRENT-STOP 2 9", TorrentControl.single(2, "9", TorrentControl.Action.STOP));
        assertEquals("TORRENT-REANNOUNCE 1 3",
                TorrentControl.single(1, "3", TorrentControl.Action.REANNOUNCE));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.single(-1, "7", TorrentControl.Action.START));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.single(0, "0", TorrentControl.Action.START));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.single(0, "abc", TorrentControl.Action.START));
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.single(0, "2147483648", TorrentControl.Action.START));
        assertThrows(IllegalArgumentException.class, () -> TorrentControl.single(0, "7", null));
    }

    @Test
    public void creationTrackerValidatesWithoutAnEngineId() {
        TorrentControl.creationTracker("http://tracker.example.i2p/announce");
        TorrentControl.creationTracker("udp://tracker.i2p:6969/announce");
        for (String invalid : new String[] {null, "", "http://clearnet.example.com/announce",
                     "https://tracker.i2p/announce", "http://user:secret@tracker.i2p/announce",
                     "http://tracker.i2p/announce\nQUIT", "http://tracker.i2p:0/announce",
                     "http://tracker.i2p/announce#fragment"}) {
            assertThrows(
                    IllegalArgumentException.class, () -> TorrentControl.creationTracker(invalid));
        }
        // A creation hint never becomes an admin tracker command for id 0.
        assertThrows(IllegalArgumentException.class,
                () -> TorrentControl.tracker(true, 0, "0", "http://tracker.i2p/announce"));
    }

    @Test
    public void removeAllBulkVariantsAreExact() {
        assertEquals(
                "TORRENT-REMOVE-ALL 4", TorrentControl.bulk(4, TorrentControl.Bulk.REMOVE_ALL));
        assertEquals("TORRENT-REMOVE-ALL 4 files",
                TorrentControl.bulk(4, TorrentControl.Bulk.REMOVE_ALL_FILES));
    }
}
