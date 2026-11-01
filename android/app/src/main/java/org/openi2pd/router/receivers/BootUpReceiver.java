/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: BootUpReceiver.
 *
 * BOOT_COMPLETED and MY_PACKAGE_REPLACED receiver that resumes the
 * persisted run intent. It never sends an explicit start command, so a
 * router the operator stopped, or a drain whose deadline already
 * passed, cannot be resurrected by a broadcast. It never launches an
 * Activity and treats a refused background start as a logged,
 * user-recoverable failure.
 *
 */

package org.openi2pd.router.receivers;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import org.openi2pd.router.AutoStartController;
import org.openi2pd.router.RouterService;

/**
 * Boot and package-replacement resume.
 *
 * <p>Neither action starts a router by itself. The receiver only
 * delivers the service's sticky-restore command, so {@code
 * RouterService} reconciles the durable run state exactly as after a
 * {@code START_STICKY} restart: nothing is started for a persisted
 * stop, an expired graceful drain becomes its terminal stop and a live
 * drain is re-armed. {@code BOOT_COMPLETED} additionally requires the
 * operator's boot-autostart marker, while {@code MY_PACKAGE_REPLACED}
 * resumes only because the package update killed a router that was
 * already requested. Any other action is ignored.</p>
 *
 * <p>{@code onReceive} runs on the main thread under the broadcast
 * timeout, so it only decides and forwards one restore request; the
 * daemon lifecycle stays in {@link RouterService}. A refused or failed
 * start logs the action category only, never keys, paths or
 * configuration, and leaves recovery to the user.</p>
 */
public class BootUpReceiver extends BroadcastReceiver {
    private static final String TAG = "i2pdBoot";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null)
            return;
        if (context == null)
            return;
        String action = intent.getAction();
        boolean bootCompleted = Intent.ACTION_BOOT_COMPLETED.equals(action);
        boolean packageReplaced = Intent.ACTION_MY_PACKAGE_REPLACED.equals(action);
        if (!bootCompleted && !packageReplaced)
            return;
        // The marker is read only for the boot action; a package
        // replacement is not gated on the boot preference.
        boolean autostartEnabled = bootCompleted && AutoStartController.isEnabled(context);
        if (!mayResume(action, autostartEnabled, RouterService.isStartRequested(context)))
            return;
        if (!RouterService.requestRestore(context)) {
            Log.w(TAG,
                    "unable to restore router service after "
                            + (bootCompleted ? "boot" : "package replacement")
                            + "; Android may require a user-initiated start");
        }
    }

    /**
     * Pure decision for one broadcast, without a {@code Context}, so a
     * JVM test can pin the rule that a broadcast never starts a router
     * the operator stopped.
     *
     * @param action            broadcast action; any other action is
     *                          ignored
     * @param autostartEnabled  boot-autostart marker, read only for
     *                          {@code BOOT_COMPLETED}
     * @param runIntentRequested whether the persisted run intent is
     *                          anything other than a stopped router
     * @return whether the restore command may be delivered
     */
    public static boolean mayResume(String action, boolean autostartEnabled,
            boolean runIntentRequested) {
        // A stopped run intent is terminal for both actions: only an
        // explicit start command may begin a new run.
        if (!runIntentRequested)
            return false;
        if (Intent.ACTION_MY_PACKAGE_REPLACED.equals(action))
            return true;
        return Intent.ACTION_BOOT_COMPLETED.equals(action) && autostartEnabled;
    }
}
