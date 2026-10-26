/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterRunState.
 *
 * Persisted run intent of the router service: stopped, running or draining a
 * graceful stop until a wall-clock deadline, plus a monotonic generation. The
 * value type only touches the SharedPreferences interfaces so a plain JVM test
 * exercises the migration, expiry and corruption rules without a device.
 *
 */

package org.openi2pd.router;

import android.content.SharedPreferences;

/**
 * Immutable persisted run intent of {@link RouterService}.
 *
 * <p>The service used to persist a single {@code should_run} boolean and kept
 * the graceful-stop deadline in memory only. A process death during the drain
 * therefore looked like an ordinary running router and {@code START_STICKY} or
 * the boot receiver resurrected it. This record makes the whole intent durable:
 * the phase says what the operator asked for, the deadline says when a drain
 * ends, and the generation orders the writes so a stale restore cannot
 * overwrite a newer command.</p>
 *
 * <p><b>Persisted schema</b> (inside the existing {@code router_state}
 * preference file, so no file name or migration step changes):</p>
 * <ul>
 *   <li>{@value #KEY_PHASE} - {@code "stopped"}, {@code "running"} or
 *       {@code "draining"}.</li>
 *   <li>{@value #KEY_DEADLINE} - absolute wall-clock graceful deadline in
 *       milliseconds, {@code 0} when there is none.</li>
 *   <li>{@value #KEY_GENERATION} - monotonically increasing write
 *       generation.</li>
 *   <li>{@value #KEY_SHOULD_RUN} - the legacy boolean, always mirrored as
 *       {@code phase != Stopped} so an older build or a component that only
 *       knows the boolean keeps working.</li>
 * </ul>
 *
 * <p>The legacy boolean stays authoritative in one direction: a persisted
 * {@code false} is a terminal "do not run" and is never overridden by a stale
 * phase. A missing boolean means no complete record exists (fresh install or
 * cleared preferences), which also fails closed.</p>
 *
 * <p>The type deliberately contains no service, notification, handler or timer
 * code. Scheduling and the native calls stay in {@link RouterService}; this
 * record only answers "should the daemon run, until when, and did the drain
 * already expire".</p>
 *
 * @param phase                   requested run phase; never {@code null}
 * @param gracefulDeadlineEpochMs absolute drain deadline, {@code 0} unless the
 *                                phase is {@link Phase#Draining}
 * @param generation              persisted write generation, never negative
 */
public record RouterRunState(Phase phase, long gracefulDeadlineEpochMs, long generation) {
    /** Requested run phase. */
    public enum Phase {
        /** Never start the daemon. */
        Stopped("stopped"),
        /** Keep the daemon running until an explicit stop. */
        Running("running"),
        /**
         * Keep the daemon running until the graceful deadline, then stop it
         * for good instead of accepting transit tunnels again.
         */
        Draining("draining");

        private final String wireName;

        Phase(String wireName) {
            this.wireName = wireName;
        }

        /** Exact string persisted in {@link #KEY_PHASE}. */
        public String wireName() {
            return wireName;
        }

        /**
         * Decode a persisted phase.
         *
         * @return the phase, or {@code null} for an absent or unknown value so
         *         {@link RouterRunState#load(SharedPreferences, long)} can fall
         *         back to the legacy boolean instead of guessing
         */
        static Phase fromWire(String value) {
            if (value == null) {
                return null;
            }
            return switch (value) {
                case "stopped" -> Stopped;
                case "running" -> Running;
                case "draining" -> Draining;
                default -> null;
            };
        }
    }

    /** Legacy run flag; always written as {@code phase != Stopped}. */
    static final String KEY_SHOULD_RUN = "should_run";

    /** Persisted {@link Phase#wireName()}. */
    static final String KEY_PHASE = "run_phase";

    /** Absolute graceful-stop deadline in wall-clock milliseconds. */
    static final String KEY_DEADLINE = "graceful_deadline_epoch_ms";

    /** Monotonic write generation. */
    static final String KEY_GENERATION = "run_generation";

