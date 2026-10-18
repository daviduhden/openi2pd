/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

/**
 * One published or observed transport address.
 *
 * <p>Instances are immutable. {@code host} is a plain IPv4/IPv6 literal; the
 * brackets used in the native wire format are removed before publication.</p>
 */
public record AddressInfo(String transport, String host, int port, boolean published) {

    public AddressInfo {
        transport = transport == null ? "" : transport;
        host = host == null ? "" : host;
        if (port < 0) {
            port = 0;
        } else if (port > 65535) {
            port = 65535;
        }
    }
}
