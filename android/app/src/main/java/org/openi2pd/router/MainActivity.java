/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.PowerManager;
import android.provider.Settings;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.Toast;

import androidx.activity.OnBackPressedCallback;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentManager;
import androidx.fragment.app.FragmentTransaction;
import androidx.lifecycle.ViewModelProvider;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.navigation.NavigationBarView;

/**
 * Single-activity host for the router screens.
 *
 * <p>State is owned by {@link RouterViewModel} and the router service, so
 * recreating the activity (rotation, theme change, font-scale change) never
 * restarts the daemon. The activity only renders snapshots and forwards user
 * actions.</p>
 */
public class MainActivity extends AppCompatActivity {

    private static final String STATE_SELECTED_NAV = "selected_nav";
    private static final int REQUEST_POST_NOTIFICATIONS = 3;

    private static final String TAG_DASHBOARD = "dashboard";
    private static final String TAG_NETWORK = "network";
    private static final String TAG_TUNNELS = "tunnels";
    private static final String TAG_STATISTICS = "statistics";
    private static final String TAG_SETTINGS = "settings";
    private static final String TAG_LOGS = "logs";
    private static final String TAG_ABOUT = "about";

    private RouterViewModel viewModel;
    private MaterialToolbar toolbar;
    private NavigationBarView navigationView;

