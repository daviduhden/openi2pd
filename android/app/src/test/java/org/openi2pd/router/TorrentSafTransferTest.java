/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: deterministic fragmented positional IO regressions.
 */
package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;

public class TorrentSafTransferTest {
    @Test
    public void fragmentsAdvanceBothOffsetsAndRemaining() throws Exception {
        List<String> calls = new ArrayList<>();
        int[] checks = {0};
        assertTrue(
                TorrentSafTransfer.exact(100, 7, () -> checks[0]++, (at, remaining, position) -> {
                    calls.add(at + ":" + remaining + ":" + position);
                    return Math.min(3, remaining);
                }));
        assertEquals(List.of("0:7:100", "3:4:103", "6:1:106"), calls);
        assertEquals(3, checks[0]);
    }

    @Test
    public void failedProgressIsNotRetriedOrAcknowledged() throws Exception {
        for (int outcome : new int[] {-1, 0, 4}) {
            int[] calls = {0};
            assertFalse(TorrentSafTransfer.exact(0, 5,
                    ()
                            -> {},
                    (at, remaining, position) -> {
                        calls[0]++;
                        return at == 0 ? 2 : outcome;
                    }));
            assertEquals(2, calls[0]);
        }
    }

    @Test
    public void cancellationAfterPartialProgressStopsBeforeNextIo() throws Exception {
        IOException canceled = new IOException("canceled");
        int[] checks = {0}, calls = {0};
        try {
            TorrentSafTransfer.exact(0, 5,
                    ()
                            -> {
                        if (++checks[0] == 2)
                            throw canceled;
                    },
                    (at, remaining, position) -> {
                        calls[0]++;
                        return 2;
                    });
            fail("Cancellation must propagate");
        } catch (IOException failure) {
            assertSame(canceled, failure);
        }
        assertEquals(1, calls[0]);
    }

    @Test
    public void ioExceptionPreservesIdentityWithoutReplay() throws Exception {
        IOException failure = new IOException("IO refused");
        int[] calls = {0};
        try {
            TorrentSafTransfer.exact(0, 5,
                    ()
                            -> {},
                    (at, remaining, position) -> {
                        if (++calls[0] == 2)
                            throw failure;
                        return 2;
                    });
            fail("IO failure must propagate");
        } catch (IOException observed) {
            assertSame(failure, observed);
        }
        assertEquals(2, calls[0]);
    }

    @Test
    public void emptyAndInvalidRangesPerformNoIo() throws Exception {
        TorrentSafTransfer.Cancellation noCheck = () -> fail("Unexpected cancellation check");
        TorrentSafTransfer.Fragment noIo = (at, remaining, position) -> {
            fail("Unexpected IO");
            return 0;
        };
        assertTrue(TorrentSafTransfer.exact(Long.MAX_VALUE, 0, noCheck, noIo));
        assertFalse(TorrentSafTransfer.exact(-1, 1, noCheck, noIo));
        assertFalse(TorrentSafTransfer.exact(0, -1, noCheck, noIo));
        assertFalse(TorrentSafTransfer.exact(0, 16385, noCheck, noIo));
        assertFalse(TorrentSafTransfer.exact(Long.MAX_VALUE, 1, noCheck, noIo));
    }
}
