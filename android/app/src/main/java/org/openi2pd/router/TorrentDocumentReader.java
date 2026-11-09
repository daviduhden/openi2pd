/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: TorrentDocumentReader.
 *
 * Reads a torrent document supplied by a SAF provider into memory under
 * the 8 MiB import limit, tolerating unknown or dishonest provider
 * lengths by reading one byte past the cap only to detect overflow.
 * Empty, oversized, stalled or interrupted streams surface as {@link
 * java.io.IOException} rather than partial metadata.
 *
 */

package org.openi2pd.router;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.function.BooleanSupplier;

/*
 * Binary SAF metadata reader: neither a provider path nor a display
 * name is trusted.
 */
final class TorrentDocumentReader {
    private TorrentDocumentReader() {
    }

    static byte[] read(InputStream input) throws IOException {
        return read(input, TorrentControl.MAX_IMPORT_BYTES);
    }

    /* Read under the import cap with a caller-owned cancellation signal. */
    static byte[] read(InputStream input, BooleanSupplier canceled) throws IOException {
        return read(input, TorrentControl.MAX_IMPORT_BYTES, canceled);
    }

    /*
     * Read a complete torrent document under a caller-selected cap. The
     * caller retains ownership of {@code input} and must close it,
     * including on error.
     *
     * <p>The cap is enforced by reading one byte past it: reaching
     * {@code maximum + 1} bytes is an overflow, while exactly {@code
     * maximum} bytes followed by EOF is accepted. A provider that
     * reports a wrong length, or none at all, therefore cannot smuggle
     * metadata past the limit, and a zero-length read is treated as a
     * stall rather than as EOF.</p>
     *
     * <p>Cancellation is cooperative: the calling thread's interrupt
     * flag is checked before and after each read, including EOF. The
     * stream is never closed here, so the caller decides when the
     * provider capability is released.</p>
     *
     * @param input stream from a user-selected document provider
     * @param maximum accepted byte count, up to the import limit
     * @return nonempty bytes read through EOF
     * @throws IOException on invalid bounds, overflow, observed
     *                     interruption or stalled input
     */
    static byte[] read(InputStream input, int maximum) throws IOException {
        return read(input, maximum, () -> false);
    }

    /*
     * Read bounded metadata while observing a caller signal and the
     * thread interrupt flag. Cancellation cannot interrupt a provider
     * already blocked in read; it prevents the next read and prevents
     * accepting bytes or EOF returned after cancellation.
     *
     * @param input caller-owned document stream, never closed here
     * @param maximum maximum accepted metadata size
     * @param canceled nonblocking cancellation observation
     * @return complete, nonempty metadata within the limit
     * @throws IOException on invalid bounds, observed cancellation,
     *                     overflow, empty or stalled input
     */
    static byte[] read(InputStream input, int maximum, BooleanSupplier canceled) throws IOException {
        if (input == null || canceled == null || maximum < 1
                || maximum > TorrentControl.MAX_IMPORT_BYTES) {
            throw new IOException("Invalid torrent input");
        }
        ByteArrayOutputStream output = new ByteArrayOutputStream(Math.min(maximum, 8192));
        byte[] buffer = new byte[Math.min(maximum, 8192)];
        int total = 0;
        for (;;) {
            checkCancellation(canceled);
            // Read one additional byte at the cap so exact-cap
            // documents are accepted only at EOF; a dishonest/unknown
            // provider length is safe.
            int length = input.read(buffer, 0, Math.min(buffer.length, maximum - total + 1));
            checkCancellation(canceled);
            if (length == -1)
                break;
            if (length == 0)
                throw new IOException("Torrent provider made no progress");
            if (length > maximum - total)
                throw new IOException("Torrent exceeds the 8 MiB import limit");
            output.write(buffer, 0, length);
            total += length;
        }
        if (total == 0)
            throw new IOException("Empty torrent document");
        return output.toByteArray();
    }

    private static void checkCancellation(BooleanSupplier canceled) throws IOException {
        if (Thread.currentThread().isInterrupted() || canceled.getAsBoolean())
            throw new IOException("Torrent import cancelled");
    }
}
