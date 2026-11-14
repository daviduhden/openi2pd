/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 */

package org.openi2pd.router;

/* Presentation bounds shared by snapshots, logs and accessibility. */
final class DisplayText {
    private DisplayText() {
    }

    /*
     * Cap UTF-16 units without splitting a valid surrogate pair.
     *
     * <p>Null becomes empty. A zero limit is valid; negative limits are
     * programming errors. This does not validate existing malformed
     * input or preserve grapheme clusters. Protocol operand validation
     * must retain its own byte limits and rejection policy.</p>
     */
    static String bound(String value, int maximum) {
        if (maximum < 0)
            throw new IllegalArgumentException("Negative text limit");
        if (value == null)
            return "";
        if (value.length() <= maximum)
            return value;
        int end = maximum;
        if (end > 0 && Character.isHighSurrogate(value.charAt(end - 1))
                && Character.isLowSurrogate(value.charAt(end)))
            --end;
        return value.substring(0, end);
    }
}
