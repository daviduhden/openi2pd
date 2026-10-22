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

final class TorrentSafGrants {
    private TorrentSafGrants() {}

    static boolean referenced(Map<String, ?> grants, String uri, String exceptKey) {
        if (uri == null || grants == null) return false;
        for (Map.Entry<String, ?> entry : grants.entrySet()) {
            if (exceptKey != null && exceptKey.equals(entry.getKey())) continue;
            if (uri.equals(entry.getValue())) return true;
        }
        return false;
    }
}
