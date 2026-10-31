/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: RuntimeAdminFragment.
 *
 * Material runtime-administration screen: per-tunnel start/stop, the
 * bounded NetDb reseed status with a "reseed now" action, and in-memory
 * router ban/unban. Every action is a closed typed command through
 * RouterViewModel's worker-backed bridge; the screen never reads keys,
 * filesystem paths or URLs.
 *
 */

package org.openi2pd.router;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;

import java.util.List;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;

/**
 * Bounded runtime administration.
 *
 * <p>Each control maps to one typed native command: {@code
 * startTunnel}/ {@code stopTunnel}, {@code requestReseed} plus {@code
 * getReseedStatus}, and {@code banRouter}/{@code unbanRouter}. There is
 * no generic admin entry point, no free-form option, and no path or URL
 * argument. Results arrive as the closed outcome tokens produced by the
 * native protocol and are shown as localized text.</p>
 */
public final class RuntimeAdminFragment extends Fragment {
    /** Bounds shared with the native bridge. */
    private static final int MAX_ROUTER_HASH = RuntimeAdminPolicy.MAX_ROUTER_HASH;

    private RouterViewModel model;
    private TextView status;
    private TextView reseedStatus;
    private LinearLayout rows;
    private boolean busy;
    /**
     * View generation. Command and read callbacks run after their JNI
     * call has returned, so each one captures the current value and is
     * dropped once {@link #onDestroyView()} has incremented it; a stale
     * outcome is therefore never shown on a detached view or applied to
     * a recreated screen.
     */
    private int generation;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
            @Nullable Bundle state) {
        model = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);
        UiFactory.Column column = UiFactory.scrollingColumn(requireContext());
        ScrollView scroll = column.scroll();
        LinearLayout content = column.content();

        TextView bounds = new TextView(requireContext());
        bounds.setText(R.string.runtime_admin_bounds);
        content.addView(bounds);

        status = new TextView(requireContext());
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        content.addView(status);

        content.addView(sectionTitle(R.string.runtime_admin_tunnels_title));
        TextView tunnelHint = new TextView(requireContext());
        tunnelHint.setText(R.string.runtime_admin_tunnels_hint);
        content.addView(tunnelHint);
        MaterialButton refresh = UiFactory.button(requireContext());
        refresh.setText(R.string.address_book_refresh);
        refresh.setOnClickListener(ignored -> refresh());
        content.addView(refresh);
        rows = new LinearLayout(requireContext());
        rows.setOrientation(LinearLayout.VERTICAL);
        content.addView(rows);

        content.addView(sectionTitle(R.string.runtime_admin_reseed_title));
        reseedStatus = new TextView(requireContext());
        content.addView(reseedStatus);
        MaterialButton reseed = UiFactory.button(requireContext());
        reseed.setText(R.string.runtime_admin_reseed_now);
        reseed.setOnClickListener(ignored -> confirmReseed());
        content.addView(reseed);

        content.addView(sectionTitle(R.string.runtime_admin_bans_title));
        TextView banHint = new TextView(requireContext());
        banHint.setText(R.string.runtime_admin_bans_hint);
        content.addView(banHint);
        MaterialButton ban = UiFactory.button(requireContext());
        ban.setText(R.string.runtime_admin_ban_action);
        ban.setOnClickListener(ignored -> banDialog(true));
        content.addView(ban);
        MaterialButton unban = UiFactory.button(requireContext());
        unban.setText(R.string.runtime_admin_unban_action);
        unban.setOnClickListener(ignored -> banDialog(false));
        content.addView(unban);

        return scroll;
    }

    @Override
    public void onResume() {
        super.onResume();
        refresh();
    }

    @Override
    public void onDestroyView() {
        ++generation;
        busy = false;
        rows = null;
        status = null;
        reseedStatus = null;
        super.onDestroyView();
    }

    private TextView sectionTitle(int label) {
        TextView title = new TextView(requireContext());
        title.setText(label);
        title.setPadding(0, getResources().getDimensionPixelSize(R.dimen.margin_small), 0, 0);
        return title;
    }

    private void refresh() {
        if (status == null) {
            return;
        }
        int expected = generation;
        model.readTunnelServices(response -> {
            if (expected != generation || rows == null || status == null || !isAdded()) {
                return;
            }
            renderTunnels(response);
        });
        model.readReseedStatus(response -> {
            if (expected != generation || reseedStatus == null || !isAdded()) {
                return;
            }
            renderReseed(response);
        });
    }

    private void renderTunnels(String response) {
        rows.removeAllViews();
        List<RuntimeAdminPolicy.TunnelService> services =
                RuntimeAdminPolicy.parseTunnelServices(response);
        for (RuntimeAdminPolicy.TunnelService service : services) {
            LinearLayout row = new LinearLayout(requireContext());
            row.setOrientation(LinearLayout.VERTICAL);
            TextView label = new TextView(requireContext());
            label.setText(
                    getString(R.string.runtime_admin_tunnel_row, service.name(), service.kind(),
                            getString(service.running() ? R.string.runtime_admin_state_running
                                                        : R.string.runtime_admin_state_stopped)));
            row.addView(label);
            LinearLayout actions = new LinearLayout(requireContext());
            actions.setOrientation(LinearLayout.HORIZONTAL);
            MaterialButton start = UiFactory.button(requireContext());
            start.setText(R.string.runtime_admin_tunnel_start);
            start.setContentDescription(
                    getString(R.string.runtime_admin_tunnel_start_named, service.name()));
            start.setEnabled(!service.running());
            start.setOnClickListener(ignored -> confirmTunnel(service.name(), true));
            actions.addView(start);
            MaterialButton stop = UiFactory.button(requireContext());
            stop.setText(R.string.runtime_admin_tunnel_stop);
            stop.setContentDescription(
                    getString(R.string.runtime_admin_tunnel_stop_named, service.name()));
            stop.setEnabled(service.running());
            stop.setOnClickListener(ignored -> confirmTunnel(service.name(), false));
            actions.addView(stop);
            row.addView(actions);
            rows.addView(row);
        }
        if (services.isEmpty()) {
            TextView empty = new TextView(requireContext());
            empty.setText(R.string.runtime_admin_tunnels_empty);
            rows.addView(empty);
        }
    }

    private void renderReseed(String response) {
        RuntimeAdminPolicy.ReseedStatus status = RuntimeAdminPolicy.parseReseedStatus(response);
        if (!status.isKnown()) {
            reseedStatus.setText(R.string.value_unknown);
            return;
        }
        int attempts = boundedInt(status.attempts());
        int succeeded = boundedInt(status.succeeded());
        StringBuilder text = new StringBuilder(
                getResources().getQuantityString(R.plurals.runtime_admin_reseed_status, attempts,
                        status.phase(), attempts, succeeded));
        if (!status.lastError().isEmpty()) {
            text.append('\n').append(
                    getString(R.string.runtime_admin_reseed_error, status.lastError()));
        }
        reseedStatus.setText(text.toString());
    }

    private static int boundedInt(String value) {
        if (value == null || value.isEmpty() || value.length() > 9) {
            return 0;
        }
        try {
            return Math.max(0, Integer.parseInt(value));
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private void confirmReseed() {
        if (busy || status == null) {
            return;
        }
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.runtime_admin_reseed_title)
                .setMessage(R.string.runtime_admin_reseed_confirm)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.runtime_admin_reseed_now,
                        (dialog, which) -> {
                            busy = true;
                            int expected = generation;
                            model.requestReseed(outcome -> {
                                if (expected != generation || status == null || !isAdded()) {
                                    return;
                                }
                                busy = false;
                                status.setText(reseedOutcomeLabel(outcome));
                                refresh();
                            });
                        })
                .show();
    }

    private int reseedOutcomeLabel(String outcome) {
        String token = outcome == null ? "" : outcome;
        return switch (token) {
            case "started" -> R.string.runtime_admin_reseed_started;
            case "already-running" -> R.string.runtime_admin_reseed_running;
            case "disabled" -> R.string.runtime_admin_reseed_disabled;
            default -> R.string.runtime_admin_reseed_failed;
        };
    }

    private void confirmTunnel(String name, boolean start) {
        if (busy || status == null || !RuntimeAdminPolicy.isValidTunnelName(name)) {
            return;
        }
        int message = start ? R.string.runtime_admin_tunnel_start_confirm
                            : R.string.runtime_admin_tunnel_stop_confirm;
        new MaterialAlertDialogBuilder(requireContext())
                .setMessage(getString(message, name))
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(start ? R.string.runtime_admin_tunnel_start
                                         : R.string.runtime_admin_tunnel_stop,
                        (dialog, which) -> {
                            busy = true;
                            int expected = generation;
                            model.tunnelCommand(name, start, outcome -> {
                                if (expected != generation || status == null || !isAdded()) {
                                    return;
                                }
                                busy = false;
                                status.setText(tunnelOutcomeLabel(outcome));
                                refresh();
                            });
                        })
                .show();
    }

    private int tunnelOutcomeLabel(String outcome) {
        String token = outcome == null ? "" : outcome;
        return switch (token) {
            case "started" -> R.string.runtime_admin_tunnel_started;
            case "stopped" -> R.string.runtime_admin_tunnel_stopped;
            case "already-running" -> R.string.runtime_admin_tunnel_running;
            case "already-stopped" -> R.string.runtime_admin_tunnel_already_stopped;
            case "unknown" -> R.string.runtime_admin_tunnel_unknown;
            case "unavailable" -> R.string.value_unknown;
            default -> R.string.runtime_admin_tunnel_failed;
        };
    }

    private void banDialog(boolean ban) {
        TextInputLayout layout = new TextInputLayout(requireContext());
        layout.setHint(R.string.runtime_admin_ban_hint);
        TextInputEditText input = new TextInputEditText(requireContext());
        input.setFilters(new android.text.InputFilter[] {
                new android.text.InputFilter.LengthFilter(MAX_ROUTER_HASH)});
        layout.addView(input);
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(ban ? R.string.runtime_admin_ban_action
                              : R.string.runtime_admin_unban_action)
                .setView(layout)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.address_book_save,
                        (dialog, which) -> {
                            String hash = input.getText() == null
                                    ? ""
                                    : input.getText().toString().trim();
                            if (busy || status == null) {
                                return;
                            }
                            if (!RuntimeAdminPolicy.isValidRouterHash(hash)) {
                                status.setText(R.string.runtime_admin_ban_invalid);
                                return;
                            }
                            busy = true;
                            int expected = generation;
                            model.banCommand(hash, ban, outcome -> {
                                if (expected != generation || status == null || !isAdded()) {
                                    return;
                                }
                                busy = false;
                                String token = outcome == null ? "" : outcome;
                                if ("ok".equals(token)) {
                                    status.setText(ban ? R.string.runtime_admin_ban_ok
                                                       : R.string.runtime_admin_unban_ok);
                                } else if ("invalid".equals(token)) {
                                    status.setText(R.string.runtime_admin_ban_invalid);
                                } else {
                                    status.setText(R.string.runtime_admin_ban_failed);
                                }
                                refresh();
                            });
                        })
                .show();
    }
}
