/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import static android.app.PendingIntent.FLAG_IMMUTABLE;
import static android.app.PendingIntent.FLAG_UPDATE_CURRENT;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.ConnectivityManager;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Started and bound router service.
 *
 * <p>This class is the single owner of {@link DaemonWrapper}. It keeps the
 * foreground notification, the graceful-stop timer and all native calls. The
 * UI only binds with the application context through {@link RouterRepository};
 * it never sees {@code DaemonWrapper} or {@link I2PD_JNI}.</p>
 *
 * <p>Status and log snapshots are produced on one background executor by
 * polling {@code I2PD_JNI.getStatusSnapshot()} and
 * {@code I2PD_JNI.getRecentLogs(after, 200)}. Polling stops when the last
 * listener unregisters, while the daemon and notification keep running until
 * an explicit stop (or until the graceful-stop timer fires).</p>
 *
 * <p><b>Process model:</b> the service is intentionally same-process and not
 * exported. {@link LocalBinder} is therefore an in-process Binder, not AIDL.
 * The migration path for a future isolated router process is: split the daemon
 * into a separate {@code android:process} service, replace this binder with an
 * AIDL {@code IRouterService} exposing the same commands plus a Parcelable
 * snapshot/log record, and keep all native calls in the isolated process. That
 * migration needs its own security/lifecycle review; it is not part of this
 * stage.</p>
 */
public class RouterService extends Service {
    private static final String TAG = "RouterService";

    public static final String ACTION_START = "org.openi2pd.router.action.START";
    public static final String ACTION_STOP = "org.openi2pd.router.action.STOP";
    public static final String ACTION_RESTART = "org.openi2pd.router.action.RESTART";
    public static final String ACTION_GRACEFUL_STOP = "org.openi2pd.router.action.GRACEFUL_STOP";
    public static final String ACTION_CANCEL_GRACEFUL_STOP =
            "org.openi2pd.router.action.CANCEL_GRACEFUL_STOP";
    public static final String ACTION_RELOAD_TUNNELS =
            "org.openi2pd.router.action.RELOAD_TUNNELS";

    private static final String CHANNEL_ID = "i2pd_service";
    private static final int NOTIFICATION_ID = 1;
    private static final long POLL_INTERVAL_MS = 1000L;
    private static final int LOG_FETCH_LIMIT = 200;
    private static final int MAX_LOG_ENTRIES = 1000;
    private static final int MAX_ERROR_TEXT = 4096;
    private static final String PREFERENCES = "router_state";
    private static final String PREF_SHOULD_RUN = "should_run";

    /**
     * Coarse process-wide flag for components that cannot bind (Quick Settings
     * tile, deprecated connectivity receiver). It only mirrors the daemon
     * state; it is not a service singleton and holds no listener references.
     */
    private static volatile boolean routerRunning;

    public interface Listener {
        /** Called on the main thread with an immutable snapshot. */
        void onRouterSnapshot(RouterSnapshot snapshot);

        /** Called on the main thread when the retained log list changes. */
        void onRouterLogs(List<LogEntry> logs);
    }

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();
    private final AtomicBoolean pollInFlight = new AtomicBoolean(false);
    private final AtomicBoolean restartInProgress = new AtomicBoolean(false);
    private final IBinder binder = new LocalBinder();

    private ExecutorService worker;
    private DaemonWrapper daemon;
    private NotificationManager notificationManager;

    private volatile boolean destroyed;
    private volatile boolean foregroundShown;
    private volatile boolean statusSnapshotSupported = true;
    private volatile boolean recentLogsSupported = true;
    private volatile RouterSnapshot lastSnapshot;
    private volatile List<LogEntry> lastLogs = Collections.emptyList();
    /** Only the worker thread writes this; volatile for main-thread snapshots. */
    private volatile long lastLogSequence;
    private volatile DaemonWrapper.State lastNotificationState;