    /**
     * Longest plausible graceful-stop window. A persisted deadline further in
     * the future than this is corrupt and is clamped to
     * {@code now + MAX_DRAIN_WINDOW_MILLIS}; a deadline further in the past is
     * dropped. The clamp exists so a wall-clock jump cannot keep a drain alive
     * for days and a backwards jump cannot stretch it indefinitely.
     */
    public static final long MAX_DRAIN_WINDOW_MILLIS = 24L * 60L * 60L * 1000L;

    /**
     * Normalise the record: a non-draining phase carries no deadline, and a
     * draining phase without a usable deadline collapses to the terminal stop
     * it was heading for.
     */
    public RouterRunState {
        phase = phase == null ? Phase.Stopped : phase;
        if (generation < 0L) {
            generation = 0L;
        }
        if (phase == Phase.Draining && gracefulDeadlineEpochMs <= 0L) {
            phase = Phase.Stopped;
        }
        if (phase != Phase.Draining) {
            gracefulDeadlineEpochMs = 0L;
        }
    }

    /** True while the daemon may run, including an in-flight drain. */
    public boolean shouldRun() {
        return phase != Phase.Stopped;
    }

    /** True while a graceful stop is draining. */
    public boolean isDraining() {
        return phase == Phase.Draining;
    }

    /**
     * True when the drain deadline has been reached or passed.
     *
     * <p>A stopped or running state is never drain-expired; callers reconcile
     * on {@link #phase()} first.</p>
     */
    public boolean isDrainExpired(long nowMs) {
        return phase == Phase.Draining && gracefulDeadlineEpochMs <= nowMs;
    }

    /**
     * Remaining drain delay clamped to {@code [0, MAX_DRAIN_WINDOW_MILLIS]}.
     * Zero for every non-draining state and for an expired drain.
     */
    public long remainingDrainMs(long nowMs) {
        if (phase != Phase.Draining) {
            return 0L;
        }
        long remaining = gracefulDeadlineEpochMs - nowMs;
        if (remaining <= 0L) {
            return 0L;
        }
        return Math.min(remaining, MAX_DRAIN_WINDOW_MILLIS);
    }

    /**
     * Enter {@link Phase#Running} as a durable transition.
     *
     * <p>This is an instance method, unlike the {@link #stopped()} factory, so
     * every transition advances {@link #generation()}. A running state never
     * carries a deadline, so a start also clears a previous drain.</p>
     *
     * @param nowMs wall-clock observation time
     */
    public RouterRunState running(long nowMs) {
        return new RouterRunState(Phase.Running, 0L, generation + 1);
    }

    /** Start (or re-anchor) a graceful drain that ends at {@code deadlineEpochMs}. */
    public RouterRunState draining(long deadlineEpochMs) {
        return new RouterRunState(Phase.Draining, deadlineEpochMs, generation + 1);
    }

    /**
     * Cancel a drain and return to {@link Phase#Running}. A stacked or repeated
     * cancel never resurrects a stopped router, so a non-draining state is
     * returned unchanged.
     */
    public RouterRunState cancelGraceful() {
        if (phase != Phase.Draining) {
            return this;
        }
        return new RouterRunState(Phase.Running, 0L, generation + 1);
    }

    /**
     * Terminal stop: clear the deadline and advance the generation. Repeating
     * the stop is harmless and still advances the generation, so an older
     * writer cannot silently win a read-modify-write race.
     */
    public RouterRunState stop() {
        return new RouterRunState(Phase.Stopped, 0L, generation + 1);
    }

    /** The never-start state written for a fresh install or an explicit stop. */
    public static RouterRunState stopped() {
        return new RouterRunState(Phase.Stopped, 0L, 0L);
    }

    /**
     * Write the complete record, including the legacy {@code should_run}
     * mirror, into {@code editor}. The caller applies or commits exactly once;
     * {@link RouterService} uses {@code apply()}.
     */
    public void save(SharedPreferences.Editor editor) {
        if (editor == null) {
            return;
        }
        editor.putString(KEY_PHASE, phase.wireName());
        editor.putLong(KEY_DEADLINE, gracefulDeadlineEpochMs);
        editor.putLong(KEY_GENERATION, generation);
        editor.putBoolean(KEY_SHOULD_RUN, shouldRun());
    }

