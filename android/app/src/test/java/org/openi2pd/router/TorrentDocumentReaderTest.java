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
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertThrows;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.atomic.AtomicBoolean;
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

    @Test
    public void preexistingInterruptPreventsProviderReadAndPreservesFlag() {
        int[] reads = {0};
        InputStream input = new InputStream() {
            @Override
            public int read() {
                reads[0]++;
                return 'd';
            }
        };
        try {
            Thread.currentThread().interrupt();
            assertThrows(IOException.class, () -> TorrentDocumentReader.read(input, 4));
            assertEquals(0, reads[0]);
            assertTrue(Thread.currentThread().isInterrupted());
        } finally {
            Thread.interrupted();
        }
    }

    @Test
    public void interruptAtEofCannotPublishCompletedMetadata() {
        InputStream input = new ByteArrayInputStream(new byte[] {'d', 'e'}) {
            @Override
            public synchronized int read(byte[] bytes, int offset, int length) {
                int count = super.read(bytes, offset, length);
                if (count == -1)
                    Thread.currentThread().interrupt();
                return count;
            }
        };
        try {
            assertThrows(IOException.class, () -> TorrentDocumentReader.read(input, 4));
            assertTrue(Thread.currentThread().isInterrupted());
        } finally {
            Thread.interrupted();
        }
    }

    @Test
    public void callerCancellationAfterReadRejectsBytesWithoutClosingStream() {
        AtomicBoolean canceled = new AtomicBoolean();
        AtomicBoolean closed = new AtomicBoolean();
        int[] reads = {0};
        InputStream input = new InputStream() {
            @Override
            public int read() {
                throw new AssertionError("bulk read expected");
            }
            @Override
            public int read(byte[] bytes, int offset, int length) {
                reads[0]++;
                bytes[offset] = 'd';
                canceled.set(true);
                return 1;
            }
            @Override
            public void close() {
                closed.set(true);
            }
        };
        assertThrows(IOException.class, () -> TorrentDocumentReader.read(input, 4, canceled::get));
        assertEquals(1, reads[0]);
        assertFalse(closed.get());
    }

    @Test
    public void alreadyCanceledSignalPreventsFirstRead() {
        InputStream input = new InputStream() {
            @Override
            public int read() {
                throw new AssertionError("a canceled import must not read");
            }
        };
        assertThrows(IOException.class, () -> TorrentDocumentReader.read(input, 4, () -> true));
    }
}
