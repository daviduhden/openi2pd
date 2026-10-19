/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: AddressInfo.
 *
 * Immutable presentation record for one published or observed transport
 * address, with the bracket notation of the native wire format already
 * stripped. The compact constructor null-normalises the text fields and clamps
 * the port to 0..65535 so native input cannot produce an out-of-range value.
 *
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
