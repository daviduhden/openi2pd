/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: TorrentDocumentReaderTest.
 *
 * Pure-JVM tests for the SAF metadata reader: acceptance at the exact
 * limit and rejection of empty, oversized and non-progressing streams.
 * Plain {@code ByteArrayInputStream} and {@code InputStream} stand-ins
 * replace a real content provider.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertThrows;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import org.junit.Test;

public class TorrentDocumentReaderTest {
    @Test
    public void acceptsBinaryMetadataAtTheExactLimit() throws Exception {
        byte[] content = {0, -1, 'd', 'e'};
        assertArrayEquals(content,
                TorrentDocumentReader.read(new ByteArrayInputStream(content), content.length));
    }

    @Test
    public void rejectsEmptyOversizedAndUnresponsiveStreams() {
        assertThrows(IOException.class,
                () -> TorrentDocumentReader.read(new ByteArrayInputStream(new byte[0]), 4));
        assertThrows(IOException.class,
                () -> TorrentDocumentReader.read(new ByteArrayInputStream(new byte[5]), 4));
        assertThrows(IOException.class, () -> TorrentDocumentReader.read(new InputStream() {
            @Override
            public int read() {
                return 0;
            }
            @Override
            public int read(byte[] bytes, int offset, int length) {
                return 0;
            }
        }, 4));
    }

    @Test
    public void propagatesProviderReadFailures() {
        assertThrows(IOException.class, () -> TorrentDocumentReader.read(new InputStream() {
            @Override
            public int read() throws IOException {
                throw new IOException("Provider unavailable");
            }
        }, 4));
    }
}