    private final Runnable pollTick = new Runnable() {
        @Override
        public void run() {
            if (destroyed || listeners.isEmpty()) {
                return;
            }
            if (!pollInFlight.compareAndSet(false, true)) {
                return;
            }
            try {
                worker.execute(() -> {
                    PollResult result;
                    try {
                        result = pollBlocking();
                    } catch (Throwable tr) {
                        result = new PollResult(
                                localSnapshotWithError(describe(tr)),
                                lastLogs,
                                false);
                    }
                    final PollResult published = result;
                    mainHandler.post(() -> {
                        pollInFlight.set(false);
                        if (destroyed) {
                            return;
                        }
                        applyPoll(published);
                        if (!listeners.isEmpty()) {
                            mainHandler.postDelayed(pollTick, POLL_INTERVAL_MS);
                        }
                    });
                });
            } catch (RejectedExecutionException e) {
                pollInFlight.set(false);
                Log.w(TAG, "poll rejected after shutdown");
            }
        }
    };

    private final DaemonWrapper.StateUpdateListener daemonStateListener =
            (oldState, newState) -> {
                routerRunning = newState.isStartedOkay();
                if (newState == DaemonWrapper.State.stopped && !destroyed
                        && !restartInProgress.get()) {
                    // Stop, graceful-stop expiration and a failed restart all
                    // converge here. An explicit restart keeps the flag set
                    // until the new start completes, so stopIfIdle() cannot
                    // tear the service down between the stop and start
                    // phases. During service destruction the flag is left
                    // intact so START_STICKY can restore a router that the
                    // system stopped for resource pressure.
                    setShouldRun(false);
                }
                mainHandler.post(() -> {
                    if (destroyed) {
                        return;
                    }
                    updateNotification();
                    if (!listeners.isEmpty()) {
                        pollNow();
                    }
                    stopIfIdle();
                });
            };

