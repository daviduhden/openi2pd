package org.openi2pd.router;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.MenuItem;
import android.widget.Button;
import android.widget.Switch;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Locale;

public class SettingsActivity extends Activity {
    private static final String TAG = "i2pdServices";
    public static final String onBootFileName = "onBoot";

    /**
     * Autostart marker, stored in app-private files rather than cache so the
     * system cannot clear it and silently disable autostart.
     */
    public static File getOnBootFile(Context context) {
        Context appContext = context.getApplicationContext();
        if (appContext == null) {
            appContext = context;
        }
        File marker = new File(appContext.getFilesDir(), onBootFileName);
        if (marker.isFile()) {
            return marker;
        }
        File legacy = new File(appContext.getCacheDir(), onBootFileName);
        if (legacy.isFile()) {
            try (FileOutputStream out = new FileOutputStream(marker)) {
                // The marker is empty; its existence is the setting.
                out.flush();
            } catch (IOException e) {
                Log.w(TAG, "unable to migrate autostart marker", e);
                return legacy;
            }
            if (!legacy.delete()) {
                Log.w(TAG, "unable to remove legacy autostart marker");
            }
        }
        return marker;
    }

    private void addAutoStartupSwitch() {
        String manufacturer = Build.MANUFACTURER == null
                ? "" : Build.MANUFACTURER.toLowerCase(Locale.ROOT);

        ComponentName component = switch (manufacturer) {
            case "xiaomi" -> new ComponentName("com.miui.securitycenter",
                    "com.miui.permcenter.autostart.AutoStartManagementActivity");
            case "oppo" -> new ComponentName("com.coloros.safecenter",
                    "com.coloros.safecenter.permission.startup.StartupAppListActivity");
            case "vivo" -> new ComponentName("com.vivo.permissionmanager",
                    "com.vivo.permissionmanager.activity.BgStartUpManagerActivity");
            case "letv" -> new ComponentName("com.letv.android.letvsafe",
                    "com.letv.android.letvsafe.AutobootManageActivity");
            case "honor" -> new ComponentName("com.huawei.systemmanager",
                    "com.huawei.systemmanager.optimize.process.ProtectActivity");
            case "oneplus" -> new ComponentName("com.oneplus.security",
                    "com.oneplus.security.chainlaunch.view.ChainLaunchAppListActivity");
            default -> null;
        };
        if (component == null) {
            return;
        }

        Intent intent = new Intent().setComponent(component);
        try {
            // Do not pre-resolve: package-visibility filtering can hide the
            // vendor activity even though it can be launched. Start it and let
            // ActivityNotFoundException report the unsupported case.
            startActivity(intent);
        } catch (ActivityNotFoundException e) {
            Log.d(TAG, "autostart settings activity not available on this device");
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to open autostart settings", e);
        }
    }

    public void onCreate(Bundle savedInstanceState) {
        Log.d(TAG, "onCreate");
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_settings);

        if (getActionBar() != null) {
            getActionBar().setDisplayHomeAsUpEnabled(true);
        }

        Switch autostartSwitch = findViewById(R.id.autostart_enable);
        Button openPreferences = findViewById(R.id.OpenPreferences);
        File onBoot = getOnBootFile(getApplicationContext());

        // Set the initial state before installing the listener so restoring the
        // switch does not trigger manufacturer-specific settings screens.
        autostartSwitch.setChecked(onBoot.isFile());
        openPreferences.setOnClickListener(view ->
                startActivity(new Intent(SettingsActivity.this,
                        MainPreferenceActivity.class)));
        autostartSwitch.setOnCheckedChangeListener(
                (buttonView, isChecked) -> {
                    if (!setAutostart(onBoot, isChecked)) {
                        buttonView.setChecked(!isChecked);
                    }
                });
    }

    private boolean setAutostart(File onBoot, boolean enabled) {
        if (enabled) {
            if (onBoot.isFile()) {
                return true;
            }
            try (FileOutputStream out = new FileOutputStream(onBoot)) {
                out.flush();
            } catch (IOException e) {
                Log.e(TAG, "unable to create autostart marker", e);
                Toast.makeText(this, R.string.autostart_marker_failed,
                        Toast.LENGTH_SHORT).show();
                return false;
            }
            addAutoStartupSwitch();
            return true;
        }
        if (onBoot.isFile() && !onBoot.delete()) {
            Log.w(TAG, "unable to delete autostart marker");
            return false;
        }
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == android.R.id.home) {
            finish();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }
}
