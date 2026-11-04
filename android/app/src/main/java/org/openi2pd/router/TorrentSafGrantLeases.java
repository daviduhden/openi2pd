/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: process-local transient SAF grant ownership.
 */
package org.openi2pd.router;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BooleanSupplier;
import java.util.function.IntConsumer;
import java.util.function.IntSupplier;

/**
 * Bounded ownership of transient URI permissions across service epochs.
 * All registry operations except {@link #retire} require the caller's
 * nonreentrant grant authority. Framework callbacks run only under that
 * authority, never under a broker lock. Retiring a token is nonblocking;
 * a later authority entry drains it if immediate cleanup was busy.
 *
 * <p>Tokens include borrowers of existing permissions. Permissions
 * acquired by any borrower, or retired by a payload mapping, remain
 * until the last borrower finishes. Existing modes stay untouched.
 * Cleanup errors retain the framework grant and discard local cleanup
 * state; cleanup is best effort and never loops on a provider.</p>
 */
final class TorrentSafGrantLeases {
    static final int MAX_LEASES = 32;
    static final int MAX_URI_LENGTH = 8192;
    private static final int SUPPORTED_MODES = 3; // Android read/write flags.

    /** An opaque one-shot authority; it carries no URI or provider data. */
    static final class Token {
        private final TorrentSafGrantLeases owner;
        private final AtomicBoolean retired = new AtomicBoolean();

        private Token(TorrentSafGrantLeases owner) {
            this.owner = owner;
        }
    }

    private static final class Entry {
        private final ArrayList<Token> tokens = new ArrayList<>();
        private final BooleanSupplier mapped;
        private final IntConsumer release;
        private int ownedModes;

        private Entry(BooleanSupplier mapped, IntConsumer release) {
            this.mapped = mapped;
            this.release = release;
        }
    }

    private final Map<String, Entry> entries = new HashMap<>();
    private int leaseCount;

    /**
     * Acquire a token, including when the modes already exist. Null
     * rejects bad input, saturation or provider failure. The caller
     * must drain retired tokens before admission. URI strings are used
     * only as internal equality keys and must never reach diagnostics.
     */
    Token acquire(String uri, int requested, IntSupplier retained,
            IntConsumer take, BooleanSupplier mapped, IntConsumer release) {
        if (uri == null || uri.isEmpty() || uri.length() > MAX_URI_LENGTH
                || requested <= 0 || (requested & ~SUPPORTED_MODES) != 0
                || retained == null || take == null || mapped == null
                || release == null || leaseCount >= MAX_LEASES)
            return null;
        try {
            int missing = requested & ~retained.getAsInt();
            Entry entry = entries.get(uri);
            if (entry == null)
                entry = new Entry(mapped, release);
            Token token = new Token(this);
            // Reserve bounded local state before the framework mutation.
            entry.tokens.add(token);
            entries.put(uri, entry);
            leaseCount++;
            try {
                if (missing != 0)
                    take.accept(missing);
            } catch (RuntimeException failure) {
                entry.tokens.remove(token);
                leaseCount--;
                if (entry.tokens.isEmpty())
                    entries.remove(uri);
                return null;
            }
            entry.ownedModes |= missing;
            return token;
        } catch (RuntimeException failure) {
            return null;
        }
    }

    /**
     * Request retirement from any thread without waiting on a provider.
     * Foreign and already retired tokens cannot change registry state.
     */
    boolean retire(Token token) {
        return token != null && token.owner == this
                && token.retired.compareAndSet(false, true);
    }

    /**
     * Transfer an obsolete mapping's release obligation to its active
     * borrowers. False means no borrower blocks immediate release.
     */
    boolean deferRelease(String uri, int modes) {
        Entry entry = entries.get(uri);
        if (entry == null || modes <= 0 || (modes & ~SUPPORTED_MODES) != 0)
            return false;
        entry.ownedModes |= modes;
        return true;
    }

    /**
     * Drain pending retirement once under grant authority. A still
     * mapped URI keeps all modes; an unreferenced URI releases only
     * owned modes. Failure to read mappings retains the grant as well.
     * The bounded entry is removed even after framework failure, so
     * repeated provider errors cannot exhaust local admission forever.
     */
    void drain() {
        var iterator = entries.entrySet().iterator();
        while (iterator.hasNext()) {
            Entry entry = iterator.next().getValue();
            int before = entry.tokens.size();
            entry.tokens.removeIf(token -> token.retired.get());
            leaseCount -= before - entry.tokens.size();
            if (!entry.tokens.isEmpty())
                continue;
            try {
                if (entry.ownedModes != 0 && !entry.mapped.getAsBoolean())
                    entry.release.accept(entry.ownedModes);
            } catch (RuntimeException ignored) {
                // Retain the framework grant without retaining callbacks.
            }
            iterator.remove();
        }
    }
}
