/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.rule.ServiceTestRule;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Instrumentation lifecycle smoke test. It intentionally only binds and
 * unbinds the non-exported service; it does not start the native daemon, open
 * sockets or require a network. A device/emulator must still run it in CI.
 *
 * <p>The test clears the persisted run flag before binding so a previous test
 * or manual session cannot make {@link RouterService#onCreate()} start the
 * router. The literal key matches the service's private preference contract;
 * this is test-only and does not expose the service to UI code.</p>
 */
@RunWith(AndroidJUnit4.class)
public final class RouterServiceLifecycleTest {
    @Rule
    public final ServiceTestRule serviceRule = new ServiceTestRule();

    @Test
    public void bindsNonExportedRouterServiceAndReturnsLocalBinder() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences("router_state", Context.MODE_PRIVATE)
                .edit()
                .remove("should_run")
                .commit();

        Intent intent = new Intent(context, RouterService.class);
        IBinder binder = serviceRule.bindService(intent);

        assertNotNull("RouterService returned no binder", binder);
        assertTrue("unexpected binder type", binder instanceof RouterService.LocalBinder);
        RouterService service = ((RouterService.LocalBinder) binder).getService();
        assertNotNull("LocalBinder returned no service", service);

        serviceRule.unbindService();
    }
}
