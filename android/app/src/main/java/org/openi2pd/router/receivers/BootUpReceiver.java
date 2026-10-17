package org.openi2pd.router.receivers;


import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
//import org.openi2pd.router.ForegroundService;
//ToDo:* fix^^^ change to service, not on window on start.
import org.openi2pd.router.I2PDActivity;

import java.io.File;

import static org.openi2pd.router.SettingsActivity.onBootFileName;

public class BootUpReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        /* todo: disable the autostart? */
        File onBoot =
                new File(
                        context.getApplicationContext().getCacheDir().getAbsolutePath()
                                + onBootFileName);
        if(onBoot.exists()) {
            Intent i = new Intent(context, I2PDActivity.class);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(i);
        }

    }
}

