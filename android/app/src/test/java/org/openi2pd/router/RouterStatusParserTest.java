/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.List;

/**
 * Pure-JVM tests for the native snapshot and log parsers. They do not call JNI
 * and do not require an emulator.
 */
public class RouterStatusParserTest {
    private static RouterSnapshot parse(String text) {
        return RouterStatusParser.parseStatus(
                text, DaemonWrapper.State.startedOkay, ServiceStates.NONE, 0L, "", "ok");
    }

    @Test
    public void parsesValidSnapshot() {
        RouterSnapshot snapshot =
                parse(String.join("\n", "status=OK", "statusCode=0", "error=none", "uptime=3600",
                        "online=1", "ntcp2=1", "ssu2=0", "peers=12", "routers=250", "inbound=1",
                        "outbound=2", "transit=3", "reseeding=0", "inRate=1024", "outRate=2048",
                        "inRate15=1000", "outRate15=2000", "totalIn=100000", "totalOut=90000",
                        "totalTransit=5000", "ipv4=NTCP2|192.0.2.1|1234|1;SSU2|192.0.2.1|1234|0",
                        "ipv6=NTCP2|[2001:db8::1]|1234|1", "unknown=future"));

        assertEquals(DaemonWrapper.State.startedOkay, snapshot.state());
        assertEquals("OK", snapshot.routerStatus());
        assertEquals(3600L, snapshot.uptimeSeconds());
        assertEquals(12, snapshot.peers());
        assertEquals(250, snapshot.routers());
        assertEquals(1, snapshot.inboundTunnels());
        assertEquals(2, snapshot.outboundTunnels());
        assertEquals(3, snapshot.transitTunnels());
        assertTrue(snapshot.online());
        assertTrue(snapshot.ntcp2());
        assertFalse(snapshot.ssu2());
        assertEquals("", snapshot.routerError());
        assertEquals(3, snapshot.addresses().size());
        assertEquals("2001:db8::1", snapshot.addresses().get(2).host());
        assertTrue(snapshot.addresses().get(2).published());
    }

    @Test
    public void malformedSnapshotIsBoundedAndSafe() {
        RouterSnapshot snapshot = parse(String.join("\n", "status=Error", "statusCode=-1",
                "error=snapshot-failed", "online=0", "uptime=-5", "peers=999999999999999999999",
                "ipv4=broken", "ipv6=SSU2|example.com|0|0"));

        assertEquals("Error", snapshot.routerStatus());
        assertEquals(0L, snapshot.uptimeSeconds());
        assertEquals(0, snapshot.peers());
        assertEquals(0, snapshot.addresses().size());
        assertFalse(snapshot.online());
    }

    @Test
    public void parsesBoundedLogRecords() {
        List<LogEntry> logs = RouterStatusParser.parseLogs("10|1700000000|2|error text|with pipe\n"
                        + "9|1699999999|4|older\n"
                        + "11|1700000001|5|debug\n"
                        + "broken\n"
                        + "12|1700000002|9|future-level",
                9);

        assertEquals(3, logs.size());
        assertEquals(10L, logs.get(0).sequence());
        assertEquals("error", logs.get(0).level());
        assertEquals("error text|with pipe", logs.get(0).message());
        assertEquals("debug", logs.get(1).level());
        assertEquals("9", logs.get(2).level());
    }

    @Test
    public void rejectsOversizedRawInput() {
        String huge = "status="
                + "x".repeat(129 * 1024);
        RouterSnapshot snapshot = parse(huge);
        assertEquals("Unknown", snapshot.routerStatus());
        assertEquals(0, snapshot.peers());
        assertTrue(RouterStatusParser.parseLogs(huge, 0).isEmpty());
    }

    @Test
    public void duplicateKeysUseTheLastValueAndIgnoreMalformedLines() {
        RouterSnapshot snapshot =
                parse(String.join("\n", "peers=1", "peers=7", "  status = Firewalled  ",
                        "not a key value line", "=orphan", "routers=not-a-number", "outbound=-4"));

        assertEquals(7, snapshot.peers());
        assertEquals("Firewalled", snapshot.routerStatus());
        assertEquals(0, snapshot.routers());
        assertEquals(0, snapshot.outboundTunnels());
    }

    @Test
    public void numericLogLevelsMapToTheDocumentedNames() {
        List<LogEntry> logs = RouterStatusParser.parseLogs("1|10|0|none\n"
                        + "2|11|1|critical\n"
                        + "3|12|2|error\n"
                        + "4|13|3|warning\n"
                        + "5|14|4|information\n"
                        + "6|15|5|debug\n"
                        + "7|16|unknown|future",
                0);

        assertEquals("none", logs.get(0).level());
        assertEquals("error", logs.get(1).level());
        assertEquals("error", logs.get(2).level());
        assertEquals("warn", logs.get(3).level());
        assertEquals("info", logs.get(4).level());
        assertEquals("debug", logs.get(5).level());
        assertEquals("unknown", logs.get(6).level());
        assertEquals(10L, logs.get(0).timestampSeconds());
    }

    @Test
    public void addressesAreBoundedAndUnbracketed() {
        StringBuilder ipv4 = new StringBuilder();
        for (int i = 0; i < 64; i++) {
            if (i > 0) {
                ipv4.append(';');
            }
            ipv4.append("NTCP2|192.0.2.").append(i % 250 + 1).append("|1234|1");
        }
        RouterSnapshot snapshot = parse("ipv4=" + ipv4);

        assertEquals(32, snapshot.addresses().size());
        assertEquals("192.0.2.1", snapshot.addresses().get(0).host());
        assertTrue(RouterStatusParser.parseLogs(null, 0).isEmpty());
    }
}
