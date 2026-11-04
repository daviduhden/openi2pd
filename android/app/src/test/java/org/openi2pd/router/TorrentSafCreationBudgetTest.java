/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: SAF creation resource-budget regressions, manual JVM CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import java.io.IOException;
import org.junit.Test;

public final class TorrentSafCreationBudgetTest {
    @FunctionalInterface
    private interface Attempt {
        void run() throws IOException;
    }

    private static void rejected(Attempt attempt) {
        try {
            attempt.run();
            fail("Expected bounded source rejection");
        } catch (IOException expected) {
            // Input operands must not appear in the rejection diagnostic.
            assertFalse(expected.getMessage().contains("secret-provider-marker"));
        }
    }

    @Test
    public void namesUseNativeUtf8BytesAndRejectMalformedUtf16() throws Exception {
        assertEquals("a".repeat(255), TorrentSafCreationBudget.component("a".repeat(255)));
        assertEquals("界".repeat(85), TorrentSafCreationBudget.component("界".repeat(85)));
        assertEquals("😀".repeat(63), TorrentSafCreationBudget.component("😀".repeat(63)));
        rejected(() -> TorrentSafCreationBudget.component("界".repeat(86)));
        rejected(() -> TorrentSafCreationBudget.component("😀".repeat(64)));
        for (String invalid : new String[] {null, "", ".", "..", "a/b", "a\\b", "a:b",
                "a<b", "a>b", "a\"b", "a|b", "a?b", "a*b",
                "a\u0000b", "\uD800", "\uDC00", "secret-provider-marker\n"})
            rejected(() -> TorrentSafCreationBudget.component(invalid));
    }

    @Test
    public void aggregatePathsStopBeforeRetainingOversizedMetadata() throws Exception {
        var budget = new TorrentSafCreationBudget();
        String path = "x".repeat(TorrentSafCreationBudget.MAX_PATH_BYTES);
        for (int i = 0; i < 1024; ++i)
            budget.document("id", path);
        rejected(() -> budget.document("secret-provider-marker", "x"));
        // Failed charges do not consume the independent ID allowance.
        budget.document("id", "");
    }

    @Test
    public void documentIdsAndEncodedUrisShareOneBoundedAllowance() throws Exception {
        var budget = new TorrentSafCreationBudget();
        String uri = "x".repeat(TorrentSafCreationBudget.MAX_URI_LENGTH);
        for (int i = 0; i < 1024; ++i)
            budget.authority(uri);
        rejected(() -> budget.document("secret-provider-marker", ""));
        rejected(() -> budget.authority("x"));
        var unicode = new TorrentSafCreationBudget();
        rejected(() -> unicode.document("界".repeat(2731), ""));
        rejected(() -> unicode.document("\uD800", ""));
        rejected(() -> unicode.document("id", "界".repeat(2731)));
        rejected(() -> unicode.authority("secret-provider-marker".repeat(400)));
        unicode.document("id", "valid");
    }

    @Test
    public void idAndPathChargesAreAtomicAndCountExactUtf8() throws Exception {
        var budget = new TorrentSafCreationBudget();
        String id = "x".repeat(8192);
        for (int i = 0; i < 1023; ++i)
            budget.document(id, "");
        budget.document("界".repeat(2730), ""); // 8190 bytes; two remain.
        rejected(() -> budget.document("界", "x".repeat(8192)));
        budget.document("ab", "x".repeat(8192));
        rejected(() -> budget.document("c", ""));
    }

    @Test
    public void everyFullAndFinalPartialHashFitsBeforeAppend() throws Exception {
        TorrentSafCreationBudget.hashAppend(0);
        TorrentSafCreationBudget.hashAppend(
                TorrentSafCreationBudget.MAX_HASH_BYTES - TorrentSafCreationBudget.HASH_BYTES);
        rejected(() -> TorrentSafCreationBudget.hashAppend(
                TorrentSafCreationBudget.MAX_HASH_BYTES));
        rejected(() -> TorrentSafCreationBudget.hashAppend(
                TorrentSafCreationBudget.MAX_HASH_BYTES - 19));
        rejected(() -> TorrentSafCreationBudget.hashAppend(-1));
    }
}
