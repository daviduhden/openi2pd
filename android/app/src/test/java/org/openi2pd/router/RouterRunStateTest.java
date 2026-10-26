/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterRunStateTest.
 *
 * Pure-JVM regression for the persisted run intent: legacy boolean migration,
 * drain expiry and clamping, corrupt deadlines, generation monotonicity and the
 * "stopped never starts" rule. A tiny in-memory SharedPreferences is used so no
 * Android framework method is ever called.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.SharedPreferences;

import org.junit.Test;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * Pure-JVM checks for {@link RouterRunState}.
 *
 * <p>These tests replace the emulator for everything the run state decides:
 * which phase survives a process death, whether a drain deadline is still
 * inside its window and whether a corrupt deadline fails closed. They
 * deliberately do not touch {@code RouterService}, a Handler, a
 * NotificationManager or a Context.</p>
 *
 * <p>The {@code onTaskRemoved}/{@code onDestroy} contract cannot be expressed
 * here: both are lifecycle callbacks on an Android {@code Service} instance and
 * need instrumentation (a bound service plus a task-removal simulation). The
 * unit-testable half of that contract is that task removal must not modify the
 * persisted state, which this file pins by asserting that a load/save round trip
 * is the only mutation path.</p>
 */
public final class RouterRunStateTest {
    /** Fixed wall clock so every deadline assertion is deterministic. */
    private static final long NOW = 1_700_000_000_000L;

    private static final long MAX = RouterRunState.MAX_DRAIN_WINDOW_MILLIS;

    @Test
    public void legacyBooleanMigratesToTheNewSchema() {
        MemoryPreferences preferences = new MemoryPreferences();
        assertFalse("a fresh install has no run record", RouterRunState.hasPersisted(preferences));
        assertFalse(RouterRunState.load(preferences, NOW).shouldRun());

        preferences.edit().putBoolean("should_run", true).apply();
        assertTrue(RouterRunState.hasPersisted(preferences));

        RouterRunState migrated = RouterRunState.load(preferences, NOW);
        assertEquals(RouterRunState.Phase.Running, migrated.phase());
        assertEquals(0L, migrated.gracefulDeadlineEpochMs());
        assertEquals(0L, migrated.generation());
        assertTrue(migrated.shouldRun());
        assertFalse(migrated.isDraining());
    }

    @Test
    public void missingLegacyFlagFailsClosedEvenWithAStalePhase() {
        MemoryPreferences preferences = new MemoryPreferences();
        // A partial write, a cleared preference or an older test that removes
        // only should_run must never restore a router.
        preferences.edit()
                .putString("run_phase", "running")
                .putLong("graceful_deadline_epoch_ms", NOW + 60_000L)
                .putLong("run_generation", 9L)
                .apply();
        assertFalse(RouterRunState.load(preferences, NOW).shouldRun());
    }

    @Test
    public void persistedFalseIsTerminalOverAStalePhase() {
        MemoryPreferences preferences = new MemoryPreferences();
        preferences.edit()
                .putBoolean("should_run", false)
                .putString("run_phase", "running")
                .putLong("run_generation", 7L)
                .apply();

        RouterRunState loaded = RouterRunState.load(preferences, NOW);
        assertEquals(RouterRunState.Phase.Stopped, loaded.phase());
        assertEquals(7L, loaded.generation());
        assertFalse(loaded.shouldRun());
    }

    @Test
    public void savedStateRoundTripsAndMirrorsTheLegacyFlag() {
        RouterRunState draining = RouterRunState.stopped().running(NOW).draining(NOW + 600_000L);
        assertTrue(draining.isDraining());

        MemoryPreferences preferences = new MemoryPreferences();
        SharedPreferences.Editor editor = preferences.edit();
        draining.save(editor);
        editor.apply();

        assertEquals("draining", preferences.getString("run_phase", null));
        assertEquals(NOW + 600_000L, preferences.getLong("graceful_deadline_epoch_ms", 0L));
        assertEquals(draining.generation(), preferences.getLong("run_generation", -1L));
        assertTrue(
                "the legacy mirror must stay usable", preferences.getBoolean("should_run", false));
        assertEquals(draining, RouterRunState.load(preferences, NOW));

        MemoryPreferences stoppedPreferences = new MemoryPreferences();
        SharedPreferences.Editor stoppedEditor = stoppedPreferences.edit();
        RouterRunState.stopped().save(stoppedEditor);
        stoppedEditor.apply();
        assertFalse(stoppedPreferences.getBoolean("should_run", true));
        assertEquals(
                RouterRunState.Phase.Stopped, RouterRunState.load(stoppedPreferences, NOW).phase());
    }

