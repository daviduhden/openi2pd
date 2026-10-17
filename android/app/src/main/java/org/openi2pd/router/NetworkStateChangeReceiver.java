package org.openi2pd.router;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.util.Log;

public class NetworkStateChangeReceiver extends BroadcastReceiver {

    private static final String TAG = "i2pd";

    @Override
    public void onReceive(final Context context, final Intent intent) {
        if (intent == null
                || !ConnectivityManager.CONNECTIVITY_ACTION.equals(intent.getAction())) {
            return;
        }

        boolean connected = false;
        try {
            ConnectivityManager manager =
                    context.getSystemService(ConnectivityManager.class);
            if (manager != null) {
                Network network = manager.getActiveNetwork();
                NetworkCapabilities capabilities = manager.getNetworkCapabilities(network);
                connected = capabilities != null
                        && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
            }
            I2PD_JNI.onNetworkStateChanged(connected);
        } catch (UnsatisfiedLinkError e) {
            // A manifest receiver can wake the process before the daemon loads
            // the native library. There is no transport to update in that case.
            Log.d(TAG, "network change ignored: native library not loaded");
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to process network change", e);
        }
    }
}
