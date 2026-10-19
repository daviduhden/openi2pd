/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import android.os.Build;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;
import android.util.Log;

/**
 * Quick Settings tile.
 *
 * <p>The tile sends the same explicit start/stop commands as the UI. It never
 * creates daemon state itself, never touches native code and does not need an
 * Activity. A tile click is a user interaction, so starting a foreground
 * service from it is permitted on modern Android.</p>
 */
public class I2PdQSTileService extends TileService {
    private static final String TAG = "I2PdQSTileService";

    @Override
    public void onClick() {
        super.onClick();
        if (isLocked()) {
            // Do not expose router start/stop controls on the lock screen.
            unlockAndRun(this::toggleRouter);
            return;
        }
        toggleRouter();
    }

    private void toggleRouter() {
        boolean wasRunning = RouterService.isRouterRunning();
        boolean delivered =
                wasRunning ? RouterService.requestStop(this) : RouterService.requestStart(this);
        if (!delivered) {
            // Do not show a state the service never accepted (Android 12+
            // background-start restrictions and Android 15 BOOT_COMPLETED
            // limits can reject the command).
            Log.w(TAG, "unable to deliver router command from tile");
            updateTile(wasRunning);
            return;
        }
        // Optimistic UI; onStartListening() refreshes from the real service
        // state once the command has been applied.
        updateTile(!wasRunning);
    }

    @Override
    public void onStartListening() {
        super.onStartListening();
        updateTile(RouterService.isRouterRunning());
    }

    @Override
    public void onTileAdded() {
        super.onTileAdded();
        updateTile(RouterService.isRouterRunning());
    }

    private void updateTile(boolean running) {
        Tile tile = getQsTile();
        if (tile == null) {
            return;
        }
        tile.setState(running ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.setLabel(getString(R.string.app_name));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            tile.setContentDescription(getString(R.string.tile_state_format,
                    getString(R.string.app_name),
                    getString(running ? R.string.value_running : R.string.value_stopped)));
        }
        try {
            tile.updateTile();
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to update tile", e);
        }
    }
}
