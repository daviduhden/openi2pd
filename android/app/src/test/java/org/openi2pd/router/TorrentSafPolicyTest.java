/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentSafPolicyTest.
 *
 * JVM-only SAF bounds used by the broker; no provider or device is required.
 */

package org.openi2pd.router;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import org.junit.Test;

public class TorrentSafPolicyTest {
    @Test
    public void rangesNeverWrap() {
        assertTrue(TorrentSafPolicy.validRange(16384, 0, 16384));
        assertTrue(TorrentSafPolicy.validRange(16384, 16383, 1));
        assertFalse(TorrentSafPolicy.validRange(16384, 16384, 1));
        assertFalse(TorrentSafPolicy.validRange(16384, Long.MAX_VALUE, 1));
        assertFalse(TorrentSafPolicy.validRange(-1, 0, 0));
        assertFalse(TorrentSafPolicy.validRange(16384, 0, 16385));
    }

    @Test
    public void exportGrowsButCommitsExactly() {
        assertTrue(TorrentSafPolicy.exportSizeAllowed(32768, 0));
        assertTrue(TorrentSafPolicy.exportSizeAllowed(32768, 32768));
        assertFalse(TorrentSafPolicy.exportSizeAllowed(32768, 32769));
        assertFalse(TorrentSafPolicy.exportSizeAllowed(0, 0));
    }

    @Test
    public void staleOrMissingIdentityIsRejected() {
        assertTrue(TorrentSafPolicy.documentIdentityMatches("doc:7", "doc:7"));
        assertFalse(TorrentSafPolicy.documentIdentityMatches("doc:7", "doc:8"));
        assertFalse(TorrentSafPolicy.documentIdentityMatches("", ""));
        assertFalse(TorrentSafPolicy.documentIdentityMatches(null, null));
    }

    @Test
    public void onlyWriteOpenExtendsShortDocuments() {
        assertTrue(TorrentSafPolicy.canExtendExisting(true, 100, 16384));
        assertTrue(TorrentSafPolicy.canExtendExisting(true, 0, 16384));
        assertFalse(TorrentSafPolicy.canExtendExisting(false, 0, 16384));
        assertFalse(TorrentSafPolicy.canExtendExisting(true, 16384, 16384));
        assertFalse(TorrentSafPolicy.canExtendExisting(true, 16385, 16384));
    }
}
