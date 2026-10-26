/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RuntimeAdminPolicyTest.
 *
 * Pure-JVM tests for the runtime-administration argument bounds, the bounded
 * tunnel/reseed/subscription decoders and the closed typed outcome sets. No
 * Android API, emulator or native library is involved.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.List;

/**
 * Pins the Java side of the runtime-administration JNI contract.
 *
 * <p>Every bound here is applied twice: Java rejects an oversized value before
 * it becomes a UTF-8 string and native code re-checks it. The outcome tokens
 * are protocol vocabulary and must match the daemon's
 * {@code AdminOutcome}/{@code AdminResultCode} spellings exactly.</p>
 */
public class RuntimeAdminPolicyTest {

    @Test
    public void tunnelNameBoundMatchesTheNativeVerb() {
        assertTrue(RuntimeAdminPolicy.isValidTunnelName("web"));
        assertTrue(RuntimeAdminPolicy.isValidTunnelName(repeat('a', 64)));
        assertFalse(RuntimeAdminPolicy.isValidTunnelName(repeat('a', 65)));
        assertFalse(RuntimeAdminPolicy.isValidTunnelName(""));
        assertFalse(RuntimeAdminPolicy.isValidTunnelName(null));
        // One line, no whitespace: the protocol verb is one token.
        assertFalse(RuntimeAdminPolicy.isValidTunnelName("two words"));
        assertFalse(RuntimeAdminPolicy.isValidTunnelName("tab\tname"));
        assertFalse(RuntimeAdminPolicy.isValidTunnelName("line\nbreak"));
        assertFalse(RuntimeAdminPolicy.isValidTunnelName("ctrl\u0001name"));
        assertFalse(RuntimeAdminPolicy.isValidTunnelName("/etc/i2pd/tunnels.conf"));
    }

    @Test
    public void addressAndDestinationBounds() {
        assertTrue(RuntimeAdminPolicy.isValidAddressName("site.i2p"));
        assertTrue(RuntimeAdminPolicy.isValidAddressName(repeat('a', 255)));
        assertFalse(RuntimeAdminPolicy.isValidAddressName(repeat('a', 256)));
        assertFalse(RuntimeAdminPolicy.isValidAddressName(""));
        assertFalse(RuntimeAdminPolicy.isValidAddressName("two words"));

        assertTrue(RuntimeAdminPolicy.isValidDestination("AAAA"));
        assertTrue(RuntimeAdminPolicy.isValidDestination(repeat('A', 1024)));
        assertFalse(RuntimeAdminPolicy.isValidDestination(repeat('A', 1025)));
        assertFalse(RuntimeAdminPolicy.isValidDestination(""));
    }

    @Test
    public void subscriptionAndRouterHashBounds() {
        assertTrue(RuntimeAdminPolicy.isValidSubscriptionUrl("http://reg.i2p/hosts.txt"));
        assertTrue(RuntimeAdminPolicy.isValidSubscriptionUrl(repeat('a', 512)));
        assertFalse(RuntimeAdminPolicy.isValidSubscriptionUrl(repeat('a', 513)));
        assertFalse(RuntimeAdminPolicy.isValidSubscriptionUrl("http://a i2p/hosts.txt"));

        String hash = repeat('A', 43) + "=";
        assertEquals(44, hash.length());
        assertTrue(RuntimeAdminPolicy.isValidRouterHash(hash));
        assertTrue(RuntimeAdminPolicy.isValidRouterHash("AAAA"));
        assertFalse(RuntimeAdminPolicy.isValidRouterHash(repeat('A', 45)));
        assertFalse(RuntimeAdminPolicy.isValidRouterHash(""));
        // A space or a shell metacharacter is never a router hash.
        assertFalse(RuntimeAdminPolicy.isValidRouterHash("AAAA AAAA"));
        assertFalse(RuntimeAdminPolicy.isValidRouterHash("AAAA;shutdown"));
        assertFalse(RuntimeAdminPolicy.isValidRouterHash("AAAA/../../etc"));
    }

