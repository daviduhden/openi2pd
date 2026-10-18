package org.openi2pd.router.receivers;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import org.openi2pd.router.AutoStartController;
import org.openi2pd.router.RouterService;

/**
 * Boot autostart.
 *
 * <p>The receiver starts the foreground router service; it never launches an
 * Activity from the background. On Android 10+ a background Activity start
 * would be refused anyway, and Android 12+ restricts some background
 * foreground-service starts. The router uses the {@code specialUse} service
 * type, which is not one of the types Android 15 blocks from
 * BOOT_COMPLETED; the attempt is still fail-closed: failure is logged and the
 * user can start the router from the launcher.</p>
 */
public class BootUpReceiver extends BroadcastReceiver {
    private static final String TAG = "i2pdBoot";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            return;
        }
        if (context == null || !AutoStartController.isEnabled(context)) {
            return;
        }
        if (!RouterService.requestStart(context)) {
            Log.w(TAG, "unable to start router service after boot; "
                    + "Android may require a user-initiated start");
        }
    }
}
