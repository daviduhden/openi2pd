/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: Java25Probe.
 *
 * Standalone compile-time probe for the Java 25 language features used
 * by the application: records, sealed interfaces with exhaustive
 * pattern switches, record deconstruction, guarded {@code when} labels,
 * unnamed patterns and flexible constructor bodies with statements
 * before {@code super(...)}. It deliberately avoids preview features
 * and Java 25-only library APIs so the class can also be dexed at the
 * app's minimum API level.
 *
 */

package org.openi2pd.java25probe;

import java.util.List;

public final class Java25Probe {
    sealed interface Event permits Started, Stopped {}

    record Started(String name, long at) implements Event {}

    record Stopped(long at, String reason) implements Event {}

    /** Exhaustive pattern switch over a sealed hierarchy. */
    static String describe(Event event) {
        return switch (event) {
            case Started(String name, long at) when at > 0 -> "started " + name;
            // The timestamp is not used here: `_` is an unnamed
            // pattern.
            case Started(var name, _) -> "started " + name;
            case Stopped(long at, String reason) -> "stopped " + reason;
        };
    }

    static List<String> describeAll(List<Event> events) {
        return events.stream().map(Java25Probe::describe).toList();
    }

    static class Base {
        final int value;
        Base(int value) {
            this.value = value;
        }
    }

    /** JEP 513: validate before the explicit super(...) invocation. */
    static final class Validated extends Base {
        Validated(int value) {
            if (value < 0) {
                throw new IllegalArgumentException("negative: " + value);
            }
            super(value);
        }
    }

    public static void main(String[] args) {
        List<Event> events = List.of(
                new Started("router", 1L), new Started("router", 0L), new Stopped(2L, "shutdown"));
        System.out.println(describeAll(events));
        System.out.println("validated=" + new Validated(25).value);
    }
}
