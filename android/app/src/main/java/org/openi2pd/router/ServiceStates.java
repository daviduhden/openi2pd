/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: ServiceStates.
 *
 * Immutable flags for the optional HTTP proxy, SOCKS, BOB, SAM and I2CP
 * services, with {@link #NONE} used before the daemon starts and on error. It
 * deliberately carries no sockets, keys or file paths.
 *
 */

package org.openi2pd.router;

/**
 * Snapshot of the optional client services exposed by the router.
 *
 * <p>This is a presentation record only. It deliberately contains booleans,
 * never listening sockets, keys or file paths.</p>
 */
public record ServiceStates(
        boolean httpProxy, boolean socksProxy, boolean bob, boolean sam, boolean i2cp) {
    /** All services disabled; used before the daemon is started or on error. */
    public static final ServiceStates NONE = new ServiceStates(false, false, false, false, false);
}
