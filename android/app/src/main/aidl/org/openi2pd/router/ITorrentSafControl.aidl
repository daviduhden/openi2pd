// See the LICENSE file at the top of the project tree for copyright
// and license details. openi2pd: broker-bound SAF control capability.

package org.openi2pd.router;

// Acquired from ITorrentSafStorage by a synchronous, PID-aware call.
// Operations still belong to the process captured at acquisition.
oneway interface ITorrentSafControl {
    void cancel(long operation);
    void retire(long generation);
}
