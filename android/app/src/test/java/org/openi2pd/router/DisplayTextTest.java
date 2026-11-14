/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;

import org.junit.Test;

/* Pure-JVM coverage for UTF-16 presentation truncation boundaries. */
public final class DisplayTextTest {
    @Test
    public void preservesSupplementaryCharactersAtBothSidesOfLimit() {
        String text = "a\uD840\uDC00z";
        assertEquals("a", DisplayText.bound(text, 2));
        assertEquals("a\uD840\uDC00", DisplayText.bound(text, 3));
        assertSame(text, DisplayText.bound(text, 4));
        assertSame(text, DisplayText.bound(text, 10));
    }

    @Test
    public void handlesEmptyNullAndZeroWithoutChangingBmpText() {
        assertEquals("", DisplayText.bound(null, 4));
        assertEquals("", DisplayText.bound("", 0));
        assertEquals("", DisplayText.bound("\uD840\uDC00", 1));
        assertEquals("", DisplayText.bound("text", 0));
        assertEquals("\u4E2D\u6587", DisplayText.bound("\u4E2D\u6587abc", 2));
        assertThrows(IllegalArgumentException.class, () -> DisplayText.bound("text", -1));
    }

    @Test
    public void logRecordUsesTheSharedBoundary() {
        StringBuilder prefix = new StringBuilder();
        for (int index = 0; index < 16 * 1024 - 1; ++index)
            prefix.append('x');
        LogEntry entry = new LogEntry(1, 2, "info", prefix + "\uD840\uDC00tail");
        assertEquals(prefix.toString(), entry.message());
        assertEquals("", new LogEntry(1, 2, null, null).message());
    }
}