    @Test
    public void drainExpiryFollowsTheWallClock() {
        RouterRunState draining = RouterRunState.stopped().running(NOW).draining(NOW + 90_000L);

        assertFalse(draining.isDrainExpired(NOW));
        assertFalse(draining.isDrainExpired(NOW + 89_999L));
        assertTrue(draining.isDrainExpired(NOW + 90_000L));
        assertTrue(draining.isDrainExpired(NOW + 90_001L));

        assertEquals(90_000L, draining.remainingDrainMs(NOW));
        assertEquals(1L, draining.remainingDrainMs(NOW + 89_999L));
        assertEquals(0L, draining.remainingDrainMs(NOW + 90_000L));
        assertEquals(0L, draining.remainingDrainMs(NOW + 600_000L));
    }

    @Test
    public void remainingDelayIsClampedToTheMaximumWindow() {
        RouterRunState absurd =
                new RouterRunState(RouterRunState.Phase.Draining, NOW + 100L * MAX, 4L);
        assertEquals(MAX, absurd.remainingDrainMs(NOW));
        // A backwards wall-clock jump must not stretch a drain either.
        assertEquals(MAX, absurd.remainingDrainMs(NOW - 60_000L));
    }

    @Test
    public void cancelGracefulReturnsToRunningAndClearsTheDeadline() {
        RouterRunState draining = RouterRunState.stopped().running(NOW).draining(NOW + 60_000L);
        RouterRunState cancelled = draining.cancelGraceful();

        assertEquals(RouterRunState.Phase.Running, cancelled.phase());
        assertEquals(0L, cancelled.gracefulDeadlineEpochMs());
        assertEquals(draining.generation() + 1L, cancelled.generation());
        assertTrue(cancelled.shouldRun());
        assertFalse(cancelled.isDrainExpired(NOW + 600_000L));

        // A repeated cancel is a no-op and can never resurrect a stopped router.
        assertEquals(cancelled, cancelled.cancelGraceful());
        assertEquals(
                RouterRunState.Phase.Stopped, RouterRunState.stopped().cancelGraceful().phase());
    }

    @Test
    public void stopClearsTheDeadlineAndAdvancesTheGeneration() {
        RouterRunState draining = RouterRunState.stopped().running(NOW).draining(NOW + 60_000L);
        RouterRunState stopped = draining.stop();

        assertEquals(RouterRunState.Phase.Stopped, stopped.phase());
        assertEquals(0L, stopped.gracefulDeadlineEpochMs());
        assertEquals(draining.generation() + 1L, stopped.generation());
        assertFalse(stopped.shouldRun());
        assertEquals(0L, stopped.remainingDrainMs(NOW));
        assertFalse(stopped.isDrainExpired(NOW));

        // Repeating the stop still advances the generation, so a stale writer
        // cannot win a read-modify-write race.
        assertEquals(stopped.generation() + 1L, stopped.stop().generation());
    }

