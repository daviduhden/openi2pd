// See the LICENSE file at the top of the project tree for copyright
// and license details.
// openi2pd: bounded, app-private SAF payload capabilities.
package org.openi2pd.router;
interface ITorrentSafStorage {
    long openExport(String uri, long length, long operation);
    // Commits a successful export or truncates a failed one to zero bytes.
    boolean finishExport(long handle, boolean success, long operation);
    long authorizeTree(String uri, long operation);
    long openFile(long root, String relative, long length, boolean create, long operation);
    byte[] read(long handle, long offset, int count, long operation);
    boolean write(long handle, long offset, in byte[] bytes, long operation);
    // Deletes only the exact regular document represented by an open capability.
    boolean deleteFile(long handle, long operation);
    void closeFile(long handle);
    void revokeTree(long root);
    // Out-of-band: never waits for a provider/data operation lock.
    oneway void cancel(long operation);
    oneway void retire();
}
