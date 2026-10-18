/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

/**
 * One router log record in presentation form.
 *
 * <p>The native ring already sanitises control characters and bounds message
 * length. The Java-side limit is a second defence against a malformed or
 * hostile native/library response.</p>
 */
public record LogEntry(long sequence, long timestampSeconds, String level, String message) {
    /** Upper bound accepted from the native ring (native limit is 512). */
    private static final int MAX_MESSAGE_LENGTH = 16 * 1024;

    public LogEntry {
        sequence = Math.max(0L, sequence);
        timestampSeconds = Math.max(0L, timestampSeconds);
        level = level == null ? "" : level.trim();
        message = message == null ? "" : message;
        if (message.length() > MAX_MESSAGE_LENGTH) {
            message = message.substring(0, MAX_MESSAGE_LENGTH);
        }
    }
}