    @Test
    public void corruptDeadlinesAreDroppedOrClamped() {
        // Negative deadline.
        MemoryPreferences negative = drainingPreferences(-5L);
        assertEquals(RouterRunState.Phase.Stopped, RouterRunState.load(negative, NOW).phase());

        // Missing deadline.
        MemoryPreferences missing = new MemoryPreferences();
        missing.edit().putBoolean("should_run", true).putString("run_phase", "draining").apply();
        assertEquals(RouterRunState.Phase.Stopped, RouterRunState.load(missing, NOW).phase());

        // Buried more than one window in the past.
        MemoryPreferences deepPast = drainingPreferences(NOW - 2L * MAX);
        assertEquals(RouterRunState.Phase.Stopped, RouterRunState.load(deepPast, NOW).phase());

        // Absurdly far in the future: clamped to the configured maximum.
        long absurdDeadline = NOW + 365L * MAX;
        MemoryPreferences absurd = drainingPreferences(absurdDeadline);
        RouterRunState clamped = RouterRunState.load(absurd, NOW);
        assertEquals(RouterRunState.Phase.Draining, clamped.phase());
        assertEquals(NOW + MAX, clamped.gracefulDeadlineEpochMs());
        assertEquals(MAX, clamped.remainingDrainMs(NOW));
        assertFalse("the stored deadline must be reported as corrupt",
                RouterRunState.isPlausibleDeadline(absurdDeadline, NOW));
        assertTrue(RouterRunState.isPlausibleDeadline(clamped.gracefulDeadlineEpochMs(), NOW));

        // Sanitisation itself is total: non-positive and deeply past values
        // become 0, a future value inside the window is preserved.
        assertEquals(0L, RouterRunState.sanitizeDeadline(0L, NOW));
        assertEquals(0L, RouterRunState.sanitizeDeadline(-1L, NOW));
        assertEquals(0L, RouterRunState.sanitizeDeadline(NOW - 2L * MAX, NOW));
        assertEquals(NOW + 1000L, RouterRunState.sanitizeDeadline(NOW + 1000L, NOW));
        assertEquals(NOW + MAX, RouterRunState.sanitizeDeadline(Long.MAX_VALUE, NOW));
    }

    @Test
    public void generationIsMonotonicAcrossTransitionsAndReload() {
        RouterRunState state = RouterRunState.stopped();
        long previous = state.generation();

        state = state.running(NOW);
        assertTrue(state.generation() > previous);
        previous = state.generation();

        state = state.draining(NOW + 60_000L);
        assertTrue(state.generation() > previous);
        previous = state.generation();

        state = state.cancelGraceful();
        assertTrue(state.generation() > previous);
        previous = state.generation();

        state = state.draining(NOW + 30_000L);
        assertTrue(state.generation() > previous);
        previous = state.generation();

        state = state.stop();
        assertTrue(state.generation() > previous);

        // The generation is part of the durable record.
        MemoryPreferences preferences = new MemoryPreferences();
        SharedPreferences.Editor editor = preferences.edit();
        state.save(editor);
        editor.apply();
        assertEquals(state.generation(), RouterRunState.load(preferences, NOW).generation());

        // A corrupt negative generation is normalised, never trusted.
        MemoryPreferences corrupt = new MemoryPreferences();
        corrupt.edit()
                .putBoolean("should_run", true)
                .putString("run_phase", "running")
                .putLong("run_generation", -3L)
                .apply();
        assertEquals(0L, RouterRunState.load(corrupt, NOW).generation());
    }

    @Test
    public void stoppedStateNeverRequestsAStart() {
        RouterRunState stopped = RouterRunState.stopped();
        assertFalse(stopped.shouldRun());
        assertFalse(stopped.isDraining());
        assertFalse(stopped.isDrainExpired(NOW));
        assertEquals(0L, stopped.remainingDrainMs(NOW));
        assertEquals(RouterRunState.Phase.Stopped, stopped.phase());

        // A draining record without a usable deadline is the terminal stop it
        // was heading for, not an unbounded run.
        assertEquals(RouterRunState.Phase.Stopped,
                new RouterRunState(RouterRunState.Phase.Draining, 0L, 1L).phase());
        assertEquals(RouterRunState.Phase.Stopped,
                new RouterRunState(RouterRunState.Phase.Draining, -1L, 1L).phase());
        // A null phase and a negative generation are normalised, not thrown.
        RouterRunState normalised = new RouterRunState(null, NOW + 1000L, -9L);
        assertEquals(RouterRunState.Phase.Stopped, normalised.phase());
        assertEquals(0L, normalised.gracefulDeadlineEpochMs());
        assertEquals(0L, normalised.generation());
    }

    @Test
    public void nullStoreFailsClosed() {
        assertFalse(RouterRunState.load(null).shouldRun());
        assertEquals(RouterRunState.Phase.Stopped, RouterRunState.load(null, NOW).phase());
        assertFalse(RouterRunState.hasPersisted(null));
        assertEquals(0L, RouterRunState.persistedDeadline(null));
    }

