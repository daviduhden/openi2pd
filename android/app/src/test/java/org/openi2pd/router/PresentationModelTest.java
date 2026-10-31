/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: PresentationModelTest.
 *
 * Pure-JVM tests for the immutable presentation records and their
 * defensive bounds: null normalisation, port clamping and message
 * truncation. No Android Context, JNI call or emulator is involved.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure-JVM tests for the immutable presentation records. They guard the
 * defensive bounds that keep malformed native input out of the UI.
 */
public class PresentationModelTest {
    @Test
    public void addressInfoNormalizesNullsAndPorts() {
        AddressInfo negative = new AddressInfo(null, null, -1, false);
        assertEquals("", negative.transport());
        assertEquals("", negative.host());
        assertEquals(0, negative.port());

        AddressInfo oversized = new AddressInfo("NTCP2", "2001:db8::1", 70000, true);
        assertEquals("NTCP2", oversized.transport());
        assertEquals(65535, oversized.port());
        assertTrue(oversized.published());
    }

    @Test
    public void logEntryClampsNegativeNumbersAndMessageLength() {
        LogEntry entry = new LogEntry(-5L, -1L, "  warn  ", "x".repeat(17_000));
        assertEquals(0L, entry.sequence());
        assertEquals(0L, entry.timestampSeconds());
        assertEquals("warn", entry.level());
        assertEquals(16 * 1024, entry.message().length());

        LogEntry empty = new LogEntry(1L, 2L, null, null);
        assertEquals("", empty.level());
        assertEquals("", empty.message());
    }

    @Test
    public void emptySnapshotClampsAndUsesSafeDefaults() {
        RouterSnapshot snapshot = RouterSnapshot.empty(null, null, null, -10L);
        assertEquals(DaemonWrapper.State.uninitialized, snapshot.state());
        assertEquals(0L, snapshot.graceRemainingMillis());
        assertEquals("", snapshot.errorText());
        assertEquals("", snapshot.startResult());
        assertTrue(snapshot.addresses().isEmpty());
        assertSame(ServiceStates.NONE, snapshot.services());
    }

    @Test
    public void snapshotOwnsACopyOfTheAddressList() {
        List<AddressInfo> addresses = new ArrayList<>();
        addresses.add(new AddressInfo("NTCP2", "192.0.2.1", 1234, true));

        RouterSnapshot snapshot = new RouterSnapshot(DaemonWrapper.State.startedOkay, true, true,
                "OK", 0, "", 10L, 1, 2, 3, 4, 5, false, true, true, 6L, 7L, 8L, 9L, 10L, 11L, 12L,
                addresses, ServiceStates.NONE, 0L, "", "ok");
        addresses.clear();

        assertEquals(1, snapshot.addresses().size());
        assertEquals("192.0.2.1", snapshot.addresses().get(0).host());
    }

    @Test
    public void serviceStatesNoneIsAllDisabled() {
        assertTrue(!ServiceStates.NONE.httpProxy() && !ServiceStates.NONE.socksProxy()
                && !ServiceStates.NONE.bob() && !ServiceStates.NONE.sam()
                && !ServiceStates.NONE.i2cp());
    }
}
