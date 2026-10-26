/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterRepository.
 *
 * Main-thread bridge between {@link RouterViewModel} and {@link RouterService}:
 * it binds with the application context, exposes immutable LiveData snapshots
 * and logs, and forwards commands as explicit service intents rather than
 * direct native calls. Listener registration follows UI visibility so polling
 * stops in the background, and closing the repository releases the binding.
 *
 */

package org.openi2pd.router;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * Presentation-facing bridge to {@link RouterService}.
 *
 * <p>Binds with the application context, exposes immutable LiveData and
 * forwards commands through the in-process binder. It never touches
 * {@link DaemonWrapper} or {@link I2PD_JNI}. A ViewModel owns one instance, so
 * an Activity recreation keeps the binding and the daemon without restarting
 * anything.</p>
 *
 * <p>Commands that must create/maintain the started foreground service are
 * delivered as explicit service intents (the same paths the Quick Settings
 * tile and boot receiver use). Observation uses the binder. When a future
 * isolated-process AIDL service replaces the local binder, this class is the
 * only UI-facing piece that needs a transport change.</p>
 */
public final class RouterRepository implements RouterService.Listener {
    private static final String TAG = "RouterRepository";

    private final Context appContext;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final MutableLiveData<RouterSnapshot> snapshot = new MutableLiveData<>();
    private final MutableLiveData<List<LogEntry>> logs =
            new MutableLiveData<>(Collections.emptyList());
    private final MutableLiveData<Boolean> serviceConnected = new MutableLiveData<>(false);