    @Test
    public void outcomeVocabularyIsClosed() {
        assertTrue(RuntimeAdminPolicy.isOk("ok"));
        assertFalse(RuntimeAdminPolicy.isOk("OK"));
        assertFalse(RuntimeAdminPolicy.isOk("started"));
        assertTrue(RuntimeAdminPolicy.isExists("exists"));
        assertFalse(RuntimeAdminPolicy.isExists("ok"));

        assertTrue(RuntimeAdminPolicy.isReseedActive("started"));
        assertTrue(RuntimeAdminPolicy.isReseedActive("already-running"));
        assertFalse(RuntimeAdminPolicy.isReseedActive("disabled"));
        assertFalse(RuntimeAdminPolicy.isReseedActive("failed"));

        assertTrue(RuntimeAdminPolicy.isTunnelStateChange("started"));
        assertTrue(RuntimeAdminPolicy.isTunnelStateChange("stopped"));
        assertTrue(RuntimeAdminPolicy.isTunnelStateChange("already-running"));
        assertTrue(RuntimeAdminPolicy.isTunnelStateChange("already-stopped"));
        assertFalse(RuntimeAdminPolicy.isTunnelStateChange("unknown"));
        assertFalse(RuntimeAdminPolicy.isTunnelStateChange("invalid"));
    }

    @Test
    public void parseTunnelServiceRows() {
        List<RuntimeAdminPolicy.TunnelService> services =
                RuntimeAdminPolicy.parseTunnelServices(
                        "web|client|1|127.0.0.1:4444|site.i2p\n"
                                + "irc|server|0|i2p:6668|abcd\n");
        assertEquals(2, services.size());
        assertEquals("web", services.get(0).name());
        assertEquals("client", services.get(0).kind());
        assertTrue(services.get(0).running());
        assertEquals("127.0.0.1:4444", services.get(0).endpoint());
        assertEquals("site.i2p", services.get(0).destination());
        assertFalse(services.get(1).running());

        // Malformed, unnamed and short rows are skipped instead of shown.
        assertTrue(RuntimeAdminPolicy.parseTunnelServices("").isEmpty());
        assertTrue(RuntimeAdminPolicy.parseTunnelServices(null).isEmpty());
        assertTrue(RuntimeAdminPolicy.parseTunnelServices("only|four|fields|here").isEmpty());
        assertTrue(RuntimeAdminPolicy.parseTunnelServices("|client|1|e|d").isEmpty());
        assertTrue(RuntimeAdminPolicy.parseTunnelServices("bad name|client|1|e|d").isEmpty());

        // The row cap is enforced even for a hostile snapshot.
        StringBuilder many = new StringBuilder();
        for (int i = 0; i < RuntimeAdminPolicy.MAX_TUNNEL_ROWS + 10; ++i) {
            many.append("t").append(i).append("|client|0|e|d\n");
        }
        assertEquals(RuntimeAdminPolicy.MAX_TUNNEL_ROWS,
                RuntimeAdminPolicy.parseTunnelServices(many.toString()).size());
    }

    @Test
    public void parseReseedStatusRecord() {
        RuntimeAdminPolicy.ReseedStatus status =
                RuntimeAdminPolicy.parseReseedStatus("failed|1|3|1|1700000000000|1699999999000|no server");
        assertTrue(status.isKnown());
        assertEquals("failed", status.phase());
        assertEquals("1", status.enabled());
        assertEquals("3", status.attempts());
        assertEquals("1", status.succeeded());
        assertEquals("1700000000000", status.lastAttemptMs());
        assertEquals("1699999999000", status.lastSuccessMs());
        assertEquals("no server", status.lastError());

        assertFalse(RuntimeAdminPolicy.parseReseedStatus("").isKnown());
        assertFalse(RuntimeAdminPolicy.parseReseedStatus(null).isKnown());
        assertFalse(RuntimeAdminPolicy.parseReseedStatus("failed|1|3").isKnown());
        // Values are bounded: an oversized phase or error cannot grow the view.
        RuntimeAdminPolicy.ReseedStatus bounded = RuntimeAdminPolicy.parseReseedStatus(
                repeat('p', 100) + "|1|1|0|0|0|" + repeat('e', 500));
        assertTrue(bounded.isKnown());
        assertEquals(16, bounded.phase().length());
        assertEquals(128, bounded.lastError().length());
    }

    @Test
    public void parseSubscriptionsRows() {
        List<String> urls = RuntimeAdminPolicy.parseSubscriptions(
                "http://reg.i2p/hosts.txt\n\nhttp://a.i2p/h\n");
        assertEquals(2, urls.size());
        assertEquals("http://reg.i2p/hosts.txt", urls.get(0));
        assertTrue(RuntimeAdminPolicy.parseSubscriptions("").isEmpty());
        assertTrue(RuntimeAdminPolicy.parseSubscriptions(null).isEmpty());
        assertTrue(RuntimeAdminPolicy.parseSubscriptions(repeat('a', 600)).isEmpty());
    }

    private static String repeat(char value, int count) {
        StringBuilder builder = new StringBuilder(count);
        for (int i = 0; i < count; ++i) {
            builder.append(value);
        }
        return builder.toString();
    }
}
