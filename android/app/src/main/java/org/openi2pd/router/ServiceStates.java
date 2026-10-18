package org.openi2pd.router;

/**
 * Snapshot of the optional client services exposed by the router.
 *
 * <p>This is a presentation record only. It deliberately contains booleans,
 * never listening sockets, keys or file paths.</p>
 */
public record ServiceStates(boolean httpProxy, boolean socksProxy, boolean bob,
                            boolean sam, boolean i2cp) {

    /** All services disabled; used before the daemon is started or on error. */
    public static final ServiceStates NONE =
            new ServiceStates(false, false, false, false, false);
}
