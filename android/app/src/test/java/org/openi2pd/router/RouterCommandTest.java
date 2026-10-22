/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterCommandTest.
 *
 * Pure-JVM tests for the sealed RouterCommand action decoder. They pin every
 * public action string, the null sticky-restore mapping, the unknown fallback
 * and the runtime log-level record. The RouterService action fields are
 * compile-time String constants, so javac inlines them; no Android class or
 * native library is loaded by these tests.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

import java.util.Locale;

/**
 * Pure-JVM tests for {@link RouterCommand#fromAction(String)}. No Android API,
 * Robolectric, emulator or native library is involved.
 */
public class RouterCommandTest {
    private static final String ACTION_START = "org.openi2pd.router.action.START";
    private static final String ACTION_STOP = "org.openi2pd.router.action.STOP";
    private static final String ACTION_RESTART = "org.openi2pd.router.action.RESTART";
    private static final String ACTION_GRACEFUL_STOP =
            "org.openi2pd.router.action.GRACEFUL_STOP";
    private static final String ACTION_CANCEL_GRACEFUL_STOP =
            "org.openi2pd.router.action.CANCEL_GRACEFUL_STOP";
    private static final String ACTION_RELOAD_TUNNELS =
            "org.openi2pd.router.action.RELOAD_TUNNELS";
    private static final String ACTION_RESEED = "org.openi2pd.router.action.RESEED";
    private static final String ACTION_SET_RUNTIME_LOG_LEVEL =
            "org.openi2pd.router.action.SET_RUNTIME_LOG_LEVEL";

    @Test
    public void mapsEveryPublicAction() {
        assertEquals(new RouterCommand.Start(), RouterCommand.fromAction(ACTION_START));
        assertEquals(new RouterCommand.Stop(), RouterCommand.fromAction(ACTION_STOP));
        assertEquals(new RouterCommand.Restart(), RouterCommand.fromAction(ACTION_RESTART));
        assertEquals(new RouterCommand.GracefulStop(),
                RouterCommand.fromAction(ACTION_GRACEFUL_STOP));
        assertEquals(new RouterCommand.CancelGracefulStop(),
                RouterCommand.fromAction(ACTION_CANCEL_GRACEFUL_STOP));
        assertEquals(new RouterCommand.ReloadTunnels(),
                RouterCommand.fromAction(ACTION_RELOAD_TUNNELS));
        assertEquals(new RouterCommand.Reseed(), RouterCommand.fromAction(ACTION_RESEED));
        assertEquals(new RouterCommand.SetRuntimeLogLevel(null),
                RouterCommand.fromAction(ACTION_SET_RUNTIME_LOG_LEVEL));
    }

    @Test
    public void nullActionRequestsStickyRestore() {
        assertEquals(new RouterCommand.Restore(), RouterCommand.fromAction(null));
        assertEquals(new RouterCommand.Restore(), RouterCommand.fromAction(null, "debug"));
    }

    @Test
    public void unknownActionsArePreservedForLogging() {
        assertEquals(new RouterCommand.Unknown(""), RouterCommand.fromAction(""));
        assertEquals(new RouterCommand.Unknown("org.openi2pd.router.action.STARTED"),
                RouterCommand.fromAction("org.openi2pd.router.action.STARTED"));
        String lowerCase = ACTION_START.toLowerCase(Locale.ROOT);
        assertEquals(new RouterCommand.Unknown(lowerCase),
                RouterCommand.fromAction(lowerCase));
        assertEquals(new RouterCommand.Unknown(ACTION_START + " "),
                RouterCommand.fromAction(ACTION_START + " "));
        // A level extra must not turn an unknown action into a log-level command.
        assertEquals(new RouterCommand.Unknown(ACTION_START + " "),
                RouterCommand.fromAction(ACTION_START + " ", "debug"));
    }

    @Test
    public void runtimeLogLevelCarriesTheExtraAndMayBeNull() {
        RouterCommand.SetRuntimeLogLevel bare = (RouterCommand.SetRuntimeLogLevel)
                RouterCommand.fromAction(ACTION_SET_RUNTIME_LOG_LEVEL);
        assertNull(bare.level());
        assertEquals(new RouterCommand.SetRuntimeLogLevel("debug"),
                RouterCommand.fromAction(ACTION_SET_RUNTIME_LOG_LEVEL, "debug"));
        assertEquals(new RouterCommand.SetRuntimeLogLevel(null),
                RouterCommand.fromAction(ACTION_SET_RUNTIME_LOG_LEVEL, null));
        assertNotEquals(new RouterCommand.SetRuntimeLogLevel("debug"),
                new RouterCommand.SetRuntimeLogLevel(null));
        assertNotEquals(new RouterCommand.SetRuntimeLogLevel("debug"),
                new RouterCommand.SetRuntimeLogLevel("info"));
    }

    @Test
    public void recordsExposeTheirDiagnosticComponents() {
        assertEquals(ACTION_START + "?",
                new RouterCommand.Unknown(ACTION_START + "?").action());
        assertEquals("debug", new RouterCommand.SetRuntimeLogLevel("debug").level());
    }

    @Test
    public void stringsMatchTheServiceConstants() {
        // RouterService.ACTION_* are constant variables, so javac substitutes
        // their values here and RouterService is not loaded at test runtime.
        assertEquals(RouterService.ACTION_START, ACTION_START);
        assertEquals(RouterService.ACTION_STOP, ACTION_STOP);
        assertEquals(RouterService.ACTION_RESTART, ACTION_RESTART);
        assertEquals(RouterService.ACTION_GRACEFUL_STOP, ACTION_GRACEFUL_STOP);
        assertEquals(RouterService.ACTION_CANCEL_GRACEFUL_STOP, ACTION_CANCEL_GRACEFUL_STOP);
        assertEquals(RouterService.ACTION_RELOAD_TUNNELS, ACTION_RELOAD_TUNNELS);
        assertEquals(RouterService.ACTION_RESEED, ACTION_RESEED);
        assertEquals(RouterService.ACTION_SET_RUNTIME_LOG_LEVEL,
                ACTION_SET_RUNTIME_LOG_LEVEL);
    }
}