    /**
     * Read the persisted run intent and sanitise it against the wall clock.
     *
     * <p>Rules, in order:</p>
     * <ol>
     *   <li>A missing legacy boolean means no complete record exists, so the
     *       result is {@link Phase#Stopped}. This also keeps an older test or
     *       component that clears only {@code should_run} fail-closed.</li>
     *   <li>A persisted {@code should_run=false} is terminal and wins over a
     *       stale phase.</li>
     *   <li>A missing or unknown phase falls back to the legacy boolean.</li>
     *   <li>A draining record whose deadline is negative, missing or buried
     *       more than one window in the past is corrupt and collapses to
     *       {@link Phase#Stopped}.</li>
     *   <li>A deadline absurdly far in the future is clamped to
     *       {@code now + MAX_DRAIN_WINDOW_MILLIS}.</li>
     * </ol>
     */
    public static RouterRunState load(SharedPreferences preferences, long nowMs) {
        if (preferences == null) {
            return stopped();
        }
        if (!preferences.contains(KEY_SHOULD_RUN)) {
            return stopped();
        }
        long persistedGeneration = Math.max(0L, preferences.getLong(KEY_GENERATION, 0L));
        boolean legacyShouldRun = preferences.getBoolean(KEY_SHOULD_RUN, false);
        Phase persistedPhase = Phase.fromWire(preferences.getString(KEY_PHASE, null));
        if (persistedPhase == null) {
            // Pre-migration store: only the boolean exists.
            persistedPhase = legacyShouldRun ? Phase.Running : Phase.Stopped;
        }
        if (!legacyShouldRun) {
            return new RouterRunState(Phase.Stopped, 0L, persistedGeneration);
        }
        if (persistedPhase != Phase.Draining) {
            return new RouterRunState(persistedPhase, 0L, persistedGeneration);
        }
        long deadline = sanitizeDeadline(persistedDeadline(preferences), nowMs);
        if (deadline <= 0L) {
            // A drain without a plausible deadline is the terminal stop it was
            // heading for, not an unbounded run.
            return new RouterRunState(Phase.Stopped, 0L, persistedGeneration);
        }
        return new RouterRunState(Phase.Draining, deadline, persistedGeneration);
    }

    /** {@link #load(SharedPreferences, long)} against the current wall clock. */
    public static RouterRunState load(SharedPreferences preferences) {
        return load(preferences, System.currentTimeMillis());
    }

    /**
     * True when the store holds any part of a run record. Used by
     * {@code RouterRepository.shouldStartOnFirstLaunch()} so a fresh install is
     * distinguishable from a stopped router.
     */
    public static boolean hasPersisted(SharedPreferences preferences) {
        if (preferences == null) {
            return false;
        }
        return preferences.contains(KEY_PHASE) || preferences.contains(KEY_SHOULD_RUN);
    }

    /**
     * Raw persisted deadline without sanitisation. Callers only use it to
     * report that {@link #isPlausibleDeadline(long, long)} rejected a value
     * before it was clamped.
     */
    static long persistedDeadline(SharedPreferences preferences) {
        return preferences == null ? 0L : preferences.getLong(KEY_DEADLINE, 0L);
    }

    /**
     * Clamp a persisted deadline to the plausible window.
     *
     * @return the deadline, {@code now + MAX_DRAIN_WINDOW_MILLIS} for an absurd
     *         future value, or {@code 0} for a non-positive or deeply past
     *         value
     */
    public static long sanitizeDeadline(long deadlineEpochMs, long nowMs) {
        if (deadlineEpochMs <= 0L) {
            return 0L;
        }
        if (deadlineEpochMs < nowMs - MAX_DRAIN_WINDOW_MILLIS) {
            return 0L;
        }
        return Math.min(deadlineEpochMs, nowMs + MAX_DRAIN_WINDOW_MILLIS);
    }

    /** True when a persisted deadline needs no clamping at {@code nowMs}. */
    public static boolean isPlausibleDeadline(long deadlineEpochMs, long nowMs) {
        if (deadlineEpochMs <= 0L) {
            return true;
        }
        return deadlineEpochMs >= nowMs - MAX_DRAIN_WINDOW_MILLIS
                && deadlineEpochMs <= nowMs + MAX_DRAIN_WINDOW_MILLIS;
    }
}