    @Test
    public void hasPersistedRecognizesBothSchemaVersions() {
        MemoryPreferences legacy = new MemoryPreferences();
        legacy.edit().putBoolean("should_run", false).apply();
        assertTrue(
                "a legacy stop is still a persisted record", RouterRunState.hasPersisted(legacy));

        MemoryPreferences current = new MemoryPreferences();
        SharedPreferences.Editor editor = current.edit();
        RouterRunState.stopped().save(editor);
        editor.apply();
        assertTrue(RouterRunState.hasPersisted(current));
        assertNotNull(RouterRunState.load(current, NOW));
    }

    private static MemoryPreferences drainingPreferences(long deadline) {
        MemoryPreferences preferences = new MemoryPreferences();
        preferences.edit()
                .putBoolean("should_run", true)
                .putString("run_phase", "draining")
                .putLong("graceful_deadline_epoch_ms", deadline)
                .putLong("run_generation", 2L)
                .apply();
        return preferences;
    }

    /**
     * Minimal in-memory {@link SharedPreferences}.
     *
     * <p>Only the methods {@link RouterRunState} uses do real work; the
     * listener bookkeeping is intentionally ignored because the run state has
     * no observers. Nothing here calls an Android framework method, so the
     * test runs on a plain JVM.</p>
     */
    private static final class MemoryPreferences implements SharedPreferences {
        private final Map<String, Object> values = new HashMap<>();

        @Override
        public Map<String, ?> getAll() {
            return Map.copyOf(values);
        }

        @Override
        public String getString(String key, String defValue) {
            Object value = values.get(key);
            return value instanceof String text ? text : defValue;
        }

        @Override
        public Set<String> getStringSet(String key, Set<String> defValues) {
            Object value = values.get(key);
            if (value instanceof Set<?> stored) {
                @SuppressWarnings("unchecked") Set<String> typed = (Set<String>) stored;
                return typed;
            }
            return defValues;
        }

        @Override
        public int getInt(String key, int defValue) {
            Object value = values.get(key);
            return value instanceof Integer number ? number : defValue;
        }

        @Override
        public long getLong(String key, long defValue) {
            Object value = values.get(key);
            return value instanceof Long number ? number : defValue;
        }

        @Override
        public float getFloat(String key, float defValue) {
            Object value = values.get(key);
            return value instanceof Float number ? number : defValue;
        }

        @Override
        public boolean getBoolean(String key, boolean defValue) {
            Object value = values.get(key);
            return value instanceof Boolean flag ? flag : defValue;
        }

        @Override
        public boolean contains(String key) {
            return values.containsKey(key);
        }

        @Override
        public Editor edit() {
            return new MemoryEditor();
        }

        @Override
        public void registerOnSharedPreferenceChangeListener(
                OnSharedPreferenceChangeListener listener) {
            // The run state has no observers.
        }

        @Override
        public void unregisterOnSharedPreferenceChangeListener(
                OnSharedPreferenceChangeListener listener) {
            // The run state has no observers.
        }

        /** Buffered editor that publishes on apply()/commit() like the framework. */
        private final class MemoryEditor implements Editor {
            private final Map<String, Object> pending = new HashMap<>();
            private final Set<String> removed = new HashSet<>();
            private boolean cleared;

            @Override
            public Editor putString(String key, String value) {
                return set(key, value);
            }

            @Override
            public Editor putStringSet(String key, Set<String> value) {
                return set(key, value == null ? null : Set.copyOf(value));
            }

            @Override
            public Editor putInt(String key, int value) {
                return set(key, value);
            }

            @Override
            public Editor putLong(String key, long value) {
                return set(key, value);
            }

            @Override
            public Editor putFloat(String key, float value) {
                return set(key, value);
            }

            @Override
            public Editor putBoolean(String key, boolean value) {
                return set(key, value);
            }

            @Override
            public Editor remove(String key) {
                pending.remove(key);
                removed.add(key);
                return this;
            }

            @Override
            public Editor clear() {
                cleared = true;
                return this;
            }

            @Override
            public boolean commit() {
                apply();
                return true;
            }

            @Override
            public void apply() {
                if (cleared) {
                    values.clear();
                    cleared = false;
                }
                for (String key : removed) {
                    values.remove(key);
                }
                removed.clear();
                values.putAll(pending);
                pending.clear();
            }

            private Editor set(String key, Object value) {
                if (value == null) {
                    return remove(key);
                }
                removed.remove(key);
                pending.put(key, value);
                return this;
            }
        }
    }
}