    /**
     * In-process binder. Do not turn this into an exported interface: clients
     * in another UID would gain router control. See the class documentation for
     * the future AIDL/isolated-process design.
     */
    public final class LocalBinder extends Binder {
        public RouterService getService() {
            return RouterService.this;
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        Context appContext = getApplicationContext();
        notificationManager =
                (NotificationManager) appContext.getSystemService(NOTIFICATION_SERVICE);
        createNotificationChannel();

        worker = Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "i2pd-router");
            thread.setDaemon(true);
            return thread;
        });

        ConnectivityManager connectivityManager =
                (ConnectivityManager) appContext.getSystemService(CONNECTIVITY_SERVICE);
        daemon = new DaemonWrapper(appContext, appContext.getAssets(),
                connectivityManager, false);
        daemon.addStateChangeListener(daemonStateListener);
        lastSnapshot = localSnapshot();
        routerRunning = false;

        if (readShouldRun()) {
            // Sticky service restart or a fresh bind after process death. If
            // the process is in a state where a foreground service may not be
            // started, leave the flag set: the next visible launch will retry.
            if (ensureForeground()) {
                enqueueControl(daemon::startDaemon);
            } else {
                Log.w(TAG, "foreground notification unavailable during restore");
                stopSelf();
            }
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();

        if (action == null) {
            // START_STICKY restart. Without a persisted "should run" flag
            // there is nothing to restore.
            if (!readShouldRun()) {
                stopSelfResult(startId);
                return START_NOT_STICKY;
            }
            if (!ensureForeground()) {
                stopSelfResult(startId);
                return START_NOT_STICKY;
            }
            enqueueControl(daemon::startDaemon);
            return START_STICKY;
        }

        switch (action) {
            case ACTION_START -> {
                if (!ensureForeground()) {
                    stopSelfResult(startId);
                    return START_NOT_STICKY;
                }
                setShouldRun(true);
                enqueueControl(daemon::startDaemon);
            }
            case ACTION_STOP -> {
                setShouldRun(false);
                // A startForegroundService() caller expects startForeground()
                // or stopSelf() within five seconds. The control task below is
                // allowed to take longer than that.
                ensureForeground();
                enqueueControl(daemon::stopDaemon);
            }
            case ACTION_RESTART -> {
                if (!ensureForeground()) {
                    stopSelfResult(startId);
                    return START_NOT_STICKY;
                }
                enqueueControl(this::performRestart);
            }
            case ACTION_GRACEFUL_STOP -> {
                if (!ensureForeground()) {
                    stopSelfResult(startId);
                    return START_NOT_STICKY;
                }
                setShouldRun(true);
                enqueueControl(() -> daemon.beginGracefulShutdown(
                        DaemonWrapper.GRACEFUL_STOP_DELAY_MILLIS));
            }
            case ACTION_CANCEL_GRACEFUL_STOP ->
                    enqueueControl(daemon::cancelGracefulShutdown);
            case ACTION_RELOAD_TUNNELS ->
                    enqueueControl(daemon::reloadTunnelsConfigs);
            default -> {
                Log.w(TAG, "ignoring unknown service action");
                if (!readShouldRun() && !daemon.isStartedOkay()) {
                    stopSelfResult(startId);
                }
                return START_NOT_STICKY;
            }
        }

        return readShouldRun() || daemon.isStartedOkay()
                ? START_STICKY : START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        // The repository unregisters explicitly; this is the safety net for a
        // client that died without doing so.
        listeners.clear();
        mainHandler.removeCallbacks(pollTick);
        return false;
    }

    @Override
    public void onDestroy() {
        destroyed = true;
        routerRunning = false;
        mainHandler.removeCallbacksAndMessages(null);
        if (daemon != null) {
            daemon.removeStateChangeListener(daemonStateListener);
        }
        // Native shutdown is never performed on the main thread. Stopping
        // unconditionally also cancels a start that is still in progress:
        // checking only isStartedOkay() would let that start thread finish
        // after the service and its notification are gone. The persisted
        // should-run flag is intentionally not cleared, so START_STICKY can
        // still restore the router after a system-initiated destruction.
        ExecutorService executor = worker;
        if (executor != null && daemon != null) {
            try {
                executor.execute(daemon::stopDaemon);
            } catch (RejectedExecutionException e) {
                Log.w(TAG, "shutdown task rejected", e);
            }
        }
        if (executor != null) {
            executor.shutdown();
        }
        stopForegroundCompat();
        super.onDestroy();
    }

    /**
     * Android 15+ calls this when a time-limited foreground service type
     * exceeds its budget. The router uses specialUse, which has no such
     * limit, but dataSync on API 29..33 and any future type change still get
     * a graceful, fail-closed shutdown instead of a crashed service.
     */
    @Override
    public void onTimeout(int startId, int fgsType) {
        Log.w(TAG, "foreground service timeout for type " + fgsType);
        setShouldRun(false);
        enqueueControl(daemon::stopDaemon);
    }

    // ---------------------------------------------------------------------
    // Binder API (same process, main-thread safe)
    // ---------------------------------------------------------------------

    public void registerListener(Listener listener) {
        if (listener == null) {
            return;
        }
        runOnMain(() -> {
            if (destroyed) {
                return;
            }
            if (!listeners.contains(listener)) {
                listeners.add(listener);
            }
            RouterSnapshot snapshot = lastSnapshot;
            if (snapshot == null || snapshot.state() != daemon.getState()) {
                snapshot = localSnapshot();
            }
            listener.onRouterSnapshot(snapshot);
            listener.onRouterLogs(lastLogs);
            pollNow();
        });
    }

    public void unregisterListener(Listener listener) {
        if (listener == null) {
            return;
        }
        runOnMain(() -> {
            listeners.remove(listener);
            if (listeners.isEmpty()) {
                mainHandler.removeCallbacks(pollTick);
            }
        });
    }

    public RouterSnapshot getLastSnapshot() {
        RouterSnapshot snapshot = lastSnapshot;
        return snapshot != null ? snapshot : localSnapshot();
    }

    public List<LogEntry> getLastLogs() {
        return lastLogs;
    }

    public void refresh() {
        runOnMain(this::pollNow);
    }

    public void startRouter() {
        runOnMain(() -> {
            if (!ensureForeground()) {
                Log.w(TAG, "unable to start foreground notification");
                return;
            }
            setShouldRun(true);
            enqueueControl(daemon::startDaemon);
        });
    }

    public void stopRouter() {
        runOnMain(() -> {
            setShouldRun(false);
            enqueueControl(daemon::stopDaemon);
        });
    }

    public void restartRouter() {
        runOnMain(() -> {
            if (!ensureForeground()) {
                Log.w(TAG, "unable to start foreground notification");
                return;
            }
            enqueueControl(this::performRestart);
        });
    }

    /**
     * Stop and start the daemon as one explicit user action. The run flag is
     * set before the stop phase, and the stopped transition is ignored while
     * restartInProgress is set, so stopIfIdle() cannot stop the service
     * between the two phases.
     */
    private void performRestart() {
        restartInProgress.set(true);
        setShouldRun(true);
        boolean restarted = false;
        try {
            restarted = daemon.restartDaemon();
        } catch (RuntimeException e) {
            Log.e(TAG, "unable to restart the router", e);
        } finally {
            restartInProgress.set(false);
        }
        if (!restarted) {
            if (daemon.getState() == DaemonWrapper.State.stopped) {
                // Do not leave a sticky service whose daemon is stopped and
                // has no start thread left to complete.
                setShouldRun(false);
            }
            Log.w(TAG, "restart did not complete");
        }
    }

    public void beginGracefulStop() {
        runOnMain(() -> {
            if (!ensureForeground()) {
                Log.w(TAG, "unable to start foreground notification");
                return;
            }
            setShouldRun(true);
            enqueueControl(() -> daemon.beginGracefulShutdown(
                    DaemonWrapper.GRACEFUL_STOP_DELAY_MILLIS));
        });
    }

    public void cancelGracefulStop() {
        runOnMain(() -> enqueueControl(daemon::cancelGracefulShutdown));
    }

    public void reloadTunnels() {
        runOnMain(() -> enqueueControl(daemon::reloadTunnelsConfigs));
    }

    public void clearLogs() {
        runOnMain(() -> enqueueControl(() -> {
            lastLogs = Collections.emptyList();
            mainHandler.post(() -> publishLogs(lastLogs));
        }));
    }

    // ---------------------------------------------------------------------
    // Commands used by receivers/tile without binding.
    // ---------------------------------------------------------------------

    public static boolean isRouterRunning() {
        return routerRunning;
    }

    public static boolean requestStart(Context context) {
        return sendCommand(context, ACTION_START, true);
    }

    public static boolean requestStop(Context context) {
        if (!routerRunning) {
            return false;
        }
        return sendCommand(context, ACTION_STOP, false);
    }

    /**
     * True when the process has never persisted a run state. A fresh install
     * auto-starts the router once; after an explicit stop, or after the
     * service has started at boot/from the tile, a later Activity launch must
     * not silently restart a deliberately stopped daemon.
     */
    static boolean hasPersistedRunState(Context context) {
        if (context == null) {
            return false;
        }
        Context appContext = context.getApplicationContext();
        if (appContext == null) {
            appContext = context;
        }
        return appContext.getSharedPreferences(PREFERENCES, MODE_PRIVATE)
                .contains(PREF_SHOULD_RUN);
    }

    private static boolean sendCommand(Context context, String action, boolean foreground) {
        if (context == null) {
            return false;
        }
        Context appContext = context.getApplicationContext();
        if (appContext == null) {
            appContext = context;
        }
        Intent intent = new Intent(appContext, RouterService.class).setAction(action);
        try {
            if (foreground) {
                ContextCompat.startForegroundService(appContext, intent);
            } else {
                appContext.startService(intent);
            }
            return true;
        } catch (RuntimeException e) {
            // Never fall back to startForegroundService() for commands that
            // do not enter the foreground themselves: that would create a
            // five-second startForeground() obligation for a no-op command.
            Log.w(TAG, "unable to deliver service command " + action, e);
            return false;
        }
    }

    // ---------------------------------------------------------------------
    // Polling and publication
    // ---------------------------------------------------------------------

    private record PollResult(RouterSnapshot snapshot, List<LogEntry> logs,
                              boolean logsChanged) {
    }

    private void pollNow() {
        if (destroyed || listeners.isEmpty()) {
            return;
        }
        mainHandler.removeCallbacks(pollTick);
        mainHandler.post(pollTick);
    }

    private PollResult pollBlocking() {
        DaemonWrapper local = daemon;
        if (local == null) {
            return new PollResult(
                    RouterSnapshot.empty(DaemonWrapper.State.uninitialized,
                            "", "", 0L),
                    lastLogs,
                    false);
        }

        DaemonWrapper.State state = local.getState();
        String errorText = describe(local.getLastThrowable());
        ServiceStates services = ServiceStates.NONE;
        String rawStatus = null;

        if (!state.isStartedOkay()) {
            // The native log ring may restart its sequence with a new daemon.
            // Re-read from zero on the next start; mergeLogs() de-duplicates
            // against the retained presentation list.
            lastLogSequence = 0L;
        }

        if (state.isStartedOkay()) {
            try {
                services = new ServiceStates(
                        I2PD_JNI.getHTTPProxyState(),
                        I2PD_JNI.getSOCKSProxyState(),
                        I2PD_JNI.getBOBState(),
                        I2PD_JNI.getSAMState(),
                        I2PD_JNI.getI2CPState());
            } catch (UnsatisfiedLinkError e) {
                errorText = mergeError(errorText, e);
            } catch (RuntimeException e) {
                errorText = mergeError(errorText, e);
            }

            if (statusSnapshotSupported) {
                try {
                    rawStatus = I2PD_JNI.getStatusSnapshot();
                } catch (UnsatisfiedLinkError e) {
                    // Older native builds do not export the method. Keep the
                    // service usable and avoid logging this every second.
                    statusSnapshotSupported = false;
                    Log.w(TAG, "status snapshot unavailable in native library", e);
                } catch (RuntimeException e) {
                    errorText = mergeError(errorText, e);
                }
            }
        }

        RouterSnapshot snapshot;
        long grace = local.getGracefulStopRemainingMillis();
        if (rawStatus == null || rawStatus.isEmpty()) {
            snapshot = RouterSnapshot.empty(state, errorText,
                    local.getDaemonStartResult(), grace);
        } else {
            snapshot = RouterStatusParser.parseStatus(rawStatus, state, services,
                    grace, errorText, local.getDaemonStartResult());
        }

        List<LogEntry> logs = lastLogs;
        boolean logsChanged = false;
        if (state.isStartedOkay() && recentLogsSupported) {
            try {
                String rawLogs = I2PD_JNI.getRecentLogs(lastLogSequence, LOG_FETCH_LIMIT);
                List<LogEntry> parsed =
                        RouterStatusParser.parseLogs(rawLogs, lastLogSequence);
                if (!parsed.isEmpty()) {
                    logs = mergeLogs(parsed);
                    logsChanged = true;
                }
            } catch (UnsatisfiedLinkError e) {
                recentLogsSupported = false;
                Log.w(TAG, "recent logs unavailable in native library", e);
            } catch (RuntimeException e) {
                Log.w(TAG, "unable to read recent logs", e);
            }
        }

        return new PollResult(snapshot, logs, logsChanged);
    }

    private List<LogEntry> mergeLogs(List<LogEntry> parsed) {
        List<LogEntry> current = new ArrayList<>(lastLogs);
        long maximum = current.isEmpty()
                ? 0L : current.get(current.size() - 1).sequence();

        if (!current.isEmpty() && parsed.get(0).sequence() < current.get(0).sequence()) {
            // The native ring restarted with lower sequence numbers. Drop the
            // old list instead of showing stale records forever.
            current.clear();
            maximum = 0L;
        }

        for (LogEntry entry : parsed) {
            if (entry.sequence() > maximum) {
                current.add(entry);
                maximum = entry.sequence();
            }
        }

        if (current.size() > MAX_LOG_ENTRIES) {
            current = new ArrayList<>(current.subList(
                    current.size() - MAX_LOG_ENTRIES, current.size()));
        }
        lastLogSequence = maximum;
        return Collections.unmodifiableList(current);
    }

    private void applyPoll(PollResult result) {
        lastSnapshot = result.snapshot();
        if (result.logsChanged()) {
            lastLogs = result.logs();
            publishLogs(lastLogs);
        }
        for (Listener listener : listeners) {
            try {
                listener.onRouterSnapshot(result.snapshot());
            } catch (RuntimeException e) {
                Log.w(TAG, "listener snapshot callback failed", e);
            }
        }
        updateNotification();
    }

    private void publishLogs(List<LogEntry> logs) {
        if (destroyed) {
            return;
        }
        lastLogs = logs;
        for (Listener listener : listeners) {
            try {
                listener.onRouterLogs(logs);
            } catch (RuntimeException e) {
                Log.w(TAG, "listener log callback failed", e);
            }
        }
    }

    private RouterSnapshot localSnapshot() {
        DaemonWrapper local = daemon;
        if (local == null) {
            return RouterSnapshot.empty(DaemonWrapper.State.uninitialized,
                    "", "", 0L);
        }
        return RouterSnapshot.empty(local.getState(),
                describe(local.getLastThrowable()),
                local.getDaemonStartResult(),
                local.getGracefulStopRemainingMillis());
    }

    private RouterSnapshot localSnapshotWithError(String errorText) {
        DaemonWrapper local = daemon;
        if (local == null) {
            return RouterSnapshot.empty(DaemonWrapper.State.uninitialized,
                    errorText, "", 0L);
        }
        return RouterSnapshot.empty(local.getState(), errorText,
                local.getDaemonStartResult(),
                local.getGracefulStopRemainingMillis());
    }

    // ---------------------------------------------------------------------
    // Foreground notification
    // ---------------------------------------------------------------------

    private boolean ensureForeground() {
        if (foregroundShown) {
            updateNotification();
            return true;
        }
        try {
            Notification notification = buildNotification();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                // specialUse is the only unrestricted long-running type that
                // fits a router. dataSync is capped at six hours per day on
                // Android 15 and cannot be started from BOOT_COMPLETED.
                startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            } else {
                startForeground(NOTIFICATION_ID, notification);
            }
            foregroundShown = true;
            lastNotificationState = daemon.getState();
            return true;
        } catch (RuntimeException e) {
            // Includes ForegroundServiceStartNotAllowedException on newer
            // Android releases. The caller decides whether to stop the
            // service; the daemon must not run without its notification.
            Log.e(TAG, "unable to enter the foreground", e);
            return false;
        }
    }

    private void updateNotification() {
        if (!foregroundShown || destroyed || notificationManager == null) {
            return;
        }
        DaemonWrapper local = daemon;
        DaemonWrapper.State state = local == null
                ? DaemonWrapper.State.uninitialized : local.getState();
        if (state == lastNotificationState) {
            return;
        }
        try {
            notificationManager.notify(NOTIFICATION_ID, buildNotification());
            lastNotificationState = state;
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to update router notification", e);
        }
    }

    private Notification buildNotification() {
        DaemonWrapper local = daemon;
        DaemonWrapper.State state = local == null
                ? DaemonWrapper.State.uninitialized : local.getState();
        CharSequence text = getText(state.getStatusStringResourceId());

        NotificationCompat.Builder builder =
                new NotificationCompat.Builder(this, CHANNEL_ID)
                        .setOngoing(true)
                        .setSmallIcon(R.drawable.ic_logo_monochrome)
                        .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                        .setCategory(NotificationCompat.CATEGORY_SERVICE)
                        .setTicker(text)
                        .setWhen(System.currentTimeMillis())
                        .setContentTitle(getText(R.string.app_name))
                        .setContentText(text);

        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            PendingIntent contentIntent = PendingIntent.getActivity(this, 0, launch,
                    FLAG_IMMUTABLE | FLAG_UPDATE_CURRENT);
            builder.setContentIntent(contentIntent);
        }
        return builder.build();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O || notificationManager == null) {
            return;
        }
        try {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, getText(R.string.app_name),
                    NotificationManager.IMPORTANCE_LOW);
            channel.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
            channel.setShowBadge(false);
            notificationManager.createNotificationChannel(channel);
        } catch (RuntimeException e) {
            Log.e(TAG, "unable to create notification channel", e);
        }
    }

    private void stopForegroundCompat() {
        if (!foregroundShown) {
            return;
        }
        foregroundShown = false;
        lastNotificationState = null;
        if (notificationManager != null) {
            notificationManager.cancel(NOTIFICATION_ID);
        }
        try {
            stopForeground(STOP_FOREGROUND_REMOVE);
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to leave the foreground", e);
        }
    }

    // ---------------------------------------------------------------------
    // Lifecycle helpers
    // ---------------------------------------------------------------------

    private void enqueueControl(Runnable command) {
        ExecutorService executor = worker;
        if (executor == null) {
            return;
        }
        try {
            executor.execute(() -> {
                try {
                    command.run();
                } catch (Throwable tr) {
                    Log.e(TAG, "router command failed", tr);
                } finally {
                    mainHandler.post(this::onControlFinished);
                }
            });
        } catch (RejectedExecutionException e) {
            Log.w(TAG, "router command rejected after shutdown", e);
        }
    }

    private void onControlFinished() {
        if (destroyed) {
            return;
        }
        if (!listeners.isEmpty()) {
            pollNow();
        }
        stopIfIdle();
    }

    private void stopIfIdle() {
        if (destroyed) {
            return;
        }
        DaemonWrapper local = daemon;
        boolean stopped = local != null
                && local.getState() == DaemonWrapper.State.stopped;
        if (!readShouldRun() && stopped) {
            stopForegroundCompat();
            stopSelf();
        }
    }

    private void runOnMain(Runnable action) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            action.run();
        } else {
            mainHandler.post(action);
        }
    }

    private boolean readShouldRun() {
        return getSharedPreferences(PREFERENCES, MODE_PRIVATE)
                .getBoolean(PREF_SHOULD_RUN, false);
    }

    private void setShouldRun(boolean shouldRun) {
        getSharedPreferences(PREFERENCES, MODE_PRIVATE)
                .edit()
                .putBoolean(PREF_SHOULD_RUN, shouldRun)
                .apply();
    }

    private static String mergeError(String existing, Throwable tr) {
        String description = describe(tr);
        if (description.isEmpty()) {
            return existing;
        }
        if (existing == null || existing.isEmpty()) {
            return description;
        }
        if (existing.contains(description)) {
            return existing;
        }
        String combined = existing + "\n" + description;
        return combined.length() <= MAX_ERROR_TEXT
                ? combined : combined.substring(0, MAX_ERROR_TEXT);
    }

    private static String describe(Throwable tr) {
        if (tr == null) {
            return "";
        }
        StringWriter writer = new StringWriter(1024);
        try (PrintWriter printWriter = new PrintWriter(writer)) {
            tr.printStackTrace(printWriter);
        }
        String text = writer.toString();
        return text.length() <= MAX_ERROR_TEXT ? text : text.substring(0, MAX_ERROR_TEXT);
    }
}
