/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
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

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            binding = false;
            bound = true;
            RouterService connected = binder instanceof RouterService.LocalBinder local
                    ? local.getService() : null;
            if (connected == null || closed) {
                return;
            }
            service = connected;
            serviceConnected.setValue(true);
            connected.registerListener(RouterRepository.this);
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            binding = false;
            bound = false;
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

    public void refresh() {
        RouterService local = service;
        if (local != null) {
            local.refresh();
        } else {
            bind();
        }
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

    public void close() {
        runOnMain(() -> {
            if (closed) {
                return;
            }
            closed = true;
            RouterService local = service;
            service = null;
            if (local != null) {
                local.unregisterListener(this);
            }
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
        if (closed) {
            return;
        }
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
        List<LogEntry> safe = value == null
                ? Collections.emptyList() : value;
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
                androidx.core.content.ContextCompat.startForegroundService(
                        appContext, intent);
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

    private static void runOnMain(Runnable action) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            action.run();
        } else {
            new android.os.Handler(Looper.getMainLooper()).post(action);
        }
    }
}
