package org.openi2pd.router;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.CheckBox;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.core.content.ContextCompat;

import static android.provider.Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS;

public class I2PDActivity extends Activity {
    private static final String TAG = "i2pdActvt";
    public static final int GRACEFUL_DELAY_MILLIS =
            (int) DaemonWrapper.GRACEFUL_STOP_DELAY_MILLIS;
    public static final String PACKAGE_URI_SCHEME = "package:";
    private static final int REQUEST_POST_NOTIFICATIONS = 3;
    private static final String BATTERY_OPTIMIZATION_PREFERENCE =
            "show_battery_optimization";

    private static final Object DAEMON_LOCK = new Object();
    private static volatile DaemonWrapper daemon;

    private TextView textView;
    private CheckBox HTTPProxyState;
    private CheckBox SOCKSProxyState;
    private CheckBox BOBState;
    private CheckBox SAMState;
    private CheckBox I2CPState;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final AtomicBoolean statusQueryPending = new AtomicBoolean(false);
    private final AtomicBoolean finishing = new AtomicBoolean(false);

    private final ExecutorService statusExecutor = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "i2pdStatus");
        thread.setDaemon(true);
        return thread;
    });

    private final ExecutorService controlExecutor = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "i2pdControl");
        thread.setDaemon(true);
        return thread;
    });

    private boolean bound;
    private boolean lastGracefulState;

    private final Runnable graceTicker = new Runnable() {
        @Override
        public void run() {
            if (isFinishing() || isDestroyed()) {
                return;
            }
            updateStatusText();
            DaemonWrapper current = daemon;
            if (current != null && current.isGracefulShutdownScheduled()) {
                mainHandler.postDelayed(this, 1000L);
            }
        }
    };

    private record ServiceStates(boolean httpProxy, boolean socksProxy, boolean bob,
                                 boolean sam, boolean i2cp) {
        private static final ServiceStates NONE = new ServiceStates(false, false, false, false, false);
    }

    private record StatusSnapshot(String errorText, DaemonWrapper.State state, String startResult,
                                  String summary, ServiceStates services,
                                  long graceRemainingMillis) {
    }

    private final DaemonWrapper.StateUpdateListener daemonStateUpdatedListener =
            (oldValue, newValue) -> mainHandler.post(() -> {
                updateGraceTicker();
                updateStatusText();
            });

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
            // The daemon is process-wide; binding only keeps the notification
            // service alive while the UI is visible.
        }

        @Override
        public void onServiceDisconnected(ComponentName className) {
            // Same-process service, so this only happens if the process dies.
        }
    };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        Log.d(TAG, "onCreate");
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        textView = findViewById(R.id.appStatusText);
        HTTPProxyState = findViewById(R.id.service_httpproxy_box);
        SOCKSProxyState = findViewById(R.id.service_socksproxy_box);
        BOBState = findViewById(R.id.service_bob_box);
        SAMState = findViewById(R.id.service_sam_box);
        I2CPState = findViewById(R.id.service_i2cp_box);

        DaemonWrapper current = getOrCreateDaemon(this);
        current.addStateChangeListener(daemonStateUpdatedListener);
        if (savedInstanceState == null
                && (current.getState() == DaemonWrapper.State.stopped
                || current.getState() == DaemonWrapper.State.startFailed)) {
            // A previous "stop" no longer kills the process, so restart the
            // daemon when the user launches the app again.
            current.startDaemon();
        }
        ForegroundService.init(current);

        try {
            ContextCompat.startForegroundService(this,
                    new Intent(this, ForegroundService.class));
        } catch (RuntimeException e) {
            Log.e(TAG, "unable to start foreground service", e);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    REQUEST_POST_NOTIFICATIONS);
        }

        doBindService();
        updateGraceTicker();
        updateStatusText();
        openBatteryOptimizationDialogIfNeeded();
    }

    @Override
    protected void onResume() {
        super.onResume();
        doBindService();
        updateGraceTicker();
        updateStatusText();
    }

    @Override
    protected void onDestroy() {
        Log.d(TAG, "onDestroy");
        DaemonWrapper current = daemon;
        if (current != null) {
            current.removeStateChangeListener(daemonStateUpdatedListener);
        }
        doUnbindService();
        mainHandler.removeCallbacksAndMessages(null);
        statusExecutor.shutdownNow();
        controlExecutor.shutdown();
        textView = null;
        super.onDestroy();
    }

    private static DaemonWrapper getOrCreateDaemon(Context context) {
        synchronized (DAEMON_LOCK) {
            if (daemon == null) {
                Context appContext = context.getApplicationContext();
                if (appContext == null) {
                    appContext = context;
                }
                ConnectivityManager connectivityManager =
                        (ConnectivityManager) appContext.getSystemService(
                                Context.CONNECTIVITY_SERVICE);
                daemon = new DaemonWrapper(appContext, appContext.getAssets(), connectivityManager);
            }
            return daemon;
        }
    }

    /**
     * Query native state off the UI thread and publish an immutable snapshot.
     */
    private void updateStatusText() {
        if (isFinishing() || isDestroyed()) {
            return;
        }
        final DaemonWrapper current = daemon;
        if (current == null) {
            return;
        }
        if (!statusQueryPending.compareAndSet(false, true)) {
            return;
        }
        try {
            statusExecutor.execute(() -> {
                StatusSnapshot result;
                try {
                    result = queryStatus(current);
                } catch (Throwable tr) {
                    result = new StatusSnapshot(stackTrace(tr), current.getState(),
                            current.getDaemonStartResult(), "", ServiceStates.NONE,
                            current.getGracefulStopRemainingMillis());
                } finally {
                    statusQueryPending.set(false);
                }
                final StatusSnapshot snapshot = result;
                mainHandler.post(() -> applyStatus(snapshot));
            });
        } catch (RejectedExecutionException e) {
            statusQueryPending.set(false);
        }
    }

    private static StatusSnapshot queryStatus(DaemonWrapper current) {
        DaemonWrapper.State state = current.getState();
        Throwable throwable = current.getLastThrowable();
        String errorText = throwable == null ? null : stackTrace(throwable);
        String startResult = current.getDaemonStartResult();
        boolean started = current.isStartedOkay();

        ServiceStates services = ServiceStates.NONE;
        String summary = "";
        long graceRemaining = current.getGracefulStopRemainingMillis();
        if (started && errorText == null) {
            try {
                services = new ServiceStates(
                        I2PD_JNI.getHTTPProxyState(),
                        I2PD_JNI.getSOCKSProxyState(),
                        I2PD_JNI.getBOBState(),
                        I2PD_JNI.getSAMState(),
                        I2PD_JNI.getI2CPState());
                summary = I2PD_JNI.getStatusSummary();
            } catch (Throwable tr) {
                errorText = stackTrace(tr);
            }
        }
        return new StatusSnapshot(errorText, state, startResult, summary, services, graceRemaining);
    }

    private void applyStatus(StatusSnapshot snapshot) {
        if (isFinishing() || isDestroyed() || textView == null) {
            return;
        }
        try {
            if (snapshot.errorText() != null) {
                textView.setText(snapshot.errorText());
                return;
            }

            DaemonWrapper.State state = snapshot.state() != null
                    ? snapshot.state() : DaemonWrapper.State.uninitialized;
            boolean started = state.isStartedOkay();
            if (started) {
                ServiceStates services = snapshot.services();
                HTTPProxyState.setChecked(services.httpProxy());
                SOCKSProxyState.setChecked(services.socksProxy());
                BOBState.setChecked(services.bob());
                SAMState.setChecked(services.sam());
                I2CPState.setChecked(services.i2cp());
            }

            String startResultStr = state == DaemonWrapper.State.startFailed
                    ? String.format(": %s", snapshot.startResult()) : "";
            String graceStr = state == DaemonWrapper.State.gracefulShutdownInProgress
                    ? String.format(": %s %s",
                    formatGraceTimeRemaining(snapshot.graceRemainingMillis()),
                    getText(R.string.remaining)) : "";
            String summary = started && snapshot.summary() != null
                    ? "\n" + snapshot.summary() : "";
            textView.setText(String.format("%s%s%s%s",
                    getText(state.getStatusStringResourceId()),
                    startResultStr, graceStr, summary));

            boolean graceful = state == DaemonWrapper.State.gracefulShutdownInProgress;
            if (graceful != lastGracefulState) {
                lastGracefulState = graceful;
                invalidateOptionsMenu();
            }
        } catch (Throwable tr) {
            Log.e(TAG, "error updating status", tr);
        }
    }

    private void updateGraceTicker() {
        mainHandler.removeCallbacks(graceTicker);
        DaemonWrapper current = daemon;
        if (current != null && current.isGracefulShutdownScheduled()) {
            mainHandler.postDelayed(graceTicker, 1000L);
        }
    }

    private static String formatGraceTimeRemaining(long remainingMillis) {
        long remainingSeconds = Math.round(Math.max(0L, remainingMillis) / 1000.0D);
        long remainingMinutes = remainingSeconds / 60;
        long remSec = remainingSeconds - remainingMinutes * 60;
        return remainingMinutes + ":" + (remSec / 10) + remSec % 10;
    }

    private static String stackTrace(Throwable tr) {
        StringWriter sw = new StringWriter(4096);
        try (PrintWriter pw = new PrintWriter(sw)) {
            tr.printStackTrace(pw);
        }
        return sw.toString();
    }

    private void doBindService() {
        if (bound) {
            return;
        }
        try {
            bound = bindService(new Intent(this, ForegroundService.class), connection,
                    Context.BIND_AUTO_CREATE);
        } catch (RuntimeException e) {
            Log.e(TAG, "unable to bind foreground service", e);
            bound = false;
        }
    }

    private void doUnbindService() {
        if (!bound) {
            return;
        }
        bound = false;
        try {
            unbindService(connection);
        } catch (IllegalArgumentException e) {
            Log.i(TAG, "service was not registered when unbinding");
        } catch (Throwable tr) {
            Log.e(TAG, "unable to unbind foreground service", tr);
        }
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.options_main, menu);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        MenuItem graceful = menu.findItem(R.id.action_graceful_stop);
        if (graceful != null) {
            DaemonWrapper current = daemon;
            boolean inProgress = current != null && current.isGracefulShutdownScheduled();
            graceful.setTitle(inProgress
                    ? R.string.action_cancel_graceful_stop : R.string.action_graceful_stop);
        }
        return super.onPrepareOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(@NonNull MenuItem item) {
        return switch (item.getItemId()) {
            case R.id.action_stop -> {
                i2pdStop();
                yield true;
            }
            case R.id.action_graceful_stop -> {
                toggleGracefulStop();
                yield true;
            }
            case R.id.action_battery_otimizations -> {
                onActionBatteryOptimizations();
                yield true;
            }
            case R.id.action_reload_tunnels_config -> {
                onReloadTunnelsConfig();
                yield true;
            }
            case R.id.action_settings -> {
                startActivity(new Intent(getApplicationContext(), SettingsActivity.class));
                yield true;
            }
            default -> super.onOptionsItemSelected(item);
        };
    }

    private void onActionBatteryOptimizations() {
        try {
            startActivity(new Intent(ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
        } catch (ActivityNotFoundException e) {
            Log.e(TAG, "battery optimisation settings activity missing", e);
            Toast.makeText(this,
                    R.string.os_version_does_not_support_battery_optimizations_show_os_dialog_api,
                    Toast.LENGTH_SHORT).show();
        }
    }

    private void onReloadTunnelsConfig() {
        Log.i(TAG, "reloading tunnels");
        DaemonWrapper current = daemon;
        if (current != null) {
            runControl(current::reloadTunnelsConfigs);
        }
        Toast.makeText(this, R.string.tunnels_reloading, Toast.LENGTH_SHORT).show();
    }

    private void runControl(Runnable task) {
        try {
            controlExecutor.execute(task);
        } catch (RejectedExecutionException e) {
            Log.w(TAG, "control task rejected after shutdown");
        }
    }

    private void i2pdStop() {
        mainHandler.removeCallbacks(graceTicker);
        Log.i(TAG, "stopping");
        if (textView != null) {
            textView.setText(R.string.stopping);
        }
        final DaemonWrapper current = daemon;
        if (current == null) {
            finishApplication();
            return;
        }
        runControl(() -> {
            try {
                current.stopDaemon();
            } catch (Throwable tr) {
                Log.e(TAG, "unable to stop daemon", tr);
            }
            mainHandler.post(this::finishApplication);
        });
    }

    private void toggleGracefulStop() {
        DaemonWrapper current = daemon;
        if (current == null) {
            return;
        }
        if (current.isGracefulShutdownScheduled()) {
            cancelGracefulStop(current);
        } else {
            beginGracefulStop(current);
        }
    }

    private void beginGracefulStop(DaemonWrapper current) {
        if (current.getState() == DaemonWrapper.State.stopped) {
            Toast.makeText(this, R.string.already_stopped, Toast.LENGTH_SHORT).show();
            return;
        }
        if (current.isGracefulShutdownScheduled()) {
            Toast.makeText(this, R.string.graceful_stop_is_already_in_progress,
                    Toast.LENGTH_SHORT).show();
            return;
        }
        Log.i(TAG, "graceful stopping");
        Toast.makeText(this, R.string.graceful_stop_is_in_progress, Toast.LENGTH_SHORT).show();
        runControl(() -> {
            try {
                current.beginGracefulShutdown(GRACEFUL_DELAY_MILLIS);
                if (current.getState() == DaemonWrapper.State.stopped) {
                    mainHandler.post(this::finishApplication);
                } else {
                    mainHandler.post(this::updateGraceTicker);
                }
            } catch (Throwable tr) {
                Log.e(TAG, "unable to begin graceful shutdown", tr);
            }
        });
    }

    private void cancelGracefulStop(DaemonWrapper current) {
        Log.i(TAG, "cancelling graceful stop");
        runControl(() -> {
            try {
                current.cancelGracefulShutdown();
                boolean started = current.isStartedOkay();
                if (!started) {
                    current.stopDaemon();
                }
                mainHandler.post(() -> {
                    if (isFinishing() || isDestroyed()) {
                        return;
                    }
                    if (started) {
                        Toast.makeText(this, R.string.shutdown_canceled, Toast.LENGTH_SHORT).show();
                    } else {
                        finishApplication();
                    }
                });
            } catch (Throwable tr) {
                Log.e(TAG, "unable to cancel graceful shutdown", tr);
            }
        });
    }

    @SuppressLint("BatteryLife")
    private void openBatteryOptimizationDialogIfNeeded() {
        boolean questionEnabled = getPreferences().getBoolean(
                BATTERY_OPTIMIZATION_PREFERENCE, true);
        Log.d(TAG, "battery optimization question enabled=" + questionEnabled);
        if (!isKnownIgnoringBatteryOptimizations() && questionEnabled) {
            var builder = new AlertDialog.Builder(this);
            builder.setTitle(R.string.battery_optimizations_enabled);
            builder.setMessage(R.string.battery_optimizations_enabled_dialog);
            builder.setPositiveButton(R.string.continue_str, (dialog, which) -> {
                try {
                    startActivity(new Intent(
                            Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                            Uri.parse(PACKAGE_URI_SCHEME + getPackageName())));
                } catch (ActivityNotFoundException e) {
                    Log.e(TAG, "battery optimisation request activity missing", e);
                    Toast.makeText(this,
                            R.string.device_does_not_support_disabling_battery_optimizations,
                            Toast.LENGTH_SHORT).show();
                }
            });
            builder.setOnDismissListener(
                    dialog -> setNeverAskForBatteryOptimizationsAgain());
            final AlertDialog dialog = builder.create();
            dialog.setCanceledOnTouchOutside(false);
            dialog.show();
        }
    }

    private void setNeverAskForBatteryOptimizationsAgain() {
        getPreferences().edit().putBoolean(BATTERY_OPTIMIZATION_PREFERENCE, false).apply();
    }

    private boolean isKnownIgnoringBatteryOptimizations() {
        final PowerManager powerManager = (PowerManager) getSystemService(POWER_SERVICE);
        if (powerManager == null) {
            Log.d(TAG, "battery optimization: POWER_SERVICE==null");
            return false;
        }
        boolean ignoring = powerManager.isIgnoringBatteryOptimizations(getPackageName());
        Log.d(TAG, "battery optimization: ignoring=" + ignoring);
        return ignoring;
    }

    private SharedPreferences getPreferences() {
        // Same file as PreferenceManager.getDefaultSharedPreferences(), without
        // depending on the deprecated android.preference helper.
        return getSharedPreferences(getPackageName() + "_preferences", MODE_PRIVATE);
    }

    private void finishApplication() {
        if (!finishing.compareAndSet(false, true)) {
            return;
        }
        try {
            finishAndRemoveTask();
        } catch (Throwable tr) {
            Log.e(TAG, "unable to finish task", tr);
            finish();
        }
        try {
            stopService(new Intent(this, ForegroundService.class));
        } catch (Throwable tr) {
            Log.e(TAG, "unable to stop foreground service", tr);
        }
    }
}
