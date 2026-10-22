/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentSafGrantsTest.
 *
 * JVM-only reference counting for persisted SAF tree grants.
 */

package org.openi2pd.router;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import java.util.HashMap;
import java.util.Map;
import org.junit.Test;

public class TorrentSafGrantsTest {
    @Test
    public void sharedTreeSurvivesOneDetach() {
        Map<String, String> grants = new HashMap<>();
        grants.put("0:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "content://tree/shared");
        grants.put("0:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "content://tree/shared");
        assertTrue(TorrentSafGrants.referenced(grants, "content://tree/shared", null));
        assertTrue(TorrentSafGrants.referenced(
                grants, "content://tree/shared", "0:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        grants.remove("0:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        assertFalse(TorrentSafGrants.referenced(
                grants, "content://tree/shared", "0:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    }

    @Test
    public void unrelatedTreeIsNeverReleased() {
        Map<String, String> grants = new HashMap<>();
        grants.put("a", "content://tree/a");
        assertFalse(TorrentSafGrants.referenced(grants, "content://tree/b", null));
        assertFalse(TorrentSafGrants.referenced(grants, "content://tree/a", "a"));
        assertTrue(TorrentSafGrants.referenced(grants, "content://tree/a", "b"));
    }
}
