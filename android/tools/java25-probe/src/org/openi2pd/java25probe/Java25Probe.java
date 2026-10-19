/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Compile-time probe for the Java 25 language features used by the Android
 * sources. It deliberately uses no Java 25-only library API and no preview
 * features: everything below is final in Java 25.
 *
 * Features exercised:
 *   - records
 *   - sealed interfaces with an exhaustive pattern switch
 *   - record deconstruction patterns
 *   - guarded pattern switch labels (`when`)
 *   - unnamed patterns (`_`)  [JEP 456]
 *   - flexible constructor bodies with statements before super(...) [JEP 513]
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
            // The timestamp is not used here: `_` is an unnamed pattern.
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