    private volatile RouterService service;
    private volatile List<LogEntry> allLogs = Collections.emptyList();
    private volatile String logFilterLevel;
    private boolean binding;
    private boolean bound;
    private boolean closed;
    /** UI visibility gates the service listener so polling stops in background. */
    private boolean uiVisible;
    private boolean listening;
    private RouterSnapshot lastSnapshot;

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            binding = false;
            bound = true;
            RouterService connected =
                    binder instanceof RouterService.LocalBinder local ? local.getService() : null;
            if (connected == null || closed) {
                if (closed) {
                    // close() may have run before this callback was delivered.
                    // Drop the callback's binding instead of retaining a
                    // service connection no owner will release.
                    try {
                        appContext.unbindService(connection);
                    } catch (RuntimeException e) {
                        Log.i(TAG, "service already unbound");
                    }
                    bound = false;
                    service = null;
                }
                return;
            }
            service = connected;
            serviceConnected.setValue(true);
            if (uiVisible) {
                connected.registerListener(RouterRepository.this);
                listening = true;
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            binding = false;
            bound = false;
            listening = false;
            service = null;
            serviceConnected.setValue(false);
        }
    };

    public RouterRepository(Context context) {
        Context app = context.getApplicationContext();
        this.appContext = app == null ? context : app;
    }

    /** Bind once; safe to call repeatedly and after a configuration change. */
    public void bind() {
        runOnMain(this::ensureBound);
    }

    public LiveData<RouterSnapshot> getSnapshot() {
        return snapshot;
    }

    public LiveData<List<LogEntry>> getLogs() {
        return logs;
    }

    public LiveData<Boolean> getServiceConnected() {
        return serviceConnected;
    }

    /**
     * Start or stop observation of the bound service. Keeping the binding while
     * the UI is not visible lets commands still use the binder, but stops the
     * service's JNI polling and the fragment render pipeline in background.
     */
    public void setUiVisible(boolean visible) {
        runOnMain(() -> {
            if (closed || uiVisible == visible) {
                return;
            }
            uiVisible = visible;
            RouterService local = service;
            if (local == null) {
                return;
            }
            if (visible && !listening) {
                local.registerListener(this);
                listening = true;
            } else if (!visible && listening) {
                local.unregisterListener(this);
                listening = false;
            }
        });
    }

    public void refresh() {
        RouterService local = service;
        if (local != null) {
            local.refresh();
        } else {
            bind();
        }
    }

    /**
     * Run one bounded torrent control request through the service.
     *
     * <p>{@code request} is the typed {@code I2PDTUI/1} line built by
     * {@link TorrentControl}; the service validates it and the native side
     * re-validates it again. The callback always runs on the main thread and
     * receives a protocol response, including the fixed
     * {@code I2PDTUI/1 ERR 6 14 router stopped} refusal when no service is
     * bound, so a caller never has to distinguish "no answer" from failure.</p>
     */
    public void torrentCommand(String request, java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept("I2PDTUI/1 ERR 6 14\nrouter stopped"));
            return;
        }
        local.torrentCommand(request, callback);
    }

    public void readAddressBook(java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(""));
            return;
        }
        local.readAddressBook(callback);
    }

    public void readBlockedAddresses(java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(""));
            return;
        }
        local.readBlockedAddresses(callback);
    }

    public void blockAddress(String name, java.util.function.Consumer<Boolean> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(false));
            return;
        }
        local.blockAddress(name, callback);
    }

    public void unblockAddress(String name, java.util.function.Consumer<Boolean> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(false));
            return;
        }
        local.unblockAddress(name, callback);
    }

    public void removeAddress(String name, java.util.function.Consumer<Boolean> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(false));
            return;
        }
        local.removeAddress(name, callback);
    }

    public void createSafTorrent(android.net.Uri source, boolean tree, int pieceLength,
            String[] trackers, boolean privateTorrent, java.util.function.LongConsumer progress,
            java.util.function.Consumer<byte[]> callback) {
        RouterService local = service;
        if (closed || local == null) {
            callback.accept(null);
            return;
        }
        local.createSafTorrent(
                source, tree, pieceLength, trackers, privateTorrent, progress, callback);
    }
    public void cancelSafCreation() {
        RouterService local = service;
        if (local != null)
            local.cancelSafCreation();
    }
    public void exportTorrent(android.net.Uri target, byte[] metadata,
            java.util.function.Consumer<Boolean> callback) {
        RouterService local = service;
        if (closed || local == null) {
            callback.accept(false);
            return;
        }
        local.exportTorrent(target, metadata, callback);
    }

    /**
     * Import bounded metainfo from a user-granted {@code content} URI. Only the
     * bytes cross into the engine; a provider path and display name are never
     * trusted or converted, and the result is a typed control response.
     */
    public void importTorrent(
            int tunnel, android.net.Uri document, java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept("I2PDTUI/1 ERR 6 14\nrouter stopped"));
            return;
        }
        local.importTorrent(tunnel, document, callback);
    }

    /** Persist a selected SAF payload tree on the service's SAF worker. */
    public void selectPayloadTree(String tunnel, String hash, android.net.Uri tree,
            java.util.function.Consumer<Boolean> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(false));
            return;
        }
        local.selectPayloadTree(tunnel, hash, tree, callback);
    }

    /** Release a persisted SAF payload grant on the service's SAF worker. */
    public void detachPayload(
            String tunnel, String hash, java.util.function.Consumer<Boolean> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(false));
            return;
        }
        local.detachPayload(tunnel, hash, callback);
    }

    /**
     * Start the router. Always goes through startForegroundService() so the
     * service is in the started (START_STICKY) state even if it is currently
     * only bound.
     */
    public void startRouter() {
        bind();
        sendCommand(RouterService.ACTION_START, true);
    }

    public void stopRouter() {
        RouterService local = service;
        if (local != null) {
            local.stopRouter();
        } else {
            sendCommand(RouterService.ACTION_STOP, false);
        }
    }

    public void restartRouter() {
        bind();
        sendCommand(RouterService.ACTION_RESTART, true);
    }

    public void beginGracefulStop() {
        bind();
        sendCommand(RouterService.ACTION_GRACEFUL_STOP, true);
    }

    public void cancelGracefulStop() {
        RouterService local = service;
        if (local != null) {
            local.cancelGracefulStop();
        } else {
            sendCommand(RouterService.ACTION_CANCEL_GRACEFUL_STOP, false);
        }
    }

    public void reloadTunnels() {
        RouterService local = service;
        if (local != null) {
            local.reloadTunnels();
        } else {
            sendCommand(RouterService.ACTION_RELOAD_TUNNELS, false);
        }
    }

    /**
     * Ask the running router to reseed. The service starts the non-blocking
     * native job on its worker and reports the typed outcome token on the main
     * thread. A missing binding is reported as {@code unavailable} rather than
     * an intent fallback because the result is part of the contract.
     */
    public void requestReseed(java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept("unavailable"));
            return;
        }
        local.requestReseed(callback);
    }

    /** Bounded reseed status record; empty when the router is not running. */
    public void readReseedStatus(java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(""));
            return;
        }
        local.readReseedStatus(callback);
    }

    /** Configured tunnel rows; empty when the router is not running. */
    public void readTunnelServices(java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(""));
            return;
        }
        local.readTunnelServices(callback);
    }

    public void tunnelCommand(
            String name, boolean start, java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept("unavailable"));
            return;
        }
        local.tunnelCommand(name, start, callback);
    }

    public void addAddressChecked(String name, String destination, boolean replace,
            java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept("unavailable"));
            return;
        }
        local.addAddressChecked(name, destination, replace, callback);
    }

    public void readSubscriptions(java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(""));
            return;
        }
        local.readSubscriptions(callback);
    }

    public void subscriptionCommand(
            String url, boolean add, java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept("unavailable"));
            return;
        }
        local.subscriptionCommand(url, add, callback);
    }

    public void banCommand(String hash, boolean ban, java.util.function.Consumer<String> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept("unavailable"));
            return;
        }
        local.banCommand(hash, ban, callback);
    }

    /**
     * Apply one runtime log level to the running logger. Persisting the value
     * is the caller's explicit, separate step.
     */
    public void applyRuntimeLogLevel(String level, java.util.function.Consumer<Boolean> callback) {
        RouterService local = service;
        if (closed || local == null) {
            mainHandler.post(() -> callback.accept(false));
            return;
        }
        local.applyRuntimeLogLevel(level, callback);
    }

    /** True only when no run state has ever been persisted by the service. */
    public boolean shouldStartOnFirstLaunch() {
        return !RouterService.hasPersistedRunState(appContext);
    }

    public void clearLogs() {
        RouterService local = service;
        if (local != null) {
            local.clearLogs();
        }
    }

    /**
     * Optional presentation filter. {@code null} or "all" shows every
     * retained record; "debug", "info", "warn" and "error" apply a minimum
     * severity. The service always retains the full bounded list.
     */
    public void setLogFilterLevel(String level) {
        String normalized = normalizeFilter(level);
        logFilterLevel = normalized;
        runOnMain(() -> logs.setValue(applyFilter(allLogs)));
    }

    /**
     * Release the service binding and stop observation. Idempotent and
     * main-thread safe; {@link RouterViewModel#onCleared()} calls it so a
     * finished UI never leaks a connection.
     */
    public void close() {
        runOnMain(() -> {
            if (closed) {
                return;
            }
            closed = true;
            RouterService local = service;
            service = null;
            if (listening && local != null) {
                local.unregisterListener(this);
            }
            listening = false;
            if (bound || binding) {
                try {
                    appContext.unbindService(connection);
                } catch (IllegalArgumentException e) {
                    Log.i(TAG, "service was not registered when unbinding");
                } catch (RuntimeException e) {
                    Log.w(TAG, "unable to unbind router service", e);
                }
            }
            bound = false;
            binding = false;
            serviceConnected.setValue(false);
        });
    }

    // RouterService.Listener -------------------------------------------------

    @Override
    public void onRouterSnapshot(RouterSnapshot value) {
        if (closed || value == null || value.equals(lastSnapshot)) {
            return;
        }
        lastSnapshot = value;
        if (Looper.myLooper() == Looper.getMainLooper()) {
            snapshot.setValue(value);
        } else {
            snapshot.postValue(value);
        }
    }

    @Override
    public void onRouterLogs(List<LogEntry> value) {
        if (closed) {
            return;
        }
        List<LogEntry> safe = value == null ? Collections.emptyList() : value;
        allLogs = safe;
        if (Looper.myLooper() == Looper.getMainLooper()) {
            logs.setValue(applyFilter(safe));
        } else {
            logs.postValue(applyFilter(safe));
        }
    }

    // Internals -------------------------------------------------------------

    private void ensureBound() {
        if (closed || bound || binding) {
            return;
        }
        binding = true;
        try {
            Intent intent = new Intent(appContext, RouterService.class);
            if (!appContext.bindService(intent, connection, Context.BIND_AUTO_CREATE)) {
                binding = false;
                Log.w(TAG, "bindService returned false");
                serviceConnected.setValue(false);
            }
        } catch (RuntimeException e) {
            binding = false;
            Log.e(TAG, "unable to bind router service", e);
            serviceConnected.setValue(false);
        }
    }

    private void sendCommand(String action, boolean foreground) {
        Intent intent = new Intent(appContext, RouterService.class).setAction(action);
        try {
            if (foreground) {
                androidx.core.content.ContextCompat.startForegroundService(appContext, intent);
            } else {
                appContext.startService(intent);
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to deliver router command " + action, e);
        }
    }

    private List<LogEntry> applyFilter(List<LogEntry> source) {
        String filter = logFilterLevel;
        if (filter == null || source.isEmpty()) {
            return source;
        }
        int minimum = rank(filter);
        if (minimum <= 0) {
            return source;
        }
        List<LogEntry> filtered = new ArrayList<>(source.size());
        for (LogEntry entry : source) {
            if (rank(entry.level()) >= minimum) {
                filtered.add(entry);
            }
        }
        return Collections.unmodifiableList(filtered);
    }

    private static String normalizeFilter(String level) {
        if (level == null) {
            return null;
        }
        String lower = level.trim().toLowerCase(Locale.ROOT);
        return switch (lower) {
            case "", "all", "none" -> null;
            case "debug" -> "debug";
            case "info" -> "info";
            case "warn", "warning" -> "warn";
            case "error", "critical" -> "error";
            default -> null;
        };
    }

    private static int rank(String level) {
        if (level == null) {
            return 0;
        }
        return switch (level.trim().toLowerCase(Locale.ROOT)) {
            case "debug", "trace" -> 0;
            case "info", "information" -> 1;
            case "warn", "warning" -> 2;
            case "error", "critical", "err" -> 3;
            case "none" -> 4;
            default -> 0;
        };
    }

    private void runOnMain(Runnable action) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            action.run();
        } else {
            mainHandler.post(action);
        }
    }
}
