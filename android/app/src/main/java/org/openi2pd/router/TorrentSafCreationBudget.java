/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: bounded SAF creation input accounting.
 */
package org.openi2pd.router;

import java.io.IOException;

/*
 * Per-request retained UTF-8 budgets for provider-controlled creation
 * inputs. Paths have an 8 MiB aggregate ceiling; document IDs and
 * retained URI strings share a separate 8 MiB ceiling.
 * Counting directories also bounds sibling-name sets and recursion
 * prefixes. These are conservative in-memory limits, not a promise
 * that the resulting bencoded metadata fits the separate 8 MiB limit.
 * Provider cursors may allocate their own data before returning a row;
 * this class bounds only text retained by the application afterwards.
 * Instances belong to the single creation worker.
 */
final class TorrentSafCreationBudget {
    static final int MAX_TEXT_BYTES = 8 * 1024 * 1024;
    static final int MAX_ID_BYTES = 8192;
    static final int MAX_PATH_BYTES = 8192;
    static final int MAX_URI_LENGTH = 8192;
    static final int MAX_HASH_BYTES = 5 * 1024 * 1024;
    static final int HASH_BYTES = 20;
    private int pathBytes;
    private int documentIdBytes;

    /* Validate a component against the native UTF-8 byte limit. */
    static String component(String name) throws IOException {
        if (name == null || name.isEmpty() || name.length() > 255 || name.equals(".")
                || name.equals("..")
                || name.chars().anyMatch(value -> "/\\:<>\"|?*".indexOf(value) >= 0))
            throw new IOException("Invalid source name");
        for (int i = 0; i < name.length(); ++i)
            if (Character.isISOControl(name.charAt(i)))
                throw new IOException("Invalid source name");
        if (utf8Length(name) > 255)
            throw new IOException("Source name exceeds UTF-8 limit");
        return name;
    }

    /*
     * Charge root or child text before building a URI or retaining it
     * in a source list, visited set or sibling-name set. Failed charges
     * leave both counters unchanged. IDs are opaque provider operands;
     * rejection messages never include their contents.
     */
    void document(String id, String relativePath) throws IOException {
        if (id == null || id.isEmpty() || id.length() > MAX_ID_BYTES || relativePath == null
                || relativePath.length() > MAX_PATH_BYTES)
            throw new IOException("Source text limit exceeded");
        int ids = utf8Length(id);
        int paths = utf8Length(relativePath);
        if (ids > MAX_ID_BYTES || paths > MAX_PATH_BYTES || ids > MAX_TEXT_BYTES - documentIdBytes
                || paths > MAX_TEXT_BYTES - pathBytes)
            throw new IOException("Source text limit exceeded");
        documentIdBytes += ids;
        pathBytes += paths;
    }

    /* Bound the encoded document authority before retaining its URI. */
    static void uri(String uri) throws IOException {
        if (uri == null || uri.length() > MAX_URI_LENGTH)
            throw new IOException("Source URI limit exceeded");
    }

    /* Charge an encoded URI before retaining it in a source record. */
    void authority(String uri) throws IOException {
        uri(uri);
        int bytes = utf8Length(uri);
        if (bytes > MAX_TEXT_BYTES - documentIdBytes)
            throw new IOException("Source authority limit exceeded");
        documentIdBytes += bytes;
    }

    /* Check every full or final partial-piece hash before appending. */
    static void hashAppend(int currentBytes) throws IOException {
        if (currentBytes < 0 || currentBytes > MAX_HASH_BYTES - HASH_BYTES)
            throw new IOException("Too many pieces; select a larger piece size");
    }

    // Count without allocating a provider-sized UTF-8 byte array.
    // Reject unmatched UTF-16 surrogates instead of replacement encoding.
    private static int utf8Length(String text) throws IOException {
        int bytes = 0;
        for (int i = 0; i < text.length(); ++i) {
            char value = text.charAt(i);
            if (value < 0x80)
                bytes++;
            else if (value < 0x800)
                bytes += 2;
            else if (Character.isHighSurrogate(value)) {
                if (++i >= text.length() || !Character.isLowSurrogate(text.charAt(i)))
                    throw new IOException("Malformed source text");
                bytes += 4;
            } else if (Character.isLowSurrogate(value)) {
                throw new IOException("Malformed source text");
            } else {
                bytes += 3;
            }
        }
        return bytes;
    }
}