    private int selectedItemId = R.id.nav_home;
    private boolean suppressNavigation;
    private RouterSnapshot lastSnapshot;
    private int lastActionMask = -1;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        DynamicColors.applyToActivityIfAvailable(this);
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);
        toolbar.setTitle(R.string.app_name);

        navigationView = findViewById(R.id.nav_bottom);
        if (navigationView == null) {
            navigationView = findViewById(R.id.nav_rail);
        }
        if (navigationView != null) {
            navigationView.setOnItemSelectedListener(this::onNavigationSelected);
        }

        viewModel = new ViewModelProvider(this).get(RouterViewModel.class);

        FragmentManager fragments = getSupportFragmentManager();
        if (savedInstanceState == null) {
            viewModel.startOnFirstLaunchIfNeeded();
            showDestination(R.id.nav_home);
            requestNotificationPermission();
        } else {
            selectedItemId = savedInstanceState.getInt(STATE_SELECTED_NAV, R.id.nav_home);
            if (fragments.findFragmentById(R.id.fragment_container) == null) {
                showDestination(selectedItemId);
            } else {
                syncNavigationSelection(selectedItemId);
                restoreToolbarTitle(fragments);
            }
        }

        fragments.addOnBackStackChangedListener(() -> {
            if (fragments.getBackStackEntryCount() == 0) {
                toolbar.setTitle(R.string.app_name);
            }
        });

        viewModel.getSnapshot().observe(this, snapshot -> {
            lastSnapshot = snapshot;
            int mask = (UiFormat.gracefulActive(snapshot) ? 1 : 0)
                    | (UiFormat.isRunning(snapshot) ? 2 : 0)
                    | (UiFormat.isConnected(snapshot) ? 4 : 0)
                    | (UiFormat.canStop(snapshot) ? 8 : 0);
            if (mask != lastActionMask) {
                lastActionMask = mask;
                invalidateOptionsMenu();
            }
        });

        getOnBackPressedDispatcher().addCallback(this, new OnBackPressedCallback(true) {
            @Override
            public void handleOnBackPressed() {
                FragmentManager manager = getSupportFragmentManager();
                if (manager.getBackStackEntryCount() > 0) {
                    manager.popBackStack();
                    return;
                }
                if (selectedItemId != R.id.nav_home) {
                    showDestination(R.id.nav_home);
                    return;
                }
                setEnabled(false);
                getOnBackPressedDispatcher().onBackPressed();
            }
        });
    }

    /**
     * After a configuration change the FragmentManager restores the detail
     * fragment and its back stack entry, but the toolbar title is not part of
     * Fragment state. Derive it from the top back-stack entry.
     */
    private void restoreToolbarTitle(@NonNull FragmentManager fragments) {
        int count = fragments.getBackStackEntryCount();
        if (count == 0) {
            toolbar.setTitle(R.string.app_name);
            return;
        }
        String name = fragments.getBackStackEntryAt(count - 1).getName();
        if (TAG_LOGS.equals(name)) {
            toolbar.setTitle(R.string.logs_title);
        } else if (TAG_ABOUT.equals(name)) {
            toolbar.setTitle(R.string.about_title);
        } else {
            toolbar.setTitle(R.string.app_name);
        }
    }

    private boolean onNavigationSelected(@NonNull MenuItem item) {
        if (suppressNavigation) {
            return true;
        }
        showDestination(item.getItemId());
        return true;
    }

    private void showDestination(int itemId) {
        String tag = tagFor(itemId);
        if (tag == null) {
            return;
        }

        FragmentManager fragments = getSupportFragmentManager();
        if (fragments.getBackStackEntryCount() > 0) {
            fragments.popBackStackImmediate(null,
                    FragmentManager.POP_BACK_STACK_INCLUSIVE);
        }

        FragmentTransaction transaction = fragments.beginTransaction()
                .setReorderingAllowed(true);
        for (String existingTag : new String[]{
                TAG_DASHBOARD, TAG_NETWORK, TAG_TUNNELS,
                TAG_STATISTICS, TAG_SETTINGS, TAG_LOGS, TAG_ABOUT}) {
            Fragment existing = fragments.findFragmentByTag(existingTag);
            if (existing != null && !existingTag.equals(tag)) {
                transaction.hide(existing);
            }
        }

        Fragment target = fragments.findFragmentByTag(tag);
        if (target == null) {
            target = createFragment(itemId);
            transaction.add(R.id.fragment_container, target, tag);
        } else {
            transaction.show(target);
        }
        transaction.commitNow();

        selectedItemId = itemId;
        toolbar.setTitle(R.string.app_name);
        syncNavigationSelection(itemId);
    }

    private void syncNavigationSelection(int itemId) {
        if (navigationView == null || navigationView.getSelectedItemId() == itemId) {
            return;
        }
        suppressNavigation = true;
        try {
            navigationView.setSelectedItemId(itemId);
        } finally {
            suppressNavigation = false;
        }
    }

    @Nullable
    private static String tagFor(int itemId) {
        if (itemId == R.id.nav_home) {
            return TAG_DASHBOARD;
        }
        if (itemId == R.id.nav_network) {
            return TAG_NETWORK;
        }
        if (itemId == R.id.nav_tunnels) {
            return TAG_TUNNELS;
        }
        if (itemId == R.id.nav_stats) {
            return TAG_STATISTICS;
        }
        if (itemId == R.id.nav_settings) {
            return TAG_SETTINGS;
        }
        return null;
    }

    @NonNull
    private static Fragment createFragment(int itemId) {
        if (itemId == R.id.nav_network) {
            return new NetworkFragment();
        }
        if (itemId == R.id.nav_tunnels) {
            return new TunnelsFragment();
        }
        if (itemId == R.id.nav_stats) {
            return new StatisticsFragment();
        }
        if (itemId == R.id.nav_settings) {
            return new SettingsFragment();
        }
        return new DashboardFragment();
    }

    private void openDetail(@NonNull Fragment fragment, @NonNull String tag,
                            @StringRes int title) {
        getSupportFragmentManager().beginTransaction()
                .setReorderingAllowed(true)
                .replace(R.id.fragment_container, fragment, tag)
                .addToBackStack(tag)
                .commit();
        toolbar.setTitle(title);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.main_actions, menu);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        MenuItem graceful = menu.findItem(R.id.action_graceful_stop);
        MenuItem reload = menu.findItem(R.id.action_reload_tunnels);
        MenuItem stop = menu.findItem(R.id.action_stop);

        boolean gracefulActive = UiFormat.gracefulActive(lastSnapshot);
        if (graceful != null) {
            graceful.setTitle(gracefulActive
                    ? R.string.action_cancel_graceful_stop : R.string.action_graceful_stop);
            graceful.setEnabled(gracefulActive || UiFormat.isRunning(lastSnapshot));
        }
        if (reload != null) {
            reload.setEnabled(UiFormat.isConnected(lastSnapshot));
        }
        if (stop != null) {
            stop.setEnabled(UiFormat.canStop(lastSnapshot));
        }
        return super.onPrepareOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(@NonNull MenuItem item) {
        int id = item.getItemId();
        if (id == R.id.action_logs) {
            openDetail(new LogsFragment(), TAG_LOGS, R.string.logs_title);
            return true;
        }
        if (id == R.id.action_about) {
            openDetail(new AboutFragment(), TAG_ABOUT, R.string.about_title);
            return true;
        }
        if (id == R.id.action_battery_optimizations) {
            showBatteryOptimizationOptions();
            return true;
        }
        if (id == R.id.action_reload_tunnels) {
            viewModel.reloadTunnels();
            Toast.makeText(this, R.string.tunnels_reloading, Toast.LENGTH_SHORT).show();
            return true;
        }
        if (id == R.id.action_graceful_stop) {
            toggleGracefulStop();
            return true;
        }
        if (id == R.id.action_stop) {
            confirm(R.string.confirm_exit_title, R.string.confirm_exit_message,
                    R.string.action_exit, () -> {
                        viewModel.stopRouter();
                        Toast.makeText(this, R.string.toast_stopping,
                                Toast.LENGTH_SHORT).show();
                        finishAndRemoveTask();
                    });
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    private void toggleGracefulStop() {
        if (UiFormat.gracefulActive(lastSnapshot)) {
            viewModel.cancelGracefulStop();
            Toast.makeText(this, R.string.toast_graceful_cancelled,
                    Toast.LENGTH_SHORT).show();
            return;
        }
        confirm(R.string.confirm_graceful_title, R.string.confirm_graceful_message,
                R.string.action_graceful_stop, () -> {
                    viewModel.beginGracefulStop();
                    Toast.makeText(this, R.string.toast_graceful_started,
                            Toast.LENGTH_SHORT).show();
                });
    }

    @SuppressLint("BatteryLife")
    private void showBatteryOptimizationOptions() {
        PowerManager powerManager = (PowerManager) getSystemService(POWER_SERVICE);
        if (powerManager != null
                && powerManager.isIgnoringBatteryOptimizations(getPackageName())) {
            Toast.makeText(this, R.string.battery_optimizations_already_disabled,
                    Toast.LENGTH_SHORT).show();
            return;
        }

        new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.battery_optimizations_enabled)
                .setMessage(R.string.battery_optimizations_enabled_dialog)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.action_continue, (dialog, which) -> {
                    Intent request = new Intent(
                            Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                            Uri.parse("package:" + getPackageName()));
                    try {
                        startActivity(request);
                    } catch (RuntimeException exception) {
                        openBatteryOptimizationSettings();
                    }
                })
                .show();
    }

    private void openBatteryOptimizationSettings() {
        try {
            startActivity(new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
        } catch (RuntimeException exception) {
            Toast.makeText(this,
                    R.string.os_version_does_not_support_battery_optimizations_show_os_dialog_api,
                    Toast.LENGTH_SHORT).show();
        }
    }

    private void confirm(@StringRes int title, @StringRes int message,
                         @StringRes int positive, @NonNull Runnable action) {
        new MaterialAlertDialogBuilder(this)
                .setTitle(title)
                .setMessage(message)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(positive, (dialog, which) -> action.run())
                .show();
    }

    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    REQUEST_POST_NOTIFICATIONS);
        }
    }

    @Override
    protected void onSaveInstanceState(@NonNull Bundle outState) {
        outState.putInt(STATE_SELECTED_NAV, selectedItemId);
        super.onSaveInstanceState(outState);
    }
}
