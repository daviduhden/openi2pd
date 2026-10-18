/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Locale;
import java.util.Objects;

/**
 * Autostart preference and its app-private marker file.
 *
 * <p>The marker's existence is the setting. It lives in {@code filesDir} so
 * the system cannot clear it as cache; a marker left by older versions in
 * {@code cacheDir} is migrated once. Enabling also opens the manufacturer's
 * autostart screen when a known component exists. That is a user-visible
 * action, but this class itself never needs an Activity and never starts the
 * router.</p>
 */
public final class AutoStartController {
    private static final String TAG = "AutoStartController";
    private static final String MARKER_NAME = "onBoot";
    private static final String TEMP_NAME = "onBoot.tmp";

    private AutoStartController() {
        // Static helpers.
    }

    /** Marker file, migrating the legacy cache marker if necessary. */
    public static File getMarker(Context context) {
        Objects.requireNonNull(context, "context");
        Context appContext = applicationContext(context);
        File marker = new File(appContext.getFilesDir(), MARKER_NAME);
        if (marker.isFile()) {
            return marker;
        }
        File legacy = new File(appContext.getCacheDir(), MARKER_NAME);
        if (!legacy.isFile()) {
            return marker;
        }
        File temporary = new File(appContext.getFilesDir(), TEMP_NAME);
        try {
            writeEmptyMarker(temporary);
            if (temporary.renameTo(marker)) {
                if (!legacy.delete()) {
                    Log.w(TAG, "unable to remove legacy autostart marker");
                }
                return marker;
            }
            Log.w(TAG, "unable to migrate autostart marker");
        } catch (IOException e) {
            Log.w(TAG, "unable to migrate autostart marker", e);
        } finally {
            if (temporary.exists() && !temporary.delete()) {
                Log.w(TAG, "unable to delete temporary autostart marker");
            }
        }
        // Keep honouring the old marker rather than silently disabling
        // autostart after a failed migration.
        return legacy;
    }

    public static boolean isEnabled(Context context) {
        return getMarker(context).isFile();
    }

    /**
     * Enable/disable boot autostart and, when enabling, open a known vendor
     * autostart screen for the user. See {@link #setEnabled(Context, boolean,
     * boolean)} for a variant that keeps the marker write off the main thread.
     */
    public static boolean setEnabled(Context context, boolean enabled) {
        return setEnabled(context, enabled, true);
    }

    /**
     * Enable/disable boot autostart.
     *
     * @param showVendorSettings when {@code true}, enabling also starts a known
     *                           vendor autostart screen. The caller must be on
     *                           the main thread in that case. When {@code false}
     *                           the method only touches the app-private marker,
     *                           so it is safe to call on a worker thread.
     * @return {@code false} only when the marker itself could not be changed
     */
    public static boolean setEnabled(Context context, boolean enabled,
                                     boolean showVendorSettings) {
        Objects.requireNonNull(context, "context");
        Context appContext = applicationContext(context);
        File marker = getMarker(appContext);

        if (!enabled) {
            if (!marker.isFile()) {
                return true;
            }
            if (marker.delete()) {
                return true;
            }
            Log.w(TAG, "unable to delete autostart marker");
            return false;
        }

        if (marker.isFile()) {
            if (showVendorSettings) {
                openVendorAutostart(appContext);
            }
            return true;
        }
        try {
            File parent = marker.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                Log.w(TAG, "unable to create autostart marker directory");
                return false;
            }
            writeEmptyMarker(marker);
        } catch (IOException e) {
            Log.e(TAG, "unable to create autostart marker", e);
            return false;
        }
        if (showVendorSettings) {
            openVendorAutostart(appContext);
        }
        return true;
    }

    /** Open a known vendor autostart screen; must be called on the main thread. */
    public static void openVendorAutostartSettings(Context context) {
        if (context == null) {
            return;
        }
        openVendorAutostart(applicationContext(context));
    }

    private static void writeEmptyMarker(File file) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            // The marker is empty; its existence is the setting.
            out.flush();
            out.getFD().sync();
        }
    }

    private static Context applicationContext(Context context) {
        Context appContext = context.getApplicationContext();
        return appContext == null ? context : appContext;
    }

    private static void openVendorAutostart(Context context) {
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
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            // Do not pre-resolve: package visibility can hide the vendor
            // activity even though it is launchable by an explicit intent.
            context.startActivity(intent);
        } catch (ActivityNotFoundException e) {
            Log.d(TAG, "autostart settings activity not available on this device");
        } catch (RuntimeException e) {
            // Android 10+ may refuse an activity start from the background;
            // the marker is still valid and the service can be started later.
            Log.w(TAG, "unable to open autostart settings", e);
        }
    }
}
