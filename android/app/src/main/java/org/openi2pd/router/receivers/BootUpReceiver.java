/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: BootUpReceiver.
 *
 * BOOT_COMPLETED receiver that starts the foreground router service only when
 * the autostart marker exists, using the same start command the UI handles. It
 * never launches an Activity and treats a refused background start as a
 * logged, user-recoverable failure.
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
 * Boot autostart.
 *
 * <p>The receiver starts the foreground router service; it never launches an
 * Activity from the background. On Android 10+ a background Activity start
 * would be refused anyway, and Android 12+ restricts some background
 * foreground-service starts. The router uses the {@code specialUse} service
 * type, which is not one of the types Android 15 blocks from
 * BOOT_COMPLETED; the attempt is still fail-closed: failure is logged and the
 * user can start the router from the launcher.</p>
 *
 * <p>Two actions are handled and they are not equivalent: {@code BOOT_COMPLETED}
 * starts only when the user's autostart preference is set, while
 * {@code MY_PACKAGE_REPLACED} resumes only a router whose start was already
 * requested, because the package update killed the process. Any other action
 * is ignored. A refused or failed start logs the action category only — never
 * keys, paths or configuration — and leaves recovery to the user.</p>
 *
 * <p>{@code onReceive} runs on the main thread under the broadcast timeout, so
 * it only decides and forwards one start request; the daemon lifecycle stays in
 * {@link RouterService}.</p>
 */
public class BootUpReceiver extends BroadcastReceiver {
    private static final String TAG = "i2pdBoot";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null)
            return;
        String action = intent.getAction();
        boolean packageReplaced = Intent.ACTION_MY_PACKAGE_REPLACED.equals(action);
        boolean bootCompleted = Intent.ACTION_BOOT_COMPLETED.equals(action);
        if (!packageReplaced && !bootCompleted)
            return;
        if (context == null)
            return;
        // A package update kills the process. Resume only a router that was
        // explicitly running before the update; boot still requires the
        // user's autostart preference.
        if (!packageReplaced && !AutoStartController.isEnabled(context))
            return;
        if (packageReplaced && !RouterService.isStartRequested(context))
            return;
        if (!RouterService.requestStart(context)) {
            Log.w(TAG,
                    "unable to start router service after "
                            + (packageReplaced ? "package replacement" : "boot")
                            + "; Android may require a user-initiated start");
        }
    }
}
