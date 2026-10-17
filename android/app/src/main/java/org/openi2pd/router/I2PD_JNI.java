package org.openi2pd.router;

public final class I2PD_JNI {
    private static boolean loaded;

    private I2PD_JNI() {
        // Native entry points only.
    }

    public static native String getABICompiledWith();

    public static synchronized void loadLibraries() {
        if (loaded) {
            return;
        }
        System.loadLibrary("i2pd");
        loaded = true;
    }

    /**
     * returns error info if failed
     * returns "ok" if daemon initialized and started okay
     */
    public static native String startDaemon();
    public static native void stopDaemon();

    public static native void startAcceptingTunnels();
    public static native void stopAcceptingTunnels();
    public static native void reloadTunnelsConfigs();

    public static native void setDataDir(String jdataDir);
    public static native void setLanguage(String jlanguage);

    public static native int getTransitTunnelsCount();
    public static native String getDataDir();
    public static native String getStatusSummary();

    public static native boolean getHTTPProxyState();
    public static native boolean getSOCKSProxyState();
    public static native boolean getBOBState();
    public static native boolean getSAMState();
    public static native boolean getI2CPState();

    public static native void onNetworkStateChanged(boolean isConnected);
}
