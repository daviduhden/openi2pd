/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: RouterCommand.
 *
 * Typed decoding of the RouterService intent actions. The sealed
 * hierarchy is deliberately free of Android imports so the mapping is
 * exercised by a plain JUnit test and the service dispatches it through
 * an exhaustive pattern switch.
 *
 */

package org.openi2pd.router;

/**
 * One decoded {@link RouterService} start command.
 *
 * <p>{@link #fromAction(String, String)} is the only place that knows
 * the public {@code org.openi2pd.router.action.*} vocabulary. It
 * returns a record and never touches an Android API, so a unit test can
 * prove the mapping without a device, an emulator or Robolectric. The
 * permitted subclasses are the records nested in this file, which seals
 * the hierarchy implicitly: adding a command is a compile-time error
 * until {@link RouterService#onStartCommand} handles it.</p>
 *
 * <p>Commands that carry state (the runtime log level, an unrecognised
 * action) are records with a component; the remaining commands are
 * marker records. Decoding is pure: every side effect, thread hand-off
 * and {@code startId} return stays in the service dispatcher. {@link
 * RouterService#onStartCommand} calls the decoder on the main thread
 * and must dispatch it through an exhaustive pattern switch with no
 * {@code default}; a new action therefore needs a record, a mapping, a
 * service case and a {@code RouterCommandTest} update together.</p>
 */
sealed interface RouterCommand {
    /** Null or absent action: the START_STICKY restore path. */
    record Restore() implements RouterCommand {}

    record Start() implements RouterCommand {}

    record Stop() implements RouterCommand {}

    record Restart() implements RouterCommand {}

    record GracefulStop() implements RouterCommand {}

    record CancelGracefulStop() implements RouterCommand {}

    record ReloadTunnels() implements RouterCommand {}

    record Reseed() implements RouterCommand {}

    /**
     * Runtime log-level request. {@code level} may be null or unknown;
     * the service rejects both through {@link
     * DaemonWrapper#isRuntimeLogLevel} before queueing any native call.
     */
    record SetRuntimeLogLevel(String level) implements RouterCommand {}

    /**
     * Unrecognised action. {@code action} is never null, because a null
     * action maps to {@link Restore}; it is retained for diagnostics
     * only.
     */
    record Unknown(String action) implements RouterCommand {}

    /**
     * Decode an action without its extra.
     *
     * <p>The runtime log-level command carries the value of {@link
     * RouterService#EXTRA_RUNTIME_LOG_LEVEL}; this overload leaves it
     * null, which the service rejects. Callers that hold the intent use
     * {@link #fromAction(String, String)}.</p>
     */
    static RouterCommand fromAction(String action) {
        return fromAction(action, null);
    }

    /**
     * Decode an action and, for the runtime log-level command, its
     * extra.
     *
     * <p>The action strings are spelled out here rather than read from
     * {@link RouterService} so this type stays pure JVM. They must
     * remain identical to the public {@code RouterService.ACTION_*}
     * constants; {@code RouterCommandTest} pins both sides.</p>
     *
     * <p>The switch is exhaustive over the action vocabulary: a null
     * action is the sticky restore, every known action maps to one
     * record and anything else is preserved as {@link Unknown}.</p>
     */
    static RouterCommand fromAction(String action, String runtimeLogLevel) {
        return switch (action) {
            case null -> new Restore();
            case "org.openi2pd.router.action.START" -> new Start();
            case "org.openi2pd.router.action.STOP" -> new Stop();
            case "org.openi2pd.router.action.RESTART" -> new Restart();
            case "org.openi2pd.router.action.GRACEFUL_STOP" -> new GracefulStop();
            case "org.openi2pd.router.action.CANCEL_GRACEFUL_STOP" -> new CancelGracefulStop();
            case "org.openi2pd.router.action.RELOAD_TUNNELS" -> new ReloadTunnels();
            case "org.openi2pd.router.action.RESEED" -> new Reseed();
            case "org.openi2pd.router.action.SET_RUNTIME_LOG_LEVEL" ->
                new SetRuntimeLogLevel(runtimeLogLevel);
            default -> new Unknown(action);
        };
    }
}
