/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: RouterService.
 *
 * Started and bound foreground service that is the sole owner of {@link
 * DaemonWrapper} and the native control boundary. It serialises
 * stop/reload/reseed/log-level work on a worker executor, polls bounded
 * status and log snapshots on a background thread, holds the Wi-Fi
 * multicast lock for SSDP and schedules the graceful-stop deadline on
 * the main handler.
 *
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
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.net.ConnectivityManager;
import android.net.wifi.WifiManager;
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
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.UnaryOperator;

/**
 * Started and bound router service.
 *
 * <p>This class is the single owner of {@link DaemonWrapper}. It keeps
 * the foreground notification, the service-owned graceful-stop deadline
 * and the native control boundary. Start runs on the wrapper's
 * dedicated start thread; stop/status commands are posted to the
 * service worker. The UI only binds with the application context
 * through {@link RouterRepository}; it never sees {@code DaemonWrapper}
 * or {@link I2PD_JNI}.</p>
 *
 * <p>Status and log snapshots are produced on one background executor
 * by polling {@code I2PD_JNI.getStatusSnapshot()} and {@code
 * I2PD_JNI.getRecentLogs(after, 200)}. Polling stops when the last
 * listener unregisters, while the daemon and notification keep running
 * until an explicit stop (or until the scheduled graceful-stop deadline
 * enqueues the native stop on the worker).</p>
 *
 * <p><b>Run-state ownership:</b> the durable intent lives in {@link
 * RouterRunState} inside the {@code router_state} preference file
 * ({@code run_phase}, {@code graceful_deadline_epoch_ms}, {@code
 * run_generation}, plus the legacy {@code should_run} mirror). The
 * graceful deadline is therefore wall-clock data, not process memory:
 * if the process dies during a drain, the next owner either finishes
 * the stop (the deadline passed) or re-arms the remaining window
 * instead of resurrecting a router that was already stopping. {@code
 * Stopped} never starts the daemon.</p>
 *
 * <p><b>Task removal:</b> the manifest declares {@code
 * android:stopWithTask="false"}, so {@link #onTaskRemoved(Intent)} is a
 * UI event and must not stop the daemon or touch the run state; {@link
 * #onDestroy()} removes messages already queued on the main handler,
 * including SAF deadlines. An in-flight provider operation may still
 * complete and post after that removal; destroying the service is not a
 * queue drain.</p>
 *
 * <p><b>Process model:</b> the service is intentionally same-process
 * and not exported. {@link LocalBinder} is therefore an in-process
 * Binder, not AIDL. The migration path for a future isolated router
 * process is: split the daemon into a separate {@code android:process}
 * service, replace this binder with an AIDL {@code IRouterService}
 * exposing the same commands plus a Parcelable snapshot/log record, and
 * keep all native calls in the isolated process. That migration needs
 * its own security/lifecycle review; it is not part of this stage.</p>
 */
public class RouterService extends Service {
    private static final String TAG = "RouterService";

    public static final String ACTION_START = "org.openi2pd.router.action.START";
    public static final String ACTION_STOP = "org.openi2pd.router.action.STOP";
    public static final String ACTION_RESTART = "org.openi2pd.router.action.RESTART";
    public static final String ACTION_GRACEFUL_STOP = "org.openi2pd.router.action.GRACEFUL_STOP";
    public static final String ACTION_CANCEL_GRACEFUL_STOP =
            "org.openi2pd.router.action.CANCEL_GRACEFUL_STOP";
    public static final String ACTION_RELOAD_TUNNELS = "org.openi2pd.router.action.RELOAD_TUNNELS";

    /**
     * Typed reseed command for callers that cannot bind (the result is
     * not observable through an intent; the binder method reports it).
     * The worker issues at most one native request and the native path
     * is single-flight.
     */
    public static final String ACTION_RESEED = "org.openi2pd.router.action.RESEED";

    /**
     * Typed runtime log-level command for callers that cannot bind. The
     * extra is validated against {@link
     * DaemonWrapper#isRuntimeLogLevel}; an intent with a missing or
     * unknown value is ignored without touching native state.
     */
    public static final String ACTION_SET_RUNTIME_LOG_LEVEL =
            "org.openi2pd.router.action.SET_RUNTIME_LOG_LEVEL";

    /**
     * Bounded runtime log level extra for {@link
     * #ACTION_SET_RUNTIME_LOG_LEVEL}. Only the exact closed set
     * accepted by {@link DaemonWrapper#isRuntimeLogLevel} is forwarded;
     * an intent carrying anything else is ignored.
     */
    public static final String EXTRA_RUNTIME_LOG_LEVEL =
            "org.openi2pd.router.extra.RUNTIME_LOG_LEVEL";

    private static final String CHANNEL_ID = "i2pd_service";
    private static final int NOTIFICATION_ID = 1;
    private static final long POLL_INTERVAL_MS = 1000L;
    private static final int LOG_FETCH_LIMIT = 200;
    private static final int MAX_LOG_ENTRIES = 1000;
    private static final int MAX_ERROR_TEXT = 4096;
    /**
     * Bounded diagnostics counter for task-removal events; never a
     * run-state write.
     */
    private static final int MAX_TASK_REMOVED_EVENTS = 20;
    private static final String PREFERENCES = "router_state";
    /**
     * Distinct published/logged state used when a restore could not
     * reach the foreground. It is prefixed to the snapshot error text
     * until the single bounded retry runs, so the UI never shows a
     * healthy router that does not exist.
     */
    private static final String RESTORE_DEFERRED = "restoreDeferred";
    private static final String RESTORE_DEFERRED_NOTE = RESTORE_DEFERRED
            + ": the persisted run state could not be restored yet; the next bind retries once";
    /**
     * Coarse process-wide flag for components that cannot bind (Quick
     * Settings tile, deprecated connectivity receiver). It only mirrors
     * the daemon state; it is not a service singleton and holds no
     * listener references.
     */
    private static volatile boolean routerRunning;
    private long ownerEpoch;

    public interface Listener {
        /** Called on the main thread with an immutable snapshot. */
        void onRouterSnapshot(RouterSnapshot snapshot);

        /**
         * Called on the main thread when the retained log list changes.
         */
        void onRouterLogs(List<LogEntry> logs);
    }

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();
    private final AtomicBoolean pollInFlight = new AtomicBoolean(false);
    private final AtomicBoolean torrentInFlight = new AtomicBoolean(false);
    /**
     * Bounds concurrent reseed requests; the native bootstrap is
     * single-flight too.
     */
    private final AtomicBoolean reseedInFlight = new AtomicBoolean(false);
    private volatile Runnable cancelTorrentImport;
    private final AtomicBoolean restartInProgress = new AtomicBoolean(false);
    private final IBinder binder = new LocalBinder();

