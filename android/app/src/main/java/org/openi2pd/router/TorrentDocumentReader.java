/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentDocumentReader.
 *
 * Reads a torrent document supplied by a SAF provider into memory under the
 * 8 MiB import limit, tolerating unknown or dishonest provider lengths by
 * reading one byte past the cap only to detect overflow. Empty, oversized,
 * stalled or interrupted streams surface as {@link java.io.IOException} rather
 * than partial metadata.
 *
 */

package org.openi2pd.router;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

/** Binary SAF metadata reader: neither a provider path nor a display name is trusted. */
final class TorrentDocumentReader {
    private TorrentDocumentReader() {
    }

    static byte[] read(InputStream input) throws IOException {
        return read(input, TorrentControl.MAX_IMPORT_BYTES);
    }

    /**
     * Read a complete torrent document under a caller-selected cap. The caller
     * retains ownership of {@code input} and must close it, including on error.
     *
     * @param input stream from a user-selected document provider
     * @param maximum accepted byte count, up to the import limit
     * @return nonempty bytes read through EOF
     * @throws IOException on invalid bounds, overflow, observed interruption or stalled input
     */
    static byte[] read(InputStream input, int maximum) throws IOException {
        if (input == null || maximum < 1 || maximum > TorrentControl.MAX_IMPORT_BYTES) {
            throw new IOException("Invalid torrent input");
        }
        ByteArrayOutputStream output = new ByteArrayOutputStream(Math.min(maximum, 8192));
        byte[] buffer = new byte[Math.min(maximum, 8192)];
        int total = 0;
        for (;;) {
            // Read one additional byte at the cap so exact-cap documents are
            // accepted only at EOF; a dishonest/unknown provider length is safe.
            int length = input.read(buffer, 0, Math.min(buffer.length, maximum - total + 1));
            if (length == -1)
                break;
            if (length == 0)
                throw new IOException("Torrent provider made no progress");
            if (length > maximum - total)
                throw new IOException("Torrent exceeds the 8 MiB import limit");
            output.write(buffer, 0, length);
            total += length;
            if (Thread.currentThread().isInterrupted())
                throw new IOException("Torrent import cancelled");
        }
        if (total == 0)
            throw new IOException("Empty torrent document");
        return output.toByteArray();
    }
}
