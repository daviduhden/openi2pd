/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: MainActivity.
 *
 * Single-activity host for the router screens. Screens, back-stack titles and
 * the service banner follow {@link RouterViewModel} state, so rotation or a
 * theme change never restarts the daemon; the first auto-start waits for the
 * notification and Android 17 local-network permission result.
 *
 */

package org.openi2pd.router;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.TextUtils;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.OnBackPressedCallback;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.core.graphics.drawable.DrawableCompat;
import androidx.core.view.ViewCompat;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentManager;
import androidx.fragment.app.FragmentTransaction;
import androidx.lifecycle.Lifecycle;
import androidx.lifecycle.ViewModelProvider;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.color.MaterialColors;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.navigation.NavigationBarView;

import java.util.ArrayList;
import java.util.List;

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
    private static final String STATE_START_AFTER_PERMISSION = "start_after_permission";
    private static final String PREF_PENDING_AUTOSTART = "pending_autostart";
    private static final int REQUEST_STARTUP_PERMISSIONS = 3;
    private static final String PERMISSION_ACCESS_LOCAL_NETWORK =
            "android.permission.ACCESS_LOCAL_NETWORK";

    private static final String TAG_DASHBOARD = "dashboard";
    private static final String TAG_NETWORK = "network";
    private static final String TAG_TUNNELS = "tunnels";
    private static final String TAG_STATISTICS = "statistics";
    private static final String TAG_SETTINGS = "settings";
    private static final String TAG_LOGS = "logs";
    private static final String TAG_ABOUT = "about";
    private static final String TAG_TORRENTS = "torrents";
    private static final String TAG_ADDRESS_BOOK = "address_book";
    private static final String TAG_RUNTIME_ADMIN = "runtime_admin";

    private RouterViewModel viewModel;
    private MaterialToolbar toolbar;
    private NavigationBarView navigationView;
    private View serviceBanner;
    private TextView serviceBannerText;

    private int selectedItemId = R.id.nav_home;
    private boolean suppressNavigation;
    private RouterSnapshot lastSnapshot;
    private int lastActionMask = -1;
    private boolean everServiceConnected;
    private boolean serviceBannerTimedOut;
    /** Defer the very first auto-start until the permission dialog resolves. */
    private boolean startAfterPermissionResult;
    private boolean pendingAutostart;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            setRecentsScreenshotEnabled(false);
        }
        setContentView(R.layout.activity_main);

        toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);
        toolbar.setTitle(R.string.app_name);
        toolbar.setNavigationOnClickListener(
                ignored -> getOnBackPressedDispatcher().onBackPressed());
        applyToolbarLogo();

        serviceBanner = findViewById(R.id.service_banner);
        serviceBannerText = findViewById(R.id.service_banner_text);
        View retry = findViewById(R.id.service_banner_retry);
        if (retry != null) {
            retry.setOnClickListener(ignored -> {
                if (viewModel != null) {
                    viewModel.refresh();
                }
            });
        }

        navigationView = findViewById(R.id.nav_bottom);
        if (navigationView == null) {
            navigationView = findViewById(R.id.nav_rail);
        }
        if (navigationView != null) {
            navigationView.setOnItemSelectedListener(this::onNavigationSelected);
        }

        viewModel = new ViewModelProvider(this).get(RouterViewModel.class);
        pendingAutostart = getSharedPreferences("router_state", MODE_PRIVATE)
                                   .getBoolean(PREF_PENDING_AUTOSTART, false);
        if (savedInstanceState != null) {
            startAfterPermissionResult =
                    savedInstanceState.getBoolean(STATE_START_AFTER_PERMISSION, false);
        }

        FragmentManager fragments = getSupportFragmentManager();
        if (savedInstanceState == null) {
            showDestination(R.id.nav_home);
            // Do not start the router until the permission result is known:
            // Android 17 would otherwise reject SSDP before the user grants
            // local-network access, leaving UPnP disabled for the session.
            startAfterPermissionResult = requestStartupPermissions();
            if (startAfterPermissionResult) {
                pendingAutostart = true;
                getSharedPreferences("router_state", MODE_PRIVATE)
                        .edit()
                        .putBoolean(PREF_PENDING_AUTOSTART, true)
                        .apply();
            } else {
                pendingAutostart = false;
                getSharedPreferences("router_state", MODE_PRIVATE)
                        .edit()
                        .remove(PREF_PENDING_AUTOSTART)
                        .apply();
                viewModel.startOnFirstLaunchIfNeeded();
            }
        } else {
            selectedItemId = savedInstanceState.getInt(STATE_SELECTED_NAV, R.id.nav_home);
            if (fragments.findFragmentById(R.id.fragment_container) == null) {
                showDestination(selectedItemId);
            } else {
                syncNavigationSelection(selectedItemId);
                updateNavigationState();
            }
        }

        fragments.addOnBackStackChangedListener(this::updateNavigationState);

        viewModel.getSnapshot().observe(this, snapshot -> {
            lastSnapshot = snapshot;
            updateToolbarSubtitle();
            int mask = (UiFormat.gracefulActive(snapshot) ? 1 : 0)
                    | (UiFormat.canGraceful(snapshot) ? 2 : 0)
                    | (UiFormat.isConnected(snapshot) ? 4 : 0)
                    | (UiFormat.canStop(snapshot) ? 8 : 0)
                    | (snapshot != null && snapshot.reseeding() ? 16 : 0);
            if (mask != lastActionMask) {
                lastActionMask = mask;
                invalidateOptionsMenu();
            }
        });

        viewModel.getServiceConnected().observe(this, connected -> {
            boolean isConnected = Boolean.TRUE.equals(connected);
            if (isConnected) {
                everServiceConnected = true;
                serviceBannerTimedOut = false;
            }
            updateServiceBanner(isConnected);
        });
        // A failed bind does not publish a transition, so surface the stale
        // state once without waiting forever. A later successful bind hides it.
        if (toolbar != null) {
            toolbar.postDelayed(() -> {
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                serviceBannerTimedOut = true;
                updateServiceBanner(
                        Boolean.TRUE.equals(viewModel.getServiceConnected().getValue()));
            }, 3000L);
        }

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
     * Keep the toolbar title and Up affordance in sync with the top of the
     * back stack. The title is not part of Fragment state, so it must be
     * derived after rotation and after every detail push/pop.
     */
    private void updateNavigationState() {
        FragmentManager fragments = getSupportFragmentManager();
        int count = fragments.getBackStackEntryCount();
        boolean detail = count > 0;
        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.setDisplayHomeAsUpEnabled(detail);
        }
        if (detail) {
            String name = fragments.getBackStackEntryAt(count - 1).getName();
            if (TAG_LOGS.equals(name)) {
                toolbar.setTitle(R.string.logs_title);
            } else if (TAG_ABOUT.equals(name)) {
                toolbar.setTitle(R.string.about_title);
            } else if (TAG_TORRENTS.equals(name)) {
                toolbar.setTitle(R.string.torrent_title);
            } else if (TAG_ADDRESS_BOOK.equals(name)) {
                toolbar.setTitle(R.string.address_book_title);
            } else if (TAG_RUNTIME_ADMIN.equals(name)) {
                toolbar.setTitle(R.string.runtime_admin_title);
            } else {
                toolbar.setTitle(R.string.app_name);
            }
        } else {
            toolbar.setTitle(titleFor(selectedItemId));
        }
        View container = findViewById(R.id.fragment_container);
        if (container != null && toolbar.getTitle() != null)
            ViewCompat.setAccessibilityPaneTitle(container, toolbar.getTitle());
        updateToolbarSubtitle();
        invalidateOptionsMenu();
    }

    private void applyToolbarLogo() {
        Drawable logo = AppCompatResources.getDrawable(this, R.drawable.ic_logo_monochrome);
        if (logo == null) {
            return;
        }
        Drawable tinted = logo.mutate();
        int tint =
                MaterialColors.getColor(toolbar, com.google.android.material.R.attr.colorOnSurface);
        DrawableCompat.setTint(tinted, tint);
        toolbar.setLogo(tinted);
        toolbar.setLogoDescription(R.string.app_name);
    }

    private void updateToolbarSubtitle() {
        if (toolbar == null) {
            return;
        }
        CharSequence subtitle = lastSnapshot == null
                ? getString(R.string.status_service_connecting)
                : getString(UiFormat.stateLabelRes(UiFormat.stateOf(lastSnapshot)));
        if (!TextUtils.equals(toolbar.getSubtitle(), subtitle)) {
            toolbar.setSubtitle(subtitle);
        }
    }

    private void updateServiceBanner(boolean connected) {
        if (serviceBanner == null) {
            return;
        }
        boolean show = !connected && (everServiceConnected || serviceBannerTimedOut);
        serviceBanner.setVisibility(show ? View.VISIBLE : View.GONE);
        if (show && serviceBannerText != null) {
            UiFormat.setTextIfChanged(
                    serviceBannerText, getString(R.string.service_disconnected_banner));
        }
    }

    @StringRes
    private static int titleFor(int itemId) {
        if (itemId == R.id.nav_network) {
            return R.string.nav_network;
        }
        if (itemId == R.id.nav_tunnels) {
            return R.string.nav_tunnels;
        }
        if (itemId == R.id.nav_stats) {
            return R.string.nav_stats;
        }
        if (itemId == R.id.nav_settings) {
            return R.string.nav_settings;
        }
        return R.string.nav_home;
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
            fragments.popBackStackImmediate(null, FragmentManager.POP_BACK_STACK_INCLUSIVE);
        }

        FragmentTransaction transaction = fragments.beginTransaction().setReorderingAllowed(true);
        for (String existingTag : new String[] {TAG_DASHBOARD, TAG_NETWORK, TAG_TUNNELS,
                     TAG_STATISTICS, TAG_SETTINGS, TAG_LOGS, TAG_ABOUT, TAG_TORRENTS,
                     TAG_ADDRESS_BOOK, TAG_RUNTIME_ADMIN}) {
            Fragment existing = fragments.findFragmentByTag(existingTag);
            if (existing != null && !existingTag.equals(tag)) {
                transaction.hide(existing);
                transaction.setMaxLifecycle(existing, Lifecycle.State.CREATED);
            }
        }

        Fragment target = fragments.findFragmentByTag(tag);
        if (target == null) {
            target = createFragment(itemId);
            transaction.add(R.id.fragment_container, target, tag);
        } else {
            transaction.show(target);
            transaction.setMaxLifecycle(target, Lifecycle.State.RESUMED);
        }
        transaction.commitNow();

        selectedItemId = itemId;
        syncNavigationSelection(itemId);
        updateNavigationState();
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

    /** Open the log detail from a dashboard quick action. */
    public void openLogs() {
        openDetail(new LogsFragment(), TAG_LOGS, R.string.logs_title);
    }

    /** Switch to the settings destination from a dashboard quick action. */
    public void openSettings() {
        showDestination(R.id.nav_settings);
    }

    private void openDetail(@NonNull Fragment fragment, @NonNull String tag, @StringRes int title) {
        FragmentManager fragments = getSupportFragmentManager();
        int count = fragments.getBackStackEntryCount();
        if (count > 0 && tag.equals(fragments.getBackStackEntryAt(count - 1).getName())) {
            return;
        }
        fragments.beginTransaction()
                .setReorderingAllowed(true)
                .replace(R.id.fragment_container, fragment, tag)
                .addToBackStack(tag)
                .commit();
        // The back-stack listener updates the title; setting it here avoids a
        // visible stale title until the asynchronous commit completes.
        toolbar.setTitle(title);
        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.setDisplayHomeAsUpEnabled(true);
        }
        invalidateOptionsMenu();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.main_actions, menu);
        return super.onCreateOptionsMenu(menu);
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        MenuItem graceful = menu.findItem(R.id.action_graceful_stop);
        MenuItem reload = menu.findItem(R.id.action_reload_tunnels);
        MenuItem reseed = menu.findItem(R.id.action_reseed);
        MenuItem stop = menu.findItem(R.id.action_stop);

        boolean detail = getSupportFragmentManager().getBackStackEntryCount() > 0;
        // Secondary destinations are entries, not tabs: showing them while a
        // detail is open would replace that detail and lose the back target.
        for (int id : new int[] {R.id.action_logs, R.id.action_about, R.id.action_torrents,
                     R.id.action_address_book, R.id.action_runtime_admin}) {
            MenuItem entry = menu.findItem(id);
            if (entry != null) {
                entry.setVisible(!detail);
            }
        }

        boolean gracefulActive = UiFormat.gracefulActive(lastSnapshot);
        if (graceful != null) {
            graceful.setTitle(gracefulActive ? R.string.action_cancel_graceful_stop
                                             : R.string.action_graceful_stop);
            graceful.setEnabled(gracefulActive || UiFormat.canGraceful(lastSnapshot));
        }
        if (reload != null) {
            reload.setEnabled(UiFormat.isConnected(lastSnapshot));
        }
        if (reseed != null) {
            // A reseed downloads SU3 bundles, so require a connected router and
            // block a second request while the native bootstrap is active.
            reseed.setEnabled(UiFormat.isConnected(lastSnapshot)
                    && !(lastSnapshot != null && lastSnapshot.reseeding()));
        }
        if (stop != null) {
            stop.setEnabled(UiFormat.canStop(lastSnapshot));
        }
        return super.onPrepareOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(@NonNull MenuItem item) {
        int id = item.getItemId();
        if (id == android.R.id.home) {
            getOnBackPressedDispatcher().onBackPressed();
            return true;
        }
        if (id == R.id.action_logs) {
            openDetail(new LogsFragment(), TAG_LOGS, R.string.logs_title);
            return true;
        }
        if (id == R.id.action_about) {
            openDetail(new AboutFragment(), TAG_ABOUT, R.string.about_title);
            return true;
        }
        if (id == R.id.action_torrents) {
            openDetail(new TorrentsFragment(), TAG_TORRENTS, R.string.torrent_title);
            return true;
        }
        if (id == R.id.action_address_book) {
            openDetail(new AddressBookFragment(), TAG_ADDRESS_BOOK, R.string.address_book_title);
            return true;
        }
        if (id == R.id.action_runtime_admin) {
            openDetail(new RuntimeAdminFragment(), TAG_RUNTIME_ADMIN, R.string.runtime_admin_title);
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
        if (id == R.id.action_reseed) {
            confirmReseed();
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
                        Toast.makeText(this, R.string.toast_stopping, Toast.LENGTH_SHORT).show();
                        finishAndRemoveTask();
                    });
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    /**
     * Reseeding contacts the configured reseed servers over HTTPS, so it is
     * confirmed first. The native call returns immediately with a typed
     * outcome; the bootstrap itself runs in the NetDb background job. Both
     * Toasts use the application context, so a rotation during the reseed
     * cannot retain the Activity.
     */
    void confirmReseed() {
        final Context appContext = getApplicationContext();
        confirm(R.string.confirm_reseed_title, R.string.confirm_reseed_message,
                R.string.action_reseed, () -> viewModel.requestReseed(outcome -> {
                    String token = outcome == null ? "" : outcome;
                    int message = switch (token) {
                        case "started" -> R.string.toast_reseed_started;
                        case "already-running" -> R.string.runtime_admin_reseed_running;
                        case "disabled" -> R.string.runtime_admin_reseed_disabled;
                        default -> R.string.toast_reseed_failed;
                    };
                    Toast.makeText(appContext, message, Toast.LENGTH_SHORT).show();
                }));
    }

    private void toggleGracefulStop() {
        if (UiFormat.gracefulActive(lastSnapshot)) {
            viewModel.cancelGracefulStop();
            Toast.makeText(this, R.string.toast_graceful_cancelled, Toast.LENGTH_SHORT).show();
            return;
        }
        confirm(R.string.confirm_graceful_title, R.string.confirm_graceful_message,
                R.string.action_graceful_stop, () -> {
                    viewModel.beginGracefulStop();
                    Toast.makeText(this, R.string.toast_graceful_started, Toast.LENGTH_SHORT)
                            .show();
                });
    }

    @SuppressLint("BatteryLife")
    private void showBatteryOptimizationOptions() {
        PowerManager powerManager = (PowerManager) getSystemService(POWER_SERVICE);
        if (powerManager != null && powerManager.isIgnoringBatteryOptimizations(getPackageName())) {
            Toast.makeText(
                         this, R.string.battery_optimizations_already_disabled, Toast.LENGTH_SHORT)
                    .show();
            return;
        }

        new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.battery_optimizations_enabled)
                .setMessage(R.string.battery_optimizations_enabled_dialog)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.action_continue,
                        (dialog, which) -> {
                            Intent request =
                                    new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
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
                         Toast.LENGTH_SHORT)
                    .show();
        }
    }

    private void confirm(@StringRes int title, @StringRes int message, @StringRes int positive,
            @NonNull Runnable action) {
        new MaterialAlertDialogBuilder(this)
                .setTitle(title)
                .setMessage(message)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(positive, (dialog, which) -> action.run())
                .show();
    }

    /**
     * Ask once for the runtime permissions that affect a running router.
     * Notifications are separate from routing; local-network access is
     * required by Android 17 before UPnP/SSDP may reach the gateway.
     */
    private boolean requestStartupPermissions() {
        List<String> missing = new ArrayList<>(2);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                        != PackageManager.PERMISSION_GRANTED) {
            missing.add(Manifest.permission.POST_NOTIFICATIONS);
        }
        if (Build.VERSION.SDK_INT >= 37
                && checkSelfPermission(PERMISSION_ACCESS_LOCAL_NETWORK)
                        != PackageManager.PERMISSION_GRANTED) {
            missing.add(PERMISSION_ACCESS_LOCAL_NETWORK);
        }
        if (missing.isEmpty()) {
            return false;
        }
        requestPermissions(missing.toArray(new String[0]), REQUEST_STARTUP_PERMISSIONS);
        return true;
    }

    @Override
    public void onRequestPermissionsResult(
            int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_STARTUP_PERMISSIONS) {
            return;
        }
        for (int i = 0; i < permissions.length && i < grantResults.length; i++) {
            if (grantResults[i] == PackageManager.PERMISSION_GRANTED) {
                continue;
            }
            if (Manifest.permission.POST_NOTIFICATIONS.equals(permissions[i])) {
                Toast.makeText(this, R.string.notification_permission_denied, Toast.LENGTH_LONG)
                        .show();
            } else if (PERMISSION_ACCESS_LOCAL_NETWORK.equals(permissions[i])) {
                Toast.makeText(this, R.string.local_network_permission_denied, Toast.LENGTH_LONG)
                        .show();
            }
        }
        if (startAfterPermissionResult) {
            startAfterPermissionResult = false;
            pendingAutostart = false;
            getSharedPreferences("router_state", MODE_PRIVATE)
                    .edit()
                    .remove(PREF_PENDING_AUTOSTART)
                    .apply();
            viewModel.startOnFirstLaunchIfNeeded();
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (viewModel != null) {
            viewModel.setUiVisible(true);
            if (pendingAutostart && !startAfterPermissionResult) {
                pendingAutostart = false;
                getSharedPreferences("router_state", MODE_PRIVATE)
                        .edit()
                        .remove(PREF_PENDING_AUTOSTART)
                        .apply();
                viewModel.startOnFirstLaunchIfNeeded();
            }
        }
    }

    @Override
    protected void onStop() {
        if (viewModel != null) {
            viewModel.setUiVisible(false);
        }
        super.onStop();
    }

    @Override
    protected void onSaveInstanceState(@NonNull Bundle outState) {
        outState.putInt(STATE_SELECTED_NAV, selectedItemId);
        outState.putBoolean(STATE_START_AFTER_PERMISSION, startAfterPermissionResult);
        super.onSaveInstanceState(outState);
    }
}
