package org.openi2pd.router.receivers;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import org.openi2pd.router.I2PDActivity;
import org.openi2pd.router.SettingsActivity;

import java.io.File;

public class BootUpReceiver extends BroadcastReceiver {
    private static final String TAG = "i2pdBoot";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            return;
        }

        File onBoot = SettingsActivity.getOnBootFile(context);
        if (!onBoot.isFile()) {
            return;
        }

        Intent activity = new Intent(context, I2PDActivity.class);
        activity.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(activity);
        } catch (RuntimeException e) {
            // Android 10+ may refuse background activity starts. Starting the
            // daemon still requires a user-visible foreground service design.
            Log.w(TAG, "unable to launch UI after boot", e);
        }
    }
}