    private final ExecutorService safWorker = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "openi2pd-saf");
        thread.setDaemon(true);
        return thread;
    });
    private Executor worker;
    private DaemonWrapper daemon;
    private NotificationManager notificationManager;
    /**
     * Android filters Wi-Fi multicast unless a MulticastLock is held.
     * UPnP SSDP discovery needs it while the router is running; the
     * lock is reference-counted explicitly and released on every
     * terminal state.
     */
    private WifiManager.MulticastLock multicastLock;

    private volatile boolean destroyed;
    private volatile boolean foregroundShown;
    private volatile boolean statusSnapshotSupported = true;
    private volatile boolean recentLogsSupported = true;
    private volatile RouterSnapshot lastSnapshot;
    private volatile List<LogEntry> lastLogs = Collections.emptyList();
    /**
     * Only the worker thread writes this; volatile for main-thread
     * snapshots.
     */
    private volatile long lastLogSequence;
    /**
     * Bumped by clearLogs(); an older poll must not republish the
     * cleared list.
     */
    private volatile long logsEpoch;
    private volatile DaemonWrapper.State lastNotificationState;
    /**
     * Serialises the read-modify-write of the persisted run state so a
     * generation increment cannot be lost between a main-thread command
     * and the daemon state listener.
     */
    private final Object runStateLock = new Object();
    /**
     * One-shot marker set when a restore could not enter the
     * foreground. The next bind consumes it exactly once, so no
     * unbounded retry loop exists.
     */
    private volatile boolean restoreRetryPending;
    /**
     * Remaining drain delay that must be re-armed once a restored
     * daemon reports {@code startedOkay}; {@code -1} when no resume is
     * pending. The native deadline lives only in {@link DaemonWrapper},
     * so a fresh process has to re-arm it after the asynchronous start
     * completes.
     */
    private volatile long pendingDrainResumeMillis = -1L;
    /**
     * Main-thread diagnostics counter; bounded so a swipe loop cannot
     * spam the log.
     */
    private int taskRemovedEvents;

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
                                localSnapshotWithError(describe(tr)), lastLogs, false, logsEpoch);
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
                Log.w(TAG, "poll rejected after service owner retired");
            }
        }
    };

    /**
     * The graceful-stop deadline is scheduled on the service main
     * handler and only enqueues the native stop on the single-thread
     * worker. This replaces the legacy timer thread and keeps every
     * JNI-visible transition on the service command path.
     *
     * <p>The terminal intent is persisted before the native stop, so a
     * process death between this callback and the native call cannot
     * resurrect the router: the next owner sees an expired or stopped
     * run state.</p>
     */
    private final Runnable gracefulStopExpired = () -> {
        if (destroyed) {
            return;
        }
        enqueueControl(() -> {
            DaemonWrapper local = daemon;
            if (local == null) {
                return;
            }
            long now = wallClockNow();
            boolean drainExpired = readRunState().isDrainExpired(now);
            if (!local.isGracefulShutdownScheduled() && !drainExpired) {
                // A cancel, an explicit start or a restart removed the
                // drain.
                return;
            }
            persistStopped();
            local.stopDaemon();
        });
    };

    private final DaemonWrapper.StateUpdateListener daemonStateListener =
            this::onDaemonStateChanged;

    private void onDaemonStateChanged(DaemonWrapper.State oldState, DaemonWrapper.State newState) {
        ServiceOwnerEpoch.runIfCurrent(ownerEpoch, () -> {
            if (destroyed) {
                return;
            }
            routerRunning = newState.isStartedOkay();
            if (newState == DaemonWrapper.State.stopped) {
                mainHandler.removeCallbacks(gracefulStopExpired);
                pendingDrainResumeMillis = -1L;
                if (!restartInProgress.get()) {
                    releaseMulticastLock();
                }
            } else if (newState == DaemonWrapper.State.startFailed) {
                releaseMulticastLock();
                if (pendingDrainResumeMillis >= 0L) {
                    // A drain cannot be re-armed against a daemon that
                    // never started. The persisted Draining phase stays
                    // in place so the next owner decides once, from the
                    // wall-clock deadline, instead of retrying in a
                    // loop.
                    pendingDrainResumeMillis = -1L;
                    markRestoreDeferred("daemon start failed during a restored drain");
                }
            }
            if (newState == DaemonWrapper.State.startedOkay && pendingDrainResumeMillis >= 0L) {
                long resume = pendingDrainResumeMillis;
                pendingDrainResumeMillis = -1L;
                // The start is asynchronous, so the drain is re-armed
                // here rather than right after startDaemon(), where the
                // wrapper is still in "starting" and would stop the
                // router immediately.
                enqueueControl(() -> beginGracefulShutdownOnWorker(resume));
            }
            if (newState == DaemonWrapper.State.stopped && !destroyed && !restartInProgress.get()) {
                // Stop, graceful-stop expiration and a failed restart
                // all converge here. An explicit restart keeps the
                // intent set until the new start completes, so
                // stopIfIdle() cannot tear the service down between the
                // stop and start phases. During service destruction the
                // intent is left intact so START_STICKY can restore a
                // router that the system stopped for resource pressure.
                persistStopped();
            }
            mainHandler.post(() -> {
                if (destroyed || !ServiceOwnerEpoch.isCurrent(ownerEpoch)) {
                    return;
                }
                updateNotification();
                if (!listeners.isEmpty()) {
                    pollNow();
                }
                stopIfIdle();
            });
        });
    }

    /**
     * In-process binder. Do not turn this into an exported interface:
     * clients in another UID would gain router control. See the class
     * documentation for the future AIDL/isolated-process design.
     */
    public final class LocalBinder extends Binder {
        public RouterService getService() {
            return RouterService.this;
        }
    }

    /**
     * Acquire the owner epoch and reconcile the durable run intent
     * once.
     *
     * <p>Runs on the main thread. It creates the owner-scoped view over
     * the single process FIFO (the gate that fences a stale service
     * owner), constructs {@link DaemonWrapper} with {@code
     * autoStart=false} and publishes the first local snapshot. A {@code
     * Stopped} intent does nothing, a running intent starts the daemon,
     * and a drain is either finished (its deadline passed while the
     * process was gone) or re-armed with the remaining wall-clock
     * delay. If the foreground notification cannot be installed the
     * intent is retained and reported as a deferred restore; a router
     * is never started without its notification.</p>
     */
    @Override
    public void onCreate() {
        super.onCreate();
        ownerEpoch = ServiceOwnerEpoch.acquire();
        Context appContext = getApplicationContext();
        TorrentSafStorage.initialize(appContext);
        notificationManager =
                (NotificationManager) appContext.getSystemService(NOTIFICATION_SERVICE);
        // The channel is created and verified inside
        // ensureForeground(); a failure there fails the command closed
        // instead of being swallowed here, where a missing channel
        // would silently disable the foreground service for the rest of
        // the process lifetime.

        // All owners share one process-local FIFO. The owner gate
        // fences stale submissions; NativeCallGuard still serializes
        // JNI entry itself.
        worker = ServiceOwnerEpoch.ownerExecutor(ownerEpoch);

        ConnectivityManager connectivityManager =
                (ConnectivityManager) appContext.getSystemService(CONNECTIVITY_SERVICE);
        // The control worker is also the serial dispatcher for
        // connectivity updates: the native call takes the shared
        // native-call lock, which a reseed can hold for minutes, and
        // the ConnectivityManager callback itself arrives on the main
        // thread.
        daemon = new DaemonWrapper(
                appContext, appContext.getAssets(), connectivityManager, false, worker);
        daemon.addStateChangeListener(daemonStateListener);
        lastSnapshot = localSnapshot();
        routerRunning = false;

        // Reconcile the durable run intent exactly once per owner
        // creation. Stopped never starts; a drain whose deadline passed
        // while the process was gone becomes the terminal stop it was
        // heading for; a drain still inside its window is re-armed with
        // the remaining wall-clock delay.
        RestorePlan plan = reconcilePersistedRunState();
        switch (plan.action()) {
        case STAY_STOPPED -> {
            // Nothing to restore. A binding UI can still start the
            // router.
        }
        case START_RUNNING -> {
            if (ensureForeground()) {
                enqueueControl(this::startDaemonWithMulticast);
            } else {
                // Sticky restart or a fresh bind after process death.
                // If the process is in a state where a foreground
                // service may not be started, keep the persisted intent
                // and publish the deferred restore so the next bind
                // retries once.
                Log.w(TAG, "foreground notification unavailable during restore");
                markRestoreDeferred("foreground unavailable");
                stopSelf();
            }
        }
        case RESUME_DRAIN -> {
            if (ensureForeground()) {
                startDrainRestore(plan.remainingDrainMs());
            } else {
                Log.w(TAG, "foreground notification unavailable while resuming a drain");
                markRestoreDeferred("foreground unavailable");
                stopSelf();
            }
        }
        }
    }

    /**
     * Dispatch one {@link RouterCommand} and decide the service
     * stickiness.
     *
     * <p>Called on the main thread. The sealed command is decoded by
     * {@link RouterCommand#fromAction(String, String)} and matched
     * exhaustively, so a new record fails compilation here instead of
     * being silently ignored. Commands that need the foreground enter
     * it first and fail only their own start id, which satisfies the
     * platform's five-second {@code startForeground()} obligation.
     * Native work is always enqueued on the control worker; no path
     * blocks the main thread. The result is {@code START_STICKY} only
     * while the persisted intent or a started daemon still needs the
     * service, so an explicit stop, an unknown action and a restore
     * that stays stopped are never sticky.</p>
     */
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        // The extra is read only for its own action, exactly as before:
        // an unrelated intent must never observe a wrong-typed extra
        // that this service does not consume.
        String runtimeLogLevel = intent != null && ACTION_SET_RUNTIME_LOG_LEVEL.equals(action)
                ? intent.getStringExtra(EXTRA_RUNTIME_LOG_LEVEL)
                : null;
        RouterCommand command = RouterCommand.fromAction(action, runtimeLogLevel);

        // Exhaustive over the sealed hierarchy: a new RouterCommand
        // record fails compilation here instead of being silently
        // ignored.
        switch (command) {
        case RouterCommand.Restore ignored -> {
            // START_STICKY restart. Reconcile the durable run state: a
            // stopped router is never restarted, a finished drain stays
            // stopped and a live drain is re-armed with its remaining
            // wall-clock delay.
            RestorePlan plan = reconcilePersistedRunState();
            if (plan.action() == RestoreAction.STAY_STOPPED) {
                stopSelfResult(startId);
                return START_NOT_STICKY;
            }
            if (!ensureForegroundOrFail(startId)) {
                markRestoreDeferred("foreground unavailable");
                return START_NOT_STICKY;
            }
            if (plan.action() == RestoreAction.RESUME_DRAIN) {
                startDrainRestore(plan.remainingDrainMs());
            } else {
                enqueueControl(this::startDaemonWithMulticast);
            }
            return START_STICKY;
        }
        case RouterCommand.Start ignored -> {
            if (!ensureForegroundOrFail(startId)) {
                return START_NOT_STICKY;
            }
            beginRunRequest();
            enqueueControl(this::startDaemonWithMulticast);
        }
        case RouterCommand.Stop ignored -> {
            persistStopped();
            clearGracefulDeadline();
            // A startForegroundService() caller expects
            // startForeground() or stopSelf() within five seconds. If
            // the notification cannot be installed, fail the command
            // instead of leaving the service in an invalid started
            // state.
            if (!ensureForegroundOrFail(startId)) {
                return START_NOT_STICKY;
            }
            enqueueControl(daemon::stopDaemon);
            // An explicit stop is never sticky. If the process dies
            // before the queued native shutdown completes, let the next
            // user action start a clean service rather than
            // resurrecting a stopping one.
            return START_NOT_STICKY;
        }
        case RouterCommand.Restart ignored -> {
            if (!ensureForegroundOrFail(startId)) {
                return START_NOT_STICKY;
            }
            // Persist intent before the worker starts the two-phase
            // restart, so a process death cannot leave a non-sticky
            // half-restarted service behind.
            beginRunRequest();
            enqueueControl(this::performRestart);
        }
        case RouterCommand.GracefulStop ignored -> {
            if (!ensureForegroundOrFail(startId)) {
                return START_NOT_STICKY;
            }
            beginGracefulStopRequest();
        }
        case RouterCommand.CancelGracefulStop ignored -> cancelGracefulStopRequest();
        case RouterCommand.ReloadTunnels ignored -> enqueueControl(daemon::reloadTunnelsConfigs);
        case RouterCommand.Reseed ignored ->
            enqueueControl(() -> {
                DaemonWrapper local = daemon;
                if (local != null) {
                    local.requestReseed();
                }
            });
        case RouterCommand.SetRuntimeLogLevel(var level) -> handleRuntimeLogLevel(level, startId);
        case RouterCommand.Unknown ignored -> {
            handleUnknownAction(startId);
            // An unrecognised action never makes the service sticky,
            // even if the running daemon would otherwise keep it alive.
            return START_NOT_STICKY;
        }
        }

        return readShouldRun() || daemon.isStartedOkay() ? START_STICKY : START_NOT_STICKY;
    }

    /**
     * Enter the foreground for the current command or fail the command.
     *
     * <p>A {@code startForegroundService()} caller expects {@code
     * startForeground()} or {@code stopSelf()} within five seconds, so
     * a missing notification fails only this start id; the caller then
     * returns {@link #START_NOT_STICKY}. The daemon never runs without
     * its notification.</p>
     */
    private boolean ensureForegroundOrFail(int startId) {
        if (ensureForeground()) {
            return true;
        }
        stopSelfResult(startId);
        return false;
    }

    /**
     * Forward one runtime log level to the control worker. A null or
     * unknown value is rejected before any native state is touched; the
     * command then keeps the service's current stickiness, like the
     * shared return used by the other non-terminal actions.
     */
    private void handleRuntimeLogLevel(String level, int startId) {
        if (DaemonWrapper.isRuntimeLogLevel(level)) {
            enqueueControl(() -> {
                DaemonWrapper local = daemon;
                if (local != null) {
                    local.applyRuntimeLogLevel(level);
                }
            });
            return;
        }
        Log.w(TAG, "ignoring runtime log level request with an invalid value");
        stopSelfIfIdle(startId);
    }

    /**
     * Ignore an unrecognised action and release the service when it is
     * idle.
     */
    private void handleUnknownAction(int startId) {
        Log.w(TAG, "ignoring unknown service action");
        stopSelfIfIdle(startId);
    }

    /**
     * Stop only this start id when neither the persisted run intent nor
     * a started daemon still needs the service. This is the
     * command-scoped counterpart of {@link #stopIfIdle()} and must not
     * tear down a restart that is still between its two phases.
     */
    private void stopSelfIfIdle(int startId) {
        if (!readShouldRun() && !daemon.isStartedOkay()) {
            stopSelfResult(startId);
        }
    }

    /**
     * Return the in-process binder. Clients must go through {@link
     * RouterRepository}; this is deliberately not an AIDL interface and
     * the service is not exported, so no other UID gains router
     * control.
     */
    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    /**
     * Safety net for a client that died without unregistering:
     * observation stops, while the daemon, the notification and the
     * persisted run state keep running. Returning {@code false} means
     * no rebind callback is wanted.
     */
    @Override
    public boolean onUnbind(Intent intent) {
        // The repository unregisters explicitly; this is the safety net
        // for a client that died without doing so.
        listeners.clear();
        mainHandler.removeCallbacks(pollTick);
        return false;
    }

    /**
     * The manifest contract is {@code android:stopWithTask="false"}, so
     * removing the task is a UI event and not a router command. The
     * foreground service, the daemon and the persisted run state are
     * left untouched; only the notification is republished and one
     * bounded diagnostics counter moves. An OEM that force-stops the
     * process still relies on START_STICKY plus the persisted run
     * state, never on this callback.
     */
    @Override
    public void onTaskRemoved(Intent rootIntent) {
        if (!destroyed) {
            if (taskRemovedEvents < MAX_TASK_REMOVED_EVENTS) {
                taskRemovedEvents++;
                Log.i(TAG,
                        "task removed; the router keeps running (" + taskRemovedEvents + "/"
                                + MAX_TASK_REMOVED_EVENTS + ")");
            }
            // Refresh the notification text without changing any state.
            publishNotification();
        }
        super.onTaskRemoved(rootIntent);
    }

    /**
     * Tear down this service owner without losing the run intent.
     *
     * <p>Runs on the main thread. Retirement and the mandatory native
     * stop are queued atomically, so work already accepted by this
     * owner drains ahead of the stop and no later submission from it
     * can run. The SAF lane is shut down, the pending import cancel
     * hook is invoked and the main-handler messages are purged: an
     * in-flight provider call may still post after this point, so
     * destruction is not a queue drain. The persisted run state is
     * deliberately kept so {@code START_STICKY} can restore the
     * router.</p>
     */
    @Override
    public void onDestroy() {
        ServiceOwnerEpoch.retireAndQueueStop(ownerEpoch,
                wasCurrentOwner
                -> {
                    destroyed = true;
                    if (wasCurrentOwner) {
                        routerRunning = false;
                    }
                },
                () -> {
                    if (daemon == null) {
                        return;
                    }
                    try {
                        daemon.stopDaemon();
                    } catch (Throwable tr) {
                        Log.w(TAG, "shutdown task failed", tr);
                    }
                });
        Runnable cancelImport = cancelTorrentImport;
        if (cancelImport != null)
            cancelImport.run();
        safWorker.shutdownNow();
        mainHandler.removeCallbacks(pollTick);
        mainHandler.removeCallbacks(gracefulStopExpired);
        pendingDrainResumeMillis = -1L;
        if (daemon != null) {
            daemon.removeStateChangeListener(daemonStateListener);
        }
        // Native shutdown is never performed on the main thread.
        // Stopping unconditionally also cancels a start that is still
        // in progress: checking only isStartedOkay() would let that
        // start thread finish after the service and its notification
        // are gone. The persisted run state is intentionally not
        // cleared, so START_STICKY can still restore the router after a
        // system-initiated destruction.
        releaseMulticastLock();
        stopForegroundCompat();
        // Destruction only: remove messages currently in this handler,
        // including SAF deadlines. An in-flight provider call may still
        // post a completion after this point. A plain stop keeps the
        // queued callbacks so work can finish and report its outcome.
        mainHandler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    /**
     * Android 15+ calls this when a time-limited foreground service
     * type exceeds its budget. The router uses specialUse, which has no
     * such limit, but dataSync on API 29..33 and any future type change
     * still get a graceful, fail-closed shutdown instead of a crashed
     * service.
     */
    @Override
    public void onTimeout(int startId, int fgsType) {
        Log.w(TAG, "foreground service timeout for type " + fgsType);
        persistStopped();
        clearGracefulDeadline();
        enqueueControl(daemon::stopDaemon);
        // The platform requires a timed-out dataSync service to stop
        // itself. onDestroy repeats the native stop request, which is
        // idempotent.
        stopSelf(startId);
    }

    // -----------------------------------------------------------------
    // Binder API (same process, main-thread safe)
    // -----------------------------------------------------------------

    /**
     * Register an observation listener and publish the current snapshot
     * and log list immediately, then start the one-second poll.
     *
     * <p>Safe to call from any thread: all work is posted to the main
     * thread, so the listener callbacks always arrive there.
     * Registering is also the single documented retry point for a
     * restore that could not reach the foreground on the first attempt,
     * which keeps a stuck restore bounded.</p>
     */
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
            // A bind is the documented retry point for a restore that
            // could not reach the foreground; the marker allows exactly
            // one attempt.
            retryDeferredRestore();
            pollNow();
        });
    }

    /**
     * Remove one listener; removing the last one stops status/log
     * polling.
     */
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

    /**
     * Request a start and persist the running intent before the worker
     * is asked. Main-thread safe; the wrapper rejects a start while one
     * is already running, so repeated taps cannot create a second
     * daemon.
     */
    public void startRouter() {
        runOnMain(() -> {
            if (!ensureForeground()) {
                Log.w(TAG, "unable to start foreground notification");
                return;
            }
            beginRunRequest();
            enqueueControl(this::startDaemonWithMulticast);
        });
    }

    /**
     * Terminal stop: persist {@code Stopped}, clear the graceful
     * deadline and enqueue the native stop on the control worker. The
     * caller is never blocked and the daemon is never stopped on the
     * main thread.
     */
    public void stopRouter() {
        runOnMain(() -> {
            persistStopped();
            clearGracefulDeadline();
            enqueueControl(daemon::stopDaemon);
        });
    }

    /** Restart through the wrapper's ordered stop-then-start path. */
    public void restartRouter() {
        runOnMain(() -> {
            if (!ensureForeground()) {
                Log.w(TAG, "unable to start foreground notification");
                return;
            }
            beginRunRequest();
            enqueueControl(this::performRestart);
        });
    }

    /**
     * Stop and start the daemon as one explicit user action. The run
     * intent is persisted by the caller before this reaches the worker,
     * and the stopped transition is ignored while restartInProgress is
     * set, so stopIfIdle() cannot stop the service between the two
     * phases.
     */
    private void performRestart() {
        restartInProgress.set(true);
        acquireMulticastLock();
        mainHandler.removeCallbacks(gracefulStopExpired);
        pendingDrainResumeMillis = -1L;
        boolean restarted = false;
        try {
            restarted = daemon.restartDaemon();
        } catch (RuntimeException e) {
            Log.e(TAG, "unable to restart the router", e);
        } finally {
            restartInProgress.set(false);
            if (!restarted && daemon.getState() == DaemonWrapper.State.stopped) {
                releaseMulticastLock();
            }
        }
        if (!restarted) {
            if (daemon.getState() == DaemonWrapper.State.stopped) {
                // Do not leave a sticky service whose daemon is stopped
                // and has no start thread left to complete.
                persistStopped();
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
            beginGracefulStopRequest();
        });
    }

    /**
     * Persist an explicit start intent and drop any graceful-stop
     * deadline.
     *
     * <p>Starting while a drain is in progress must also cancel the
     * native graceful shutdown; otherwise the deadline would stop a
     * router the operator just asked to keep running. The cancel is
     * enqueued before the caller's start command, so the control worker
     * keeps its ordering.</p>
     */
    private void beginRunRequest() {
        RouterRunState previous = readRunState();
        mainHandler.removeCallbacks(gracefulStopExpired);
        pendingDrainResumeMillis = -1L;
        persistRunState(previous.running(wallClockNow()));
        if (previous.isDraining()) {
            enqueueControl(() -> {
                DaemonWrapper local = daemon;
                if (local != null) {
                    local.cancelGracefulShutdown();
                }
            });
        }
    }

    /**
     * Persist the intent to stop when draining and schedule the
     * deadline.
     */
    private void beginGracefulStopRequest() {
        if (readRunState().phase() == RouterRunState.Phase.Stopped) {
            // No router is running: stay sticky only until the native
            // call decides to stop (or starts a drain); a failed call
            // ends in the stopped transition, which persists Stopped
            // again.
            persistRunning();
        }
        clearGracefulDeadline();
        beginGracefulStopCommand();
    }

    /**
     * Cancel a drain. A stacked or repeated cancel never resurrects a
     * stopped router, and the persisted phase only moves Draining to
     * Running.
     */
    private void cancelGracefulStopRequest() {
        clearGracefulDeadline();
        RouterRunState current = readRunState();
        if (current.isDraining()) {
            persistRunState(current.cancelGraceful());
        }
        enqueueControl(() -> {
            DaemonWrapper local = daemon;
            if (local != null) {
                local.cancelGracefulShutdown();
            }
        });
    }

    /**
     * Stop accepting transit tunnels and arm the service-owned
     * deadline.
     *
     * <p>The deadline is persisted before the delayed callback is
     * posted, so a process death during the drain is recoverable from
     * the wall clock. If the window already closed while this command
     * was queued, the terminal stop is persisted first and the native
     * stop is issued immediately; a finished drain is never turned back
     * into a running router.</p>
     */
    private void beginGracefulStopCommand() {
        long requestedDelay = DaemonWrapper.GRACEFUL_STOP_DELAY_MILLIS;
        enqueueControl(() -> beginGracefulShutdownOnWorker(requestedDelay));
    }

    /**
     * Worker-side counterpart of {@link #beginGracefulStopCommand()}.
     */
    private void beginGracefulShutdownOnWorker(long requestedDelayMs) {
        DaemonWrapper local = daemon;
        if (local == null) {
            return;
        }
        long delay = local.beginGracefulShutdown(Math.max(0L, requestedDelayMs));
        if (delay > 0L) {
            long now = wallClockNow();
            long deadline = RouterRunState.sanitizeDeadline(now + delay, now);
            if (deadline <= now) {
                // Corrupt or already-closed window: keep the terminal
                // stop.
                persistStopped();
                enqueueControl(this::stopDaemonOnWorker);
                return;
            }
            persistDraining(deadline);
            mainHandler.postDelayed(gracefulStopExpired, deadline - now);
            return;
        }
        // Either the router drained immediately or a previous deadline
        // had already expired. Persist the terminal intent before the
        // stop so a process death cannot resurrect a finished drain.
        persistStopped();
        if (local.isGracefulShutdownScheduled()) {
            enqueueControl(this::stopDaemonOnWorker);
        }
    }

    /**
     * Enqueue the idempotent native stop; the daemon may already be
     * gone.
     */
    private void stopDaemonOnWorker() {
        DaemonWrapper local = daemon;
        if (local != null) {
            local.stopDaemon();
        }
    }

    public void cancelGracefulStop() {
        runOnMain(this::cancelGracefulStopRequest);
    }

    public void reloadTunnels() {
        runOnMain(() -> enqueueControl(daemon::reloadTunnelsConfigs));
    }

    /**
     * Request a NetDb reseed without restarting the router.
     *
     * <p>The native call is single-flight and non-blocking: it starts
     * the owned NetDb job and returns the typed outcome, so the service
     * worker stays ordered with stop and reload. The callback is
     * invoked on the main thread with {@code started}, {@code
     * already-running}, {@code disabled}, {@code accepted}, {@code
     * busy}, {@code unavailable} or {@code failed}.</p>
     */
    public void requestReseed(java.util.function.Consumer<String> callback) {
        if (destroyed || !reseedInFlight.compareAndSet(false, true)) {
            mainHandler.post(() -> callback.accept("busy"));
            return;
        }
        Executor executor = worker;
        try {
            if (executor == null) {
                throw new RejectedExecutionException();
            }
            executor.execute(() -> {
                String outcome = "failed";
                try {
                    DaemonWrapper local = daemon;
                    outcome = local == null ? "unavailable" : local.requestReseed();
                } catch (RuntimeException | LinkageError error) {
                    outcome = "failed";
                } finally {
                    reseedInFlight.set(false);
                }
                String result = outcome;
                mainHandler.post(() -> callback.accept(result));
            });
        } catch (RejectedExecutionException error) {
            reseedInFlight.set(false);
            mainHandler.post(() -> callback.accept("unavailable"));
        }
    }

    /**
     * Bounded reseed status record on the control worker; empty when
     * stopped.
     */
    public void readReseedStatus(java.util.function.Consumer<String> callback) {
        runOnWorker(() -> {
            String status = "";
            try {
                DaemonWrapper local = daemon;
                status = local == null ? "" : local.getReseedStatus();
            } catch (RuntimeException | LinkageError error) {
                status = "";
            }
            String result = status;
            mainHandler.post(() -> callback.accept(result));
        }, () -> callback.accept(""));
    }

    /**
     * Configured tunnel rows on the control worker; empty when stopped.
     */
    public void readTunnelServices(java.util.function.Consumer<String> callback) {
        runOnWorker(() -> {
            String rows = "";
            try {
                DaemonWrapper local = daemon;
                rows = local == null ? "" : local.getTunnelServices();
            } catch (RuntimeException | LinkageError error) {
                rows = "";
            }
            String result = rows;
            mainHandler.post(() -> callback.accept(result));
        }, () -> callback.accept(""));
    }

    /**
     * Start or stop one configured tunnel; the callback receives the
     * outcome token.
     */
    public void tunnelCommand(
            String name, boolean start, java.util.function.Consumer<String> callback) {
        if (destroyed || !RuntimeAdminPolicy.isValidTunnelName(name)) {
            mainHandler.post(() -> callback.accept("invalid"));
            return;
        }
        runOnWorker(() -> {
            String outcome = "failed";
            try {
                DaemonWrapper local = daemon;
                outcome = local == null
                        ? "unavailable"
                        : (start ? local.startTunnel(name) : local.stopTunnel(name));
            } catch (RuntimeException | LinkageError error) {
                outcome = "failed";
            }
            String result = outcome;
            mainHandler.post(() -> callback.accept(result));
        }, () -> callback.accept("unavailable"));
    }

    /**
     * Add/replace one address-book name; the callback receives the
     * outcome token.
     */
    public void addAddressChecked(String name, String destination, boolean replace,
            java.util.function.Consumer<String> callback) {
        runOnWorker(() -> {
            String outcome = "failed";
            try {
                DaemonWrapper local = daemon;
                outcome = local == null ? "unavailable"
                                        : local.addAddressChecked(name, destination, replace);
            } catch (RuntimeException | LinkageError error) {
                outcome = "failed";
            }
            String result = outcome;
            mainHandler.post(() -> callback.accept(result));
        }, () -> callback.accept("unavailable"));
    }

    /** Address-book subscription sources, one bounded URL per line. */
    public void readSubscriptions(java.util.function.Consumer<String> callback) {
        runOnWorker(() -> {
            String rows = "";
            try {
                DaemonWrapper local = daemon;
                rows = local == null ? "" : local.getSubscriptions();
            } catch (RuntimeException | LinkageError error) {
                rows = "";
            }
            String result = rows;
            mainHandler.post(() -> callback.accept(result));
        }, () -> callback.accept(""));
    }

    /**
     * Add or remove one subscription source; the callback receives the
     * outcome token.
     */
    public void subscriptionCommand(
            String url, boolean add, java.util.function.Consumer<String> callback) {
        if (destroyed || !RuntimeAdminPolicy.isValidSubscriptionUrl(url)) {
            mainHandler.post(() -> callback.accept("invalid"));
            return;
        }
        runOnWorker(() -> {
            String outcome = "failed";
            try {
                DaemonWrapper local = daemon;
                outcome = local == null
                        ? "unavailable"
                        : (add ? local.addSubscription(url) : local.removeSubscription(url));
            } catch (RuntimeException | LinkageError error) {
                outcome = "failed";
            }
            String result = outcome;
            mainHandler.post(() -> callback.accept(result));
        }, () -> callback.accept("unavailable"));
    }

    /**
     * Ban or unban one base64 router hash; the callback receives the
     * outcome token.
     */
    public void banCommand(String hash, boolean ban, java.util.function.Consumer<String> callback) {
        if (destroyed || !RuntimeAdminPolicy.isValidRouterHash(hash)) {
            mainHandler.post(() -> callback.accept("invalid"));
            return;
        }
        runOnWorker(() -> {
            String outcome = "failed";
            try {
                DaemonWrapper local = daemon;
                outcome = local == null ? "unavailable"
                                        : (ban ? local.banRouter(hash) : local.unbanRouter(hash));
            } catch (RuntimeException | LinkageError error) {
                outcome = "failed";
            }
            String result = outcome;
            mainHandler.post(() -> callback.accept(result));
        }, () -> callback.accept("unavailable"));
    }

    /**
     * Run one bounded native command on the control worker or report
     * unavailability.
     */
    private void runOnWorker(Runnable work, Runnable unavailable) {
        Executor executor = worker;
        if (destroyed || executor == null) {
            mainHandler.post(unavailable);
            return;
        }
        try {
            executor.execute(work);
        } catch (RejectedExecutionException error) {
            mainHandler.post(unavailable);
        }
    }

    /**
     * Apply one runtime log level to the running logger on the control
     * worker. The caller persists any preference separately; this path
     * only changes the live logger and never writes {@code i2pd.conf}.
     */
    public void applyRuntimeLogLevel(String level, java.util.function.Consumer<Boolean> callback) {
        if (destroyed || !DaemonWrapper.isRuntimeLogLevel(level)) {
            mainHandler.post(() -> callback.accept(false));
            return;
        }
        Executor executor = worker;
        try {
            if (executor == null) {
                throw new RejectedExecutionException();
            }
            executor.execute(() -> {
                boolean applied = false;
                try {
                    DaemonWrapper local = daemon;
                    applied = local != null && local.applyRuntimeLogLevel(level);
                } catch (RuntimeException | LinkageError error) {
                    applied = false;
                }
                boolean result = applied;
                mainHandler.post(() -> callback.accept(result));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.post(() -> callback.accept(false));
        }
    }

    public void clearLogs() {
        runOnMain(() -> enqueueControl(() -> {
            ++logsEpoch;
            // Keep the sequence cursor: resetting it would replay every
            // retained native record on the next poll and undo the
            // clear.
            lastLogs = Collections.emptyList();
            mainHandler.post(() -> publishLogs(Collections.emptyList()));
        }));
    }

    // -----------------------------------------------------------------
    // Commands used by receivers/tile without binding.
    // -----------------------------------------------------------------

    /**
     * Coarse process-wide mirror for components that cannot bind (Quick
     * Settings tile, deprecated receivers). It reflects the last
     * published daemon state and is not a singleton handle: it carries
     * no listener, no native pointer and no run-state write.
     */
    public static boolean isRouterRunning() {
        return routerRunning;
    }

    /**
     * Deliver a start intent from a caller that cannot bind its own
     * service.
     */
    public static boolean requestStart(Context context) {
        return sendCommand(context, ACTION_START, true);
    }

    public static boolean requestStop(Context context) {
        // ACTION_STOP is idempotent. It must also be deliverable while
        // the router is starting or stuck in startFailed; gating on
        // startedOkay left Quick Settings unable to cancel that state.
        return sendCommand(context, ACTION_STOP, false);
    }

    /**
     * True when the persisted run intent is anything other than a
     * stopped router. A fresh install has no record at all and
     * auto-starts once; after an explicit stop, a finished drain, or
     * the service having started at boot/from the tile, a later
     * Activity launch must not silently restart a deliberately stopped
     * daemon.
     */
    public static boolean isStartRequested(Context context) {
        if (context == null)
            return false;
        Context appContext = context.getApplicationContext();
        if (appContext == null)
            appContext = context;
        return loadRunState(appContext).shouldRun();
    }

    static boolean hasPersistedRunState(Context context) {
        if (context == null) {
            return false;
        }
        Context appContext = context.getApplicationContext();
        if (appContext == null) {
            appContext = context;
        }
        return RouterRunState.hasPersisted(
                appContext.getSharedPreferences(PREFERENCES, MODE_PRIVATE));
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
            // Never fall back to startForegroundService() for commands
            // that do not enter the foreground themselves: that would
            // create a five-second startForeground() obligation for a
            // no-op command.
            Log.w(TAG, "unable to deliver service command " + action, e);
            return false;
        }
    }

    // -----------------------------------------------------------------
    // Polling and publication
    // -----------------------------------------------------------------

    private record PollResult(
            RouterSnapshot snapshot, List<LogEntry> logs, boolean logsChanged, long logsEpoch) {}

    private void pollNow() {
        if (destroyed || listeners.isEmpty()) {
            return;
        }
        mainHandler.removeCallbacks(pollTick);
        mainHandler.post(pollTick);
    }

    private PollResult pollBlocking() {
        final long epoch = logsEpoch;
        DaemonWrapper local = daemon;
        if (local == null) {
            return new PollResult(
                    RouterSnapshot.empty(DaemonWrapper.State.uninitialized, "", "", 0L), lastLogs,
                    false, logsEpoch);
        }

        DaemonWrapper.State state = local.getState();
        String errorText = describe(local.getLastThrowable());
        ServiceStates services = ServiceStates.NONE;
        String rawStatus = null;

        if (!state.isStartedOkay()) {
            // The native log ring may restart its sequence with a new
            // daemon. Re-read from zero on the next start; mergeLogs()
            // de-duplicates against the retained presentation list.
            lastLogSequence = 0L;
        }

        if (state.isStartedOkay()) {
            try {
                services = new ServiceStates(I2PD_JNI.getHTTPProxyState(),
                        I2PD_JNI.getSOCKSProxyState(), I2PD_JNI.getBOBState(),
                        I2PD_JNI.getSAMState(), I2PD_JNI.getI2CPState());
            } catch (UnsatisfiedLinkError e) {
                errorText = mergeError(errorText, e);
            } catch (RuntimeException e) {
                errorText = mergeError(errorText, e);
            }

            if (statusSnapshotSupported) {
                try {
                    rawStatus = I2PD_JNI.getStatusSnapshot();
                } catch (UnsatisfiedLinkError e) {
                    // Older native builds do not export the method.
                    // Keep the service usable and avoid logging this
                    // every second.
                    statusSnapshotSupported = false;
                    Log.w(TAG, "status snapshot unavailable in native library", e);
                } catch (RuntimeException e) {
                    errorText = mergeError(errorText, e);
                }
            }
        }

        RouterSnapshot snapshot;
        long grace = local.getGracefulStopRemainingMillis();
        errorText = deferredRestoreErrorText(errorText);
        if (rawStatus == null || rawStatus.isEmpty()) {
            snapshot = RouterSnapshot.empty(state, errorText, local.getDaemonStartResult(), grace);
        } else {
            snapshot = RouterStatusParser.parseStatus(
                    rawStatus, state, services, grace, errorText, local.getDaemonStartResult());
        }

        List<LogEntry> logs = lastLogs;
        boolean logsChanged = false;
        if (state.isStartedOkay() && recentLogsSupported) {
            try {
                String rawLogs = I2PD_JNI.getRecentLogs(lastLogSequence, LOG_FETCH_LIMIT);
                List<LogEntry> parsed = RouterStatusParser.parseLogs(rawLogs, lastLogSequence);
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

        return new PollResult(snapshot, logs, logsChanged, epoch);
    }

    private List<LogEntry> mergeLogs(List<LogEntry> parsed) {
        List<LogEntry> current = new ArrayList<>(lastLogs);
        long maximum = current.isEmpty() ? 0L : current.get(current.size() - 1).sequence();

        if (!current.isEmpty() && parsed.get(0).sequence() < current.get(0).sequence()) {
            // The native ring restarted with lower sequence numbers.
            // Drop the old list instead of showing stale records
            // forever.
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
            current = new ArrayList<>(
                    current.subList(current.size() - MAX_LOG_ENTRIES, current.size()));
        }
        lastLogSequence = maximum;
        return Collections.unmodifiableList(current);
    }

    private void applyPoll(PollResult result) {
        lastSnapshot = result.snapshot();
        if (result.logsChanged() && result.logsEpoch() == logsEpoch) {
            lastLogs = result.logs();
            publishLogs(result.logs());
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
            return RouterSnapshot.empty(
                    DaemonWrapper.State.uninitialized, deferredRestoreErrorText(""), "", 0L);
        }
        return RouterSnapshot.empty(local.getState(),
                deferredRestoreErrorText(describe(local.getLastThrowable())),
                local.getDaemonStartResult(), local.getGracefulStopRemainingMillis());
    }

    private RouterSnapshot localSnapshotWithError(String errorText) {
        String merged = deferredRestoreErrorText(errorText);
        DaemonWrapper local = daemon;
        if (local == null) {
            return RouterSnapshot.empty(DaemonWrapper.State.uninitialized, merged, "", 0L);
        }
        return RouterSnapshot.empty(local.getState(), merged, local.getDaemonStartResult(),
                local.getGracefulStopRemainingMillis());
    }

    // -----------------------------------------------------------------
    // Foreground notification
    // -----------------------------------------------------------------

    private boolean ensureForeground() {
        if (foregroundShown) {
            updateNotification();
            return true;
        }
        if (!ensureNotificationChannel()) {
            // A post to a missing channel is dropped, so the service
            // would run without its notification. Fail closed instead
            // of disabling the foreground service silently for the rest
            // of the process.
            Log.e(TAG, "notification channel unavailable; refusing to run the router");
            return false;
        }
        try {
            Notification notification = buildNotification();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                // specialUse is the only unrestricted long-running type
                // that fits a router. dataSync is capped at six hours
                // per day on Android 15 and cannot be started from
                // BOOT_COMPLETED.
                startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            } else {
                // The Android 10 floor guarantees the typed dataSync
                // overload.
                startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            }
            foregroundShown = true;
            lastNotificationState = daemon.getState();
            return true;
        } catch (RuntimeException e) {
            // Includes ForegroundServiceStartNotAllowedException on
            // newer Android releases. The caller decides whether to
            // stop the service; the daemon must not run without its
            // notification.
            Log.e(TAG, "unable to enter the foreground", e);
            return false;
        }
    }

    /**
     * Create the notification channel when it is missing and verify it
     * exists. The create is retried exactly once here rather than being
     * swallowed at service creation, where a transient failure would
     * leave the foreground service permanently unavailable.
     */
    private boolean ensureNotificationChannel() {
        if (notificationManager == null) {
            Log.e(TAG, "notification manager unavailable");
            return false;
        }
        if (notificationManager.getNotificationChannel(CHANNEL_ID) != null) {
            return true;
        }
        createNotificationChannel();
        return notificationManager.getNotificationChannel(CHANNEL_ID) != null;
    }

    private void updateNotification() {
        if (!foregroundShown || destroyed || notificationManager == null) {
            return;
        }
        DaemonWrapper local = daemon;
        DaemonWrapper.State state =
                local == null ? DaemonWrapper.State.uninitialized : local.getState();
        if (state == lastNotificationState) {
            return;
        }
        publishNotification();
    }

    /**
     * Republish the foreground notification unconditionally. Used when
     * the daemon state did not change (task removal) but the
     * notification must be kept current; the call is bounded and never
     * touches the run state.
     */
    private void publishNotification() {
        if (!foregroundShown || destroyed || notificationManager == null) {
            return;
        }
        try {
            notificationManager.notify(NOTIFICATION_ID, buildNotification());
            DaemonWrapper local = daemon;
            lastNotificationState =
                    local == null ? DaemonWrapper.State.uninitialized : local.getState();
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to publish router notification", e);
        }
    }

    private Notification buildNotification() {
        DaemonWrapper local = daemon;
        DaemonWrapper.State state =
                local == null ? DaemonWrapper.State.uninitialized : local.getState();
        CharSequence text = getText(state.getStatusStringResourceId());

        NotificationCompat.Builder builder =
                new NotificationCompat.Builder(this, CHANNEL_ID)
                        .setOngoing(true)
                        .setSmallIcon(R.drawable.ic_logo_monochrome)
                        .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                        .setCategory(NotificationCompat.CATEGORY_SERVICE)
                        .setForegroundServiceBehavior(
                                NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
                        .setOnlyAlertOnce(true)
                        .setTicker(text)
                        .setWhen(System.currentTimeMillis())
                        .setContentTitle(getText(R.string.app_name))
                        .setContentText(text)
                        .addAction(R.drawable.ic_stop, getText(R.string.action_stop),
                                PendingIntent.getService(this, 1,
                                        new Intent(this, RouterService.class)
                                                .setAction(ACTION_STOP),
                                        FLAG_IMMUTABLE | FLAG_UPDATE_CURRENT));
        // One drain action, never both: a running router offers
        // Graceful stop, an already-draining one offers Cancel. Keeping
        // the stale label would let the notification promise an action
        // the drain state rejects.
        boolean draining = state == DaemonWrapper.State.gracefulShutdownInProgress;
        builder.addAction(R.drawable.ic_graceful,
                getText(draining ? R.string.action_cancel_graceful_stop
                                 : R.string.action_graceful_stop),
                PendingIntent.getService(this, draining ? 3 : 2,
                        new Intent(this, RouterService.class)
                                .setAction(draining ? ACTION_CANCEL_GRACEFUL_STOP
                                                    : ACTION_GRACEFUL_STOP),
                        FLAG_IMMUTABLE | FLAG_UPDATE_CURRENT));

        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            PendingIntent contentIntent = PendingIntent.getActivity(
                    this, 0, launch, FLAG_IMMUTABLE | FLAG_UPDATE_CURRENT);
            builder.setContentIntent(contentIntent);
        }
        return builder.build();
    }

    private void createNotificationChannel() {
        if (notificationManager == null) {
            return;
        }
        try {
            NotificationChannel channel =
                    new NotificationChannel(CHANNEL_ID, getText(R.string.notification_channel_name),
                            NotificationManager.IMPORTANCE_LOW);
            channel.setDescription(getString(R.string.notification_channel_description));
            channel.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
            channel.setShowBadge(false);
            notificationManager.createNotificationChannel(channel);
        } catch (RuntimeException e) {
            // ensureNotificationChannel() verifies the result and fails
            // the foreground transition closed, so this is diagnostic
            // only.
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

    // -----------------------------------------------------------------
    // Lifecycle helpers
    // -----------------------------------------------------------------

    /**
     * One outstanding torrent operation bounds both the queue and JNI
     * work.
     */
    public void torrentCommand(String request, java.util.function.Consumer<String> callback) {
        if (request == null || request.length() > 4096
                || !torrentInFlight.compareAndSet(false, true)) {
            mainHandler.post(() -> callback.accept("I2PDTUI/1 ERR 4 12\ncontrol busy"));
            return;
        }
        Executor executor = worker;
        try {
            if (executor == null)
                throw new RejectedExecutionException();
            executor.execute(() -> {
                String response;
                try {
                    response = I2PD_JNI.torrentCommand(
                            request.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                } catch (LinkageError | RuntimeException error) {
                    response = "I2PDTUI/1 ERR 6 14\ncontrol failed";
                } finally {
                    torrentInFlight.set(false);
                }
                String result = response;
                mainHandler.post(() -> callback.accept(result));
            });
        } catch (RejectedExecutionException error) {
            torrentInFlight.set(false);
            mainHandler.post(() -> callback.accept("I2PDTUI/1 ERR 6 14\nrouter stopped"));
        }
    }

    /** Bounded address-book rows on the control worker. */
    public void readAddressBook(java.util.function.Consumer<String> callback) {
        Executor executor = worker;
        try {
            if (destroyed || executor == null)
                throw new RejectedExecutionException();
            executor.execute(() -> {
                String result = "";
                try {
                    DaemonWrapper local = daemon;
                    result = local == null ? "" : local.getAddressBook();
                } catch (RuntimeException | LinkageError error) {
                    result = "";
                }
                String response = result;
                mainHandler.post(() -> callback.accept(response));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.post(() -> callback.accept(""));
        }
    }

    /** Read deleted-host patterns on the control worker. */
    public void readBlockedAddresses(java.util.function.Consumer<String> callback) {
        Executor executor = worker;
        try {
            if (destroyed || executor == null)
                throw new RejectedExecutionException();
            executor.execute(() -> {
                String result = "";
                try {
                    DaemonWrapper local = daemon;
                    result = local == null ? "" : local.getBlockedAddresses();
                } catch (RuntimeException | LinkageError error) {
                    result = "";
                }
                String response = result;
                mainHandler.post(() -> callback.accept(response));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.post(() -> callback.accept(""));
        }
    }

    /**
     * Block a validated subscription name or wildcard on the control
     * worker.
     */
    public void blockAddress(String name, java.util.function.Consumer<Boolean> callback) {
        Executor executor = worker;
        try {
            if (destroyed || executor == null)
                throw new RejectedExecutionException();
            executor.execute(() -> {
                boolean result = false;
                try {
                    DaemonWrapper local = daemon;
                    result = local != null && local.blockAddress(name);
                } catch (RuntimeException | LinkageError error) {
                    result = false;
                }
                boolean blocked = result;
                mainHandler.post(() -> callback.accept(blocked));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.post(() -> callback.accept(false));
        }
    }

    /** Remove one exact deleted-host pattern on the control worker. */
    public void unblockAddress(String name, java.util.function.Consumer<Boolean> callback) {
        Executor executor = worker;
        try {
            if (destroyed || executor == null)
                throw new RejectedExecutionException();
            executor.execute(() -> {
                boolean result = false;
                try {
                    DaemonWrapper local = daemon;
                    result = local != null && local.unblockAddress(name);
                } catch (RuntimeException | LinkageError error) {
                    result = false;
                }
                boolean unblocked = result;
                mainHandler.post(() -> callback.accept(unblocked));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.post(() -> callback.accept(false));
        }
    }

    /** Remove one validated address-book name on the control worker. */
    public void removeAddress(String name, java.util.function.Consumer<Boolean> callback) {
        Executor executor = worker;
        try {
            if (destroyed || executor == null)
                throw new RejectedExecutionException();
            executor.execute(() -> {
                boolean result = false;
                try {
                    DaemonWrapper local = daemon;
                    result = local != null && local.removeAddress(name);
                } catch (RuntimeException | LinkageError error) {
                    result = false;
                }
                boolean removed = result;
                mainHandler.post(() -> callback.accept(removed));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.post(() -> callback.accept(false));
        }
    }

    /**
     * Persist or release a SAF payload tree off the main thread. Both
     * operations commit the grant map to disk and may talk to the
     * provider, so they must never run in an Activity or picker
     * callback. The SAF worker owns the authority; the callback is
     * delivered on the main thread and may be invoked after the calling
     * view is destroyed.
     */
    public void selectPayloadTree(String tunnel, String hash, android.net.Uri tree,
            java.util.function.Consumer<Boolean> callback) {
        runSafGrantOperation(
                ()
                        -> TorrentSafStorage.select(getApplicationContext(), tunnel, hash, tree),
                callback);
    }

    /** Release the persisted grant for one torrent payload. */
    public void detachPayload(
            String tunnel, String hash, java.util.function.Consumer<Boolean> callback) {
        runSafGrantOperation(
                () -> TorrentSafStorage.detach(getApplicationContext(), tunnel, hash), callback);
    }

    private void runSafGrantOperation(java.util.concurrent.Callable<Boolean> operation,
            java.util.function.Consumer<Boolean> callback) {
        if (destroyed || callback == null) {
            if (callback != null) {
                mainHandler.post(() -> callback.accept(false));
            }
            return;
        }
        try {
            safWorker.execute(() -> {
                boolean result = false;
                try {
                    result = Boolean.TRUE.equals(operation.call());
                } catch (Exception ignored) {
                    // A provider or preference failure is a typed false
                    // result.
                }
                boolean accepted = result;
                if (!destroyed) {
                    mainHandler.post(() -> callback.accept(accepted));
                }
            });
        } catch (RejectedExecutionException error) {
            mainHandler.post(() -> callback.accept(false));
        }
    }

    /**
     * Import bounded metainfo from a user-granted content URI on the
     * SAF worker. JNI receives the bytes, never the provider path. The
     * deadline requests cancellation; it cannot force a blocked
     * provider stream to return. The response is posted to the main
     * handler.
     */
    public void importTorrent(
            int tunnel, android.net.Uri document, java.util.function.Consumer<String> callback) {
        if (destroyed || tunnel < 0 || document == null || !"content".equals(document.getScheme())
                || !torrentInFlight.compareAndSet(false, true)) {
            mainHandler.post(() -> callback.accept("I2PDTUI/1 ERR 4 15\ninvalid request"));
            return;
        }
        var cancellation = new android.os.CancellationSignal();
        // The worker owns the descriptor through try-with-resources.
        // The canceller must never close a descriptor another thread is
        // reading: the released fd number can be reused and return
        // unrelated bytes.
        Runnable cancel = cancellation::cancel;
        cancelTorrentImport = cancel;
        mainHandler.postDelayed(cancel, 20_000L);
        // Import reads a provider and must not occupy the serialized
        // daemon control lane used by start/stop/reseed. The SAF worker
        // is the dedicated document lane; JNI still takes its own
        // native-call guard.
        Executor executor = safWorker != null ? safWorker : worker;
        try {
            if (executor == null)
                throw new RejectedExecutionException();
            executor.execute(() -> {
                String response;
                boolean releaseGrant = false;
                try {
                    if (destroyed)
                        throw new java.io.IOException("Service stopped");
                    android.content.ContentResolver resolver = getContentResolver();
                    boolean alreadyGranted = false;
                    for (android.content.UriPermission permission :
                            resolver.getPersistedUriPermissions()) {
                        if (document.equals(permission.getUri()) && permission.isReadPermission()) {
                            alreadyGranted = true;
                            break;
                        }
                    }
                    if (!alreadyGranted) {
                        resolver.takePersistableUriPermission(
                                document, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        releaseGrant = true;
                    }
                    try (android.content.res.AssetFileDescriptor opened =
                                    resolver.openAssetFileDescriptor(document, "r", cancellation)) {
                        if (opened == null)
                            throw new java.io.IOException("Document unavailable");
                        cancellation.throwIfCanceled();
                        if (opened.getLength() > TorrentControl.MAX_IMPORT_BYTES) {
                            throw new java.io.IOException("Torrent too large");
                        }
                        byte[] metadata;
                        try (java.io.InputStream input = opened.createInputStream()) {
                            metadata = TorrentDocumentReader.read(input);
                        }
                        cancellation.throwIfCanceled();
                        response = I2PD_JNI.importTorrent(tunnel, metadata);
                    }
                } catch (Throwable error) {
                    // Provider exception text may contain document
                    // names and credentials; do not log or propagate it
                    // to peers/UI.
                    response = "I2PDTUI/1 ERR 4 13\nimport failed";
                } finally {
                    mainHandler.removeCallbacks(cancel);
                    cancelTorrentImport = null;
                    if (releaseGrant) {
                        try {
                            getContentResolver().releasePersistableUriPermission(
                                    document, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        } catch (RuntimeException ignored) {
                        }
                    }
                    torrentInFlight.set(false);
                }
                String result = response;
                mainHandler.post(() -> callback.accept(result));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.removeCallbacks(cancel);
            cancelTorrentImport = null;
            torrentInFlight.set(false);
            mainHandler.post(() -> callback.accept("I2PDTUI/1 ERR 6 14\nrouter stopped"));
        }
    }

    /**
     * Hash a selected SAF source and produce metadata on the SAF
     * worker. Progress and the final byte array are posted to the main
     * handler; a null result denotes failure. The 30-minute deadline
     * requests cancellation but cannot force a provider that ignores
     * cancellation to return. The result is metadata only and does not
     * establish payload seeding authority.
     */
    public void createSafTorrent(android.net.Uri source, boolean tree, int pieceLength,
            String[] trackers, boolean privateTorrent, java.util.function.LongConsumer progress,
            java.util.function.Consumer<byte[]> callback) {
        if (destroyed || !torrentInFlight.compareAndSet(false, true)) {
            mainHandler.post(() -> callback.accept(null));
            return;
        }
        var cancel = new android.os.CancellationSignal();
        cancelTorrentImport = cancel::cancel;
        Runnable deadline = cancel::cancel;
        mainHandler.postDelayed(deadline, 30 * 60 * 1000L);
        try {
            if (destroyed)
                throw new RejectedExecutionException();
            safWorker.execute(() -> {
                byte[] metadata = null;
                boolean release = false;
                try {
                    var resolver = getContentResolver();
                    boolean granted = false;
                    for (var permission : resolver.getPersistedUriPermissions())
                        if (source.equals(permission.getUri()) && permission.isReadPermission())
                            granted = true;
                    if (!granted) {
                        resolver.takePersistableUriPermission(
                                source, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        release = true;
                    }
                    metadata = TorrentSafCreator.create(resolver, source, tree, cancel,
                            value
                            -> mainHandler.post(() -> progress.accept(value)),
                            pieceLength, trackers, privateTorrent);
                } catch (Throwable ignored) {
                    // Provider diagnostics may disclose document names
                    // or account identifiers.
                } finally {
                    if (release)
                        try {
                            getContentResolver().releasePersistableUriPermission(
                                    source, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        } catch (RuntimeException ignored) {
                        }
                    mainHandler.removeCallbacks(deadline);
                    cancelTorrentImport = null;
                    torrentInFlight.set(false);
                }
                byte[] result = metadata;
                mainHandler.post(() -> callback.accept(result));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.removeCallbacks(deadline);
            cancelTorrentImport = null;
            torrentInFlight.set(false);
            mainHandler.post(() -> callback.accept(null));
        }
    }

    /**
     * Request cancellation of the SAF import or creation currently in
     * flight. The hook only signals; the worker still owns the
     * descriptor and closes it, so cancelling never races a read on
     * another thread.
     */
    public void cancelSafCreation() {
        Runnable cancel = cancelTorrentImport;
        if (cancel != null)
            cancel.run();
    }

    /**
     * Export bounded metainfo through the broker's one-shot document
     * grant. Completion is posted to the main handler. On failure the
     * broker is asked to truncate the incomplete export, then the
     * handle is closed; the user-selected document is not deleted and
     * cleanup can itself fail.
     */
    public void exportTorrent(android.net.Uri target, byte[] metadata,
            java.util.function.Consumer<Boolean> callback) {
        if (target == null || !"content".equals(target.getScheme()) || metadata == null
                || metadata.length > TorrentControl.MAX_IMPORT_BYTES || destroyed
                || !torrentInFlight.compareAndSet(false, true)) {
            mainHandler.post(() -> callback.accept(false));
            return;
        }
        var canceled = new java.util.concurrent.atomic.AtomicBoolean();
        Runnable cancel = () -> {
            canceled.set(true);
            TorrentSafStorage.cancelPending();
        };
        cancelTorrentImport = cancel;
        mainHandler.postDelayed(cancel, 20_000L);
        try {
            if (destroyed)
                throw new RejectedExecutionException();
            safWorker.execute(() -> {
                boolean success = false;
                boolean releaseGrant = false;
                try {
                    boolean granted = false;
                    var resolver = getContentResolver();
                    for (var permission : resolver.getPersistedUriPermissions())
                        if (target.equals(permission.getUri()) && permission.isWritePermission())
                            granted = true;
                    if (!granted) {
                        resolver.takePersistableUriPermission(target,
                                Intent.FLAG_GRANT_READ_URI_PERMISSION
                                        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                        releaseGrant = true;
                    }
                    success =
                            TorrentSafStorage.export(target, metadata, canceled) && !canceled.get();
                } catch (Throwable ignored) {
                } finally {
                    if (releaseGrant)
                        try {
                            getContentResolver().releasePersistableUriPermission(target,
                                    Intent.FLAG_GRANT_READ_URI_PERMISSION
                                            | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                        } catch (RuntimeException ignored) {
                        }
                    mainHandler.removeCallbacks(cancel);
                    cancelTorrentImport = null;
                    torrentInFlight.set(false);
                }
                boolean result = success;
                mainHandler.post(() -> callback.accept(result));
            });
        } catch (RejectedExecutionException error) {
            mainHandler.removeCallbacks(cancel);
            cancelTorrentImport = null;
            torrentInFlight.set(false);
            mainHandler.post(() -> callback.accept(false));
        }
    }

    private void enqueueControl(Runnable command) {
        Executor executor = worker;
        if (executor == null || destroyed || !ServiceOwnerEpoch.isCurrent(ownerEpoch)) {
            return;
        }
        try {
            executor.execute(() -> {
                if (destroyed || !ServiceOwnerEpoch.isCurrent(ownerEpoch)) {
                    return;
                }
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

    /**
     * Start the router on the worker with the SSDP multicast lock held.
     */
    private void startDaemonWithMulticast() {
        DaemonWrapper local = daemon;
        if (local == null) {
            return;
        }
        acquireMulticastLock();
        try {
            local.startDaemon();
        } catch (RuntimeException e) {
            releaseMulticastLock();
            throw e;
        }
    }

    private synchronized void acquireMulticastLock() {
        if (destroyed || multicastLock != null) {
            return;
        }
        boolean upnpEnabled = true;
        try {
            upnpEnabled =
                    RouterConfigStore.get(getApplicationContext()).getBoolean("upnp.enabled", true);
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to read the UPnP setting; using the default", e);
        }
        if (!upnpEnabled) {
            return;
        }
        WifiManager wifiManager =
                (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
        if (wifiManager == null) {
            return;
        }
        WifiManager.MulticastLock lock = wifiManager.createMulticastLock("openi2pd-upnp");
        lock.setReferenceCounted(false);
        try {
            lock.acquire();
            multicastLock = lock;
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to acquire the SSDP multicast lock", e);
        }
    }

    private synchronized void releaseMulticastLock() {
        WifiManager.MulticastLock lock = multicastLock;
        multicastLock = null;
        if (lock == null || !lock.isHeld()) {
            return;
        }
        try {
            lock.release();
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to release the SSDP multicast lock", e);
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
        boolean stopped = local != null && local.getState() == DaemonWrapper.State.stopped;
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
        return readRunState().shouldRun();
    }

    // -----------------------------------------------------------------
    // Persisted run state
    // -----------------------------------------------------------------

    private static long wallClockNow() {
        return System.currentTimeMillis();
    }

    private SharedPreferences runStatePreferences() {
        return getSharedPreferences(PREFERENCES, MODE_PRIVATE);
    }

    private static RouterRunState loadRunState(Context appContext) {
        return RouterRunState.load(
                appContext.getSharedPreferences(PREFERENCES, MODE_PRIVATE), wallClockNow());
    }

    /**
     * Persisted run intent with the graceful deadline sanitised against
     * now.
     */
    private RouterRunState readRunState() {
        return RouterRunState.load(runStatePreferences(), wallClockNow());
    }

    /**
     * Apply one durable run-state transition.
     *
     * <p>The read-modify-write is serialised so the generation truly
     * increases even when a main-thread command races the daemon state
     * listener.</p>
     */
    private void updateRunState(UnaryOperator<RouterRunState> transition) {
        synchronized (runStateLock) {
            RouterRunState next = transition.apply(readRunState());
            SharedPreferences.Editor editor = runStatePreferences().edit();
            next.save(editor);
            editor.apply();
        }
    }

    /**
     * Persist an already computed state. The lock serializes this
     * write, but the earlier read occurred outside the lock and no
     * generation comparison rejects a stale state; callers requiring an
     * atomic transition must use {@link
     * #updateRunState(UnaryOperator)}.
     */
    private void persistRunState(RouterRunState state) {
        if (state == null) {
            return;
        }
        synchronized (runStateLock) {
            SharedPreferences.Editor editor = runStatePreferences().edit();
            state.save(editor);
            editor.apply();
        }
    }

    private void persistStopped() {
        updateRunState(RouterRunState::stop);
    }

    private void persistRunning() {
        updateRunState(state -> state.running(wallClockNow()));
    }

    private void persistDraining(long deadlineEpochMs) {
        updateRunState(state -> state.draining(deadlineEpochMs));
    }

    /** Cancel the scheduled expiry and any pending drain resume. */
    private void clearGracefulDeadline() {
        mainHandler.removeCallbacks(gracefulStopExpired);
        pendingDrainResumeMillis = -1L;
    }

    // -----------------------------------------------------------------
    // Restore reconciliation
    // -----------------------------------------------------------------

    private enum RestoreAction {
        /** No router is wanted; never start the daemon. */
        STAY_STOPPED,
        /** A running router was interrupted; start it normally. */
        START_RUNNING,
        /**
         * A graceful drain is still inside its window; start and re-arm
         * it.
         */
        RESUME_DRAIN
    }

    private record RestorePlan(RestoreAction action, long remainingDrainMs) {}

    /**
     * Reconcile the persisted run state with a freshly created service
     * owner.
     *
     * <p>The only side effect is persisting the terminal stop for a
     * drain whose deadline passed while the process was gone: that
     * drain was already stopping, so the next owner must not restart
     * the router.</p>
     */
    private RestorePlan reconcilePersistedRunState() {
        long now = wallClockNow();
        SharedPreferences preferences = runStatePreferences();
        long storedDeadline = RouterRunState.persistedDeadline(preferences);
        RouterRunState persisted = RouterRunState.load(preferences, now);
        if (persisted.isDraining() && !RouterRunState.isPlausibleDeadline(storedDeadline, now)) {
            Log.w(TAG, "persisted graceful-stop deadline was implausible; clamped or dropped");
        }
        if (persisted.phase() == RouterRunState.Phase.Stopped) {
            return new RestorePlan(RestoreAction.STAY_STOPPED, 0L);
        }
        if (persisted.isDrainExpired(now)) {
            persistRunState(persisted.stop());
            return new RestorePlan(RestoreAction.STAY_STOPPED, 0L);
        }
        if (persisted.isDraining()) {
            return new RestorePlan(RestoreAction.RESUME_DRAIN, persisted.remainingDrainMs(now));
        }
        return new RestorePlan(RestoreAction.START_RUNNING, 0L);
    }

    /**
     * Continue a persisted drain after a process death.
     *
     * <p>The daemon and its in-memory deadline are gone, so the
     * remaining wall-clock delay is posted here as a backstop and
     * re-armed against the native wrapper once the asynchronous start
     * reports {@code startedOkay}. Re-arming earlier would see the
     * wrapper in "starting" and stop the router before it ever ran.</p>
     */
    private void startDrainRestore(long remainingMs) {
        long delayMs = Math.max(0L, Math.min(remainingMs, RouterRunState.MAX_DRAIN_WINDOW_MILLIS));
        pendingDrainResumeMillis = delayMs;
        mainHandler.removeCallbacks(gracefulStopExpired);
        mainHandler.postDelayed(gracefulStopExpired, delayMs);
        enqueueControl(this::startDaemonWithMulticast);
    }

    /**
     * Publish and record a restore that could not start the daemon, and
     * leave a one-shot retry marker. The marker is consumed by the next
     * bind (or by the next successful foreground transition), which
     * retries exactly once, so the router is never left sticky with no
     * daemon and no signal.
     */
    private void markRestoreDeferred(String reason) {
        restoreRetryPending = true;
        Log.w(TAG, RESTORE_DEFERRED + ": " + reason);
        // Publish the distinct state immediately: a bound client must
        // not read a healthy snapshot for a router that does not exist.
        lastSnapshot = localSnapshot();
        if (!listeners.isEmpty()) {
            mainHandler.post(this::pollNow);
        }
    }

    /**
     * Consume the deferred-restore marker. The persisted phase decides
     * the action, exactly as in {@link #reconcilePersistedRunState()}:
     * a stopped router is never started and an expired drain only
     * persists its terminal stop.
     */
    private void retryDeferredRestore() {
        if (!restoreRetryPending || destroyed) {
            return;
        }
        restoreRetryPending = false;
        long now = wallClockNow();
        RouterRunState persisted = readRunState();
        if (persisted.phase() == RouterRunState.Phase.Stopped) {
            return;
        }
        if (persisted.isDrainExpired(now)) {
            persistRunState(persisted.stop());
            return;
        }
        DaemonWrapper local = daemon;
        if (local == null || local.isStartedOkay()
                || local.getState() == DaemonWrapper.State.starting) {
            return;
        }
        if (!ensureForeground()) {
            Log.w(TAG, "deferred restore retry could not enter the foreground");
            return;
        }
        Log.i(TAG, "retrying the deferred router restore once");
        if (persisted.isDraining()) {
            startDrainRestore(persisted.remainingDrainMs(now));
        } else {
            enqueueControl(this::startDaemonWithMulticast);
        }
    }

    /**
     * Extra snapshot text while a deferred restore has not been retried
     * yet.
     */
    private String deferredRestoreErrorText(String existing) {
        return restoreRetryPending ? mergeErrorText(existing, RESTORE_DEFERRED_NOTE) : existing;
    }

    private static String mergeError(String existing, Throwable tr) {
        return mergeErrorText(existing, describe(tr));
    }

    private static String mergeErrorText(String existing, String extra) {
        if (extra == null || extra.isEmpty()) {
            return existing;
        }
        if (existing == null || existing.isEmpty()) {
            return extra;
        }
        if (existing.contains(extra)) {
            return existing;
        }
        String combined = existing + "\n" + extra;
        return combined.length() <= MAX_ERROR_TEXT ? combined
                                                   : combined.substring(0, MAX_ERROR_TEXT);
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
