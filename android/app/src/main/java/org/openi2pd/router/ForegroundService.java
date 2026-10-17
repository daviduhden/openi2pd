package org.openi2pd.router;

import static android.app.PendingIntent.FLAG_IMMUTABLE;
import static android.app.PendingIntent.FLAG_UPDATE_CURRENT;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.RequiresApi;
import androidx.core.app.NotificationCompat;

import java.lang.ref.WeakReference;

public class ForegroundService extends Service {
    private static final String TAG = "FgService";
    private static final String CHANNEL_ID = "i2pd_service";
    private static final int NOTIFICATION = 1;
    private static final Object LOCK = new Object();

    private static volatile WeakReference<ForegroundService> instance;
    private static volatile DaemonWrapper daemon;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private volatile boolean shown;
    private volatile boolean destroyed;

    private NotificationManager notificationManager;
    private DaemonWrapper listeningDaemon;

    private final DaemonWrapper.StateUpdateListener daemonStateUpdatedListener =
            (oldValue, newValue) -> updateNotificationText();

    /**
     * Hand a (process-wide) daemon to the service. The service keeps the
     * daemon reference for its own lifetime and never stores an Activity.
     */
    public static void init(DaemonWrapper daemon) {
        synchronized (LOCK) {
            ForegroundService.daemon = daemon;
        }
        ForegroundService current;
        synchronized (LOCK) {
            current = instance == null ? null : instance.get();
        }
        if (current != null) {
            current.mainHandler.post(current::syncDaemonListener);
        }
    }

    /**
     * @deprecated listener lifetime is owned by the service; kept for binary
     * compatibility with older callers.
     */
    @Deprecated
    public static void deinit() {
        // No-op: removing the listener on Activity destroy would silently stop
        // notification updates while the service is still running.
    }

    /**
     * Class for clients to access. Because this service always runs in the same
     * process as its clients, no IPC is involved.
     */
    public class LocalBinder extends Binder {
        ForegroundService getService() {
            return ForegroundService.this;
        }
    }

    private final IBinder binder = new LocalBinder();

    @Override
    public void onCreate() {
        super.onCreate();
        notificationManager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        synchronized (LOCK) {
            instance = new WeakReference<>(this);
        }
        syncDaemonListener();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "received start id " + startId);
        DaemonWrapper current;
        synchronized (LOCK) {
            current = daemon;
        }
        if (current == null && intent == null) {
            // START_STICKY restarted the service after process death. Without
            // the Activity there is no daemon to display, so do not leave a
            // misleading permanent notification.
            stopSelf();
            return START_NOT_STICKY;
        }
        syncDaemonListener();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public void onDestroy() {
        destroyed = true;
        cancelNotification();
        DaemonWrapper current = listeningDaemon;
        listeningDaemon = null;
        if (current != null) {
            current.removeStateChangeListener(daemonStateUpdatedListener);
        }
        synchronized (LOCK) {
            if (instance != null && instance.get() == this) {
                instance = null;
            }
        }
        mainHandler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    /**
     * Attach the listener to the currently configured daemon. Runs on the main
     * thread so listener registration cannot race notification updates.
     */
    private void syncDaemonListener() {
        if (destroyed) {
            return;
        }
        DaemonWrapper current;
        synchronized (LOCK) {
            current = daemon;
        }
        if (current != listeningDaemon) {
            if (listeningDaemon != null) {
                listeningDaemon.removeStateChangeListener(daemonStateUpdatedListener);
            }
            listeningDaemon = current;
            if (current != null) {
                current.addStateChangeListener(daemonStateUpdatedListener);
            }
        }
        updateNotificationText();
    }

    private void updateNotificationText() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post(this::updateNotificationText);
            return;
        }
        if (destroyed) {
            return;
        }
        try {
            showNotification();
        } catch (Throwable tr) {
            Log.e(TAG, "unable to update notification", tr);
        }
    }

    private void cancelNotification() {
        if (!shown) {
            return;
        }
        if (notificationManager != null) {
            notificationManager.cancel(NOTIFICATION);
        }
        stopForeground(STOP_FOREGROUND_REMOVE);
        shown = false;
    }

    private void showNotification() {
        DaemonWrapper current;
        synchronized (LOCK) {
            current = daemon;
        }
        if (destroyed) {
            return;
        }

        CharSequence text = current != null
                ? getText(current.getState().getStatusStringResourceId())
                : getText(R.string.uninitialized);
        PendingIntent contentIntent = PendingIntent.getActivity(this, 0,
                new Intent(this, I2PDActivity.class),
                FLAG_IMMUTABLE | FLAG_UPDATE_CURRENT);

        String channelId = "";
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            channelId = createNotificationChannel();
        }

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, channelId)
                .setOngoing(true)
                .setSmallIcon(R.drawable.ic_notification_icon)
                .setPriority(NotificationCompat.PRIORITY_DEFAULT)
                .setCategory(NotificationCompat.CATEGORY_SERVICE)
                .setTicker(text)
                .setWhen(System.currentTimeMillis())
                .setContentTitle(getText(R.string.app_name))
                .setContentText(text)
                .setContentIntent(contentIntent);
        Notification notification = builder.build();

        if (shown && notificationManager != null) {
            // Update the existing foreground notification in place. Calling
            // stopForeground/startForeground on every state change can hit
            // background-start restrictions on newer Android releases.
            notificationManager.notify(NOTIFICATION, notification);
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        } else {
            startForeground(NOTIFICATION, notification);
        }
        shown = true;
    }

    @RequiresApi(Build.VERSION_CODES.O)
    private String createNotificationChannel() {
        CharSequence channelName = getText(R.string.app_name);
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, channelName, NotificationManager.IMPORTANCE_LOW);
        channel.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
        if (notificationManager != null) {
            notificationManager.createNotificationChannel(channel);
        } else {
            Log.e(TAG, "NOTIFICATION_SERVICE is null");
        }
        return CHANNEL_ID;
    }
}
