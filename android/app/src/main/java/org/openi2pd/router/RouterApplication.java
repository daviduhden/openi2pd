/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterApplication.
 *
 * Inert {@link Application} subclass: it owns no daemon, native library load,
 * service binding or Activity listener. Keeping process startup free of side
 * effects means a broadcast-woken process cannot touch native code before
 * {@link RouterService} or an explicit user action asks for it.
 *
 */

package org.openi2pd.router;

import android.app.Application;

/**
 * Application entry point.
 *
 * <p>It deliberately owns no daemon, no service connection and no Activity
 * listener. The router lives in {@link RouterService}; UI components reach it
 * through {@link RouterViewModel}. Keeping this class inert also means a
 * process that is woken for a broadcast cannot start native code until the
 * service or an explicit user action asks for it.</p>
 */
public final class RouterApplication extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        // No daemon, native loading or service binding here. Those belong to
        // RouterService so they have an Android lifecycle owner.
    }
}
