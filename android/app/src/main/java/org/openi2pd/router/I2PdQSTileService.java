package org.openi2pd.router;

import android.app.PendingIntent;
import android.content.Intent;
import android.os.Build;
import android.service.quicksettings.TileService;
import android.util.Log;

/**
 * Quick Settings tile that opens the main UI. The tile intentionally does not
 * toggle the daemon directly: daemon start/stop needs the visible Activity for
 * notification and foreground-service state.
 */
public class I2PdQSTileService extends TileService {

    private static final String TAG = "I2PdQSTileService";

    @Override
    @SuppressWarnings("deprecation")
    public void onClick() {
        super.onClick();
        Log.d(TAG, "tile clicked");

        Intent intent = new Intent(this, I2PDActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                PendingIntent pendingIntent = PendingIntent.getActivity(
                        this, 0, intent,
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
                startActivityAndCollapse(pendingIntent);
            } else {
                startActivityAndCollapse(intent);
            }
        } catch (RuntimeException e) {
            Log.e(TAG, "unable to open main activity", e);
        }
    }

    @Override
    public void onStartListening() {
        super.onStartListening();
        Log.d(TAG, "tile started listening");
    }

    @Override
    public void onStopListening() {
        super.onStopListening();
        Log.d(TAG, "tile stopped listening");
    }

    @Override
    public void onTileAdded() {
        super.onTileAdded();
        Log.d(TAG, "tile added");
    }

    @Override
    public void onTileRemoved() {
        super.onTileRemoved();
        Log.d(TAG, "tile removed");
    }
}
