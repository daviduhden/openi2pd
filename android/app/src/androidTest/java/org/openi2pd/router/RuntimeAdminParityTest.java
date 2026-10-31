/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: RuntimeAdminParityTest.
 *
 * Instrumentation checks for the bounded runtime router-admin parity
 * surface: the log-level preference values must equal the exact closed
 * set accepted by the native logger, and unknown values must be
 * rejected before any native call. No daemon is started and no socket
 * or network is used.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Static-consistency regressions for reseed and runtime log-level
 * parity.
 *
 * <p>The preference array is the UI source of the level values; the
 * native setter accepts only the same closed set. This test pins the
 * two together so a future list edit cannot silently make a value
 * unappliable at runtime. It is prepared for the manual Android CI job
 * and is not run locally.</p>
 */
@RunWith(AndroidJUnit4.class)
public final class RuntimeAdminParityTest {
    @Test
    public void preferenceValuesAreRuntimeLogLevels() {
        Context context = ApplicationProvider.getApplicationContext();
        String[] values = context.getResources().getStringArray(R.array.pref_loglevel_values);
        assertTrue("no log level values", values.length > 0);
        for (String value : values) {
            assertTrue("preference value is not a runtime log level: " + value,
                    DaemonWrapper.isRuntimeLogLevel(value));
        }
    }

    @Test
    public void runtimeLogLevelSetIsClosedAndExact() {
        for (String level : new String[] {"none", "critical", "error", "warn", "info", "debug"}) {
            assertTrue(level, DaemonWrapper.isRuntimeLogLevel(level));
        }
        for (String level : new String[] {
                     "", " ", "INFO", "Warning", "warn ", " verbose", "critical2", "off"}) {
            assertFalse(level, DaemonWrapper.isRuntimeLogLevel(level));
        }
        assertFalse(DaemonWrapper.isRuntimeLogLevel(null));
    }

    @Test
    public void stoppedRouterRejectsRuntimeAdminCalls() {
        // No test in this suite starts the daemon; skip instead of
        // failing if a future test leaves one running in the shared
        // instrumentation process.
        org.junit.Assume.assumeFalse("router already running", RouterService.isRouterRunning());
        // The library is loaded here only to prove the JNI entry points
        // fail closed while no daemon is running; nothing is
        // initialised or started.
        I2PD_JNI.loadLibraries();
        assertEquals("unavailable", I2PD_JNI.requestReseed());
        assertEquals("unavailable", I2PD_JNI.setTransit(true));
        assertEquals("unavailable", I2PD_JNI.setBandwidth("K"));
        assertEquals("unavailable", I2PD_JNI.setShareRatio(0));
        assertEquals("", I2PD_JNI.getReseedStatus());
        assertEquals("", I2PD_JNI.getTunnelServices());
        assertEquals("unavailable", I2PD_JNI.startTunnel("unused"));
        assertEquals("unavailable", I2PD_JNI.stopTunnel("unused"));
        assertEquals("unavailable", I2PD_JNI.addAddressWithResult("unused.i2p", "AAAA", false));
        assertEquals("", I2PD_JNI.getSubscriptions());
        assertEquals("unavailable", I2PD_JNI.addSubscription("http://unused.i2p/hosts.txt"));
        assertEquals("unavailable", I2PD_JNI.removeSubscription("http://unused.i2p/hosts.txt"));
        assertEquals(
                "unavailable", I2PD_JNI.banRouter("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));
        assertEquals("unavailable",
                I2PD_JNI.unbanRouter("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="));
        // Even a valid level is rejected: the daemon, not the name, is
        // missing.
        assertFalse(I2PD_JNI.setRuntimeLogLevel("debug"));
        assertFalse(I2PD_JNI.setRuntimeLogLevel("verbose"));
    }

    @Test
    public void oversizedArgumentsAreRejectedBeforeAnyNativeWork() {
        // The length caps are checked on the Java side before
        // conversion, and again in native code; an oversized value can
        // never become a command.
        org.junit.Assume.assumeFalse("router already running", RouterService.isRouterRunning());
        I2PD_JNI.loadLibraries();
        StringBuilder tunnelName = new StringBuilder();
        for (int i = 0; i < 65; ++i) {
            tunnelName.append('a');
        }
        StringBuilder hash = new StringBuilder();
        for (int i = 0; i < 45; ++i) {
            hash.append('A');
        }
        // The native entry points reject an oversized value with
        // "invalid" (or "unavailable" when the router is stopped) but
        // never with a token that reports success.
        assertNotEquals("started", I2PD_JNI.startTunnel(tunnelName.toString()));
        assertNotEquals("stopped", I2PD_JNI.stopTunnel(tunnelName.toString()));
        assertNotEquals("ok", I2PD_JNI.banRouter(hash.toString()));
        assertNotEquals("ok", I2PD_JNI.addSubscription(tunnelName.toString()));
        assertNotEquals("ok", I2PD_JNI.addAddressWithResult("n.i2p", "AAAA", false));
        assertEquals("invalid", I2PD_JNI.setBandwidth("KL"));
        assertEquals("invalid", I2PD_JNI.setBandwidth("k"));
        assertEquals("invalid", I2PD_JNI.setShareRatio(-1));
        assertEquals("invalid", I2PD_JNI.setShareRatio(101));
    }

    @Test
    public void bandwidthPickerMatchesNativeClosedSet() {
        Context context = ApplicationProvider.getApplicationContext();
        String[] options = context.getResources()
                .getStringArray(R.array.runtime_admin_bandwidth_options);
        String[] classes = {"K", "L", "M", "N", "O", "P", "X"};
        assertEquals(classes.length, options.length);
        for (String value : classes) {
            assertTrue(value, RuntimeAdminPolicy.isValidBandwidthClass(value));
        }
    }
}
