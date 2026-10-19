/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterConfigStoreTest.
 *
 * Pure-JVM tests for the static INI key and value validation used by the
 * settings editor, covering accepted daemon spellings and rejected injection or
 * boundary values. The Android Context is never constructed or called.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * Pure-JVM tests for INI value validation. The Android Context is never
 * constructed or called, so these run without an emulator.
 */
public class RouterConfigStoreTest {
    @Test
    public void acceptsValuesTheDaemonCanParse() {
        assertTrue(RouterConfigStore.isSafeValue("true"));
        assertTrue(RouterConfigStore.isSafeValue("127.0.0.1"));
        assertTrue(RouterConfigStore.isSafeValue("[2001:db8::1]"));
        assertTrue(RouterConfigStore.isSafeValue("3000"));
        assertTrue(RouterConfigStore.isSafeValue("L"));
        assertTrue(RouterConfigStore.isSafeValue("I2Pd"));
    }

    @Test
    public void rejectsIniInjectionAndUnboundedValues() {
        assertFalse(RouterConfigStore.isSafeValue(null));
        assertFalse(RouterConfigStore.isSafeValue(""));
        assertFalse(RouterConfigStore.isSafeValue(" value"));
        assertFalse(RouterConfigStore.isSafeValue("value "));
        assertFalse(RouterConfigStore.isSafeValue("a=b"));
        assertFalse(RouterConfigStore.isSafeValue("a#b"));
        assertFalse(RouterConfigStore.isSafeValue("a;b"));
        assertFalse(RouterConfigStore.isSafeValue("a\nb"));
        assertFalse(RouterConfigStore.isSafeValue("a\rb"));
        assertFalse(RouterConfigStore.isSafeValue("a\tb"));
        assertFalse(RouterConfigStore.isSafeValue("x".repeat(513)));
    }

    @Test
    public void acceptsTheMaximumBoundedValueLength() {
        assertTrue(RouterConfigStore.isSafeValue("x".repeat(512)));
    }

    @Test
    public void parsesTheIniBooleanSpellings() {
        assertTrue(RouterConfigStore.parseBoolean("1", false));
        assertTrue(RouterConfigStore.parseBoolean(" true ", false));
        assertTrue(RouterConfigStore.parseBoolean("YES", false));
        assertTrue(RouterConfigStore.parseBoolean("on", false));
        assertFalse(RouterConfigStore.parseBoolean("0", true));
        assertFalse(RouterConfigStore.parseBoolean("False", true));
        assertFalse(RouterConfigStore.parseBoolean("no", true));
        assertFalse(RouterConfigStore.parseBoolean("off", true));
        assertTrue(RouterConfigStore.parseBoolean(null, true));
        assertFalse(RouterConfigStore.parseBoolean(null, false));
        assertTrue(RouterConfigStore.parseBoolean("garbage", true));
        assertFalse(RouterConfigStore.parseBoolean("garbage", false));
    }
}
