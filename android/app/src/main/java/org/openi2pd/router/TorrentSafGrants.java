/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentSafGrants.
 *
 * Reference counting for persisted SAF tree grants. A tree may back several
 * torrents; only the last reference may release the Android permission.
 */

package org.openi2pd.router;

import java.util.Map;

/**
 * Checks whether a persisted tree URI remains referenced by another torrent.
 * Grant release is performed by the caller only after the mapping update is
 * committed, so a failed update cannot revoke a still-referenced tree.
 */
final class TorrentSafGrants {
    private TorrentSafGrants() {
    }

    /**
     * @param grants persisted torrent-key to tree-URI mappings
     * @param uri URI string whose remaining references are being checked
     * @param exceptKey mapping being replaced or removed, or {@code null}
     * @return whether another mapping contains the exact URI string
     */
    static boolean referenced(Map<String, ?> grants, String uri, String exceptKey) {
        if (uri == null || grants == null)
            return false;
        for (Map.Entry<String, ?> entry : grants.entrySet()) {
            if (exceptKey != null && exceptKey.equals(entry.getKey()))
                continue;
            if (uri.equals(entry.getValue()))
                return true;
        }
        return false;
    }
}
