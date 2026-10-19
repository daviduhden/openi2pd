/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: DashboardFragment.
 *
 * Overview screen that derives every label, metric and button state from the
 * published {@link RouterSnapshot}, showing unknown metrics until the native
 * status has actually been parsed. Start, stop, restart and graceful-stop are
 * confirmed by dialog and only forward commands through
 * {@link RouterViewModel}.
 *
 */

package org.openi2pd.router;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.chip.Chip;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;

/**
 * Router overview. Every control is enabled from the published snapshot and
 * destructive actions require confirmation.
 */
public class DashboardFragment extends Fragment {
    private RouterViewModel viewModel;

    private TextView statusTitle;
    private TextView statusSubtitle;
    private Chip chipConnectivity;
    private Chip chipTransports;
    private LinearProgressIndicator statusProgress;
    private MaterialButton buttonStart;
    private MaterialButton buttonStop;
    private MaterialButton buttonRestart;
    private MaterialButton buttonGraceful;
    private MaterialButton buttonLogs;
    private MaterialButton buttonSettings;
    private MaterialCardView warningCard;
    private TextView textWarning;
    private TextView metricUptime;
    private TextView metricPeers;
    private TextView metricRouters;
    private TextView metricTunnels;
    private TextView metricReseed;
    private TextView metricDownload;
    private TextView metricUpload;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_dashboard, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        viewModel = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);

        statusTitle = view.findViewById(R.id.status_title);
        statusSubtitle = view.findViewById(R.id.status_subtitle);
        chipConnectivity = view.findViewById(R.id.chip_connectivity);
        chipTransports = view.findViewById(R.id.chip_transports);
        statusProgress = view.findViewById(R.id.status_progress);
        buttonStart = view.findViewById(R.id.button_start);
        buttonStop = view.findViewById(R.id.button_stop);
        buttonRestart = view.findViewById(R.id.button_restart);
        buttonGraceful = view.findViewById(R.id.button_graceful);
        buttonLogs = view.findViewById(R.id.button_dashboard_logs);
        buttonSettings = view.findViewById(R.id.button_dashboard_settings);
        warningCard = view.findViewById(R.id.warning_card);
        textWarning = view.findViewById(R.id.text_warning);
        metricUptime = view.findViewById(R.id.metric_uptime);
        metricPeers = view.findViewById(R.id.metric_peers);
        metricRouters = view.findViewById(R.id.metric_routers);
        metricTunnels = view.findViewById(R.id.metric_tunnels);
        metricReseed = view.findViewById(R.id.metric_reseed);
        metricDownload = view.findViewById(R.id.metric_download);
        metricUpload = view.findViewById(R.id.metric_upload);

        buttonStart.setOnClickListener(ignored -> {
            viewModel.startRouter();
            Toast.makeText(requireContext(), R.string.toast_starting, Toast.LENGTH_SHORT).show();
        });
        buttonStop.setOnClickListener(ignored
                -> confirm(R.string.confirm_stop_title, R.string.confirm_stop_message,
                        R.string.action_stop, () -> {
                            viewModel.stopRouter();
                            Toast.makeText(requireContext(), R.string.toast_stopping,
                                         Toast.LENGTH_SHORT)
                                    .show();
                        }));
        buttonRestart.setOnClickListener(ignored
                -> confirm(R.string.confirm_restart_title, R.string.confirm_restart_message,
                        R.string.action_restart, () -> {
                            viewModel.restartRouter();
                            Toast.makeText(requireContext(), R.string.toast_restarting,
                                         Toast.LENGTH_SHORT)
                                    .show();
                        }));
        buttonLogs.setOnClickListener(ignored -> {
            if (requireActivity() instanceof MainActivity main) {
                main.openLogs();
            }
        });
        buttonSettings.setOnClickListener(ignored -> {
            if (requireActivity() instanceof MainActivity main) {
                main.openSettings();
            }
        });
        buttonGraceful.setOnClickListener(ignored -> {
            if (UiFormat.gracefulActive(lastSnapshot)) {
                viewModel.cancelGracefulStop();
                Toast.makeText(requireContext(), R.string.toast_graceful_cancelled,
                             Toast.LENGTH_SHORT)
                        .show();
            } else {
                confirm(R.string.confirm_graceful_title, R.string.confirm_graceful_message,
                        R.string.action_graceful_stop, () -> {
                            viewModel.beginGracefulStop();
                            Toast.makeText(requireContext(), R.string.toast_graceful_started,
                                         Toast.LENGTH_SHORT)
                                    .show();
                        });
            }
        });

        render(null);
        viewModel.getSnapshot().observe(getViewLifecycleOwner(), this::render);
    }

    private RouterSnapshot lastSnapshot;

    private void render(@Nullable RouterSnapshot snapshot) {
        lastSnapshot = snapshot;
        if (!isAdded()) {
            return;
        }
        android.content.Context context = requireContext();
        UiFormat.RouterState state = UiFormat.stateOf(snapshot);
        boolean hasData = UiFormat.hasLiveMetrics(snapshot);

        UiFormat.setTextIfChanged(statusTitle, getString(UiFormat.stateLabelRes(state)));
        statusTitle.setTextColor(ContextCompat.getColor(context, UiFormat.stateColorRes(state)));
        UiFormat.setTextIfChanged(statusSubtitle, UiFormat.statusSubtitle(context, snapshot));
        UiFormat.setTextIfChanged(chipConnectivity, UiFormat.connectivity(context, snapshot));
        UiFormat.setTextIfChanged(chipTransports, UiFormat.transports(context, snapshot));

        boolean busy = snapshot == null || state == UiFormat.RouterState.STARTING
                || state == UiFormat.RouterState.STOPPING;
        statusProgress.setVisibility(busy ? View.VISIBLE : View.GONE);
        statusProgress.setContentDescription(
                getString(state == UiFormat.RouterState.STOPPING ? R.string.state_stopping
                                                                 : R.string.state_starting));

        String warning = UiFormat.warningText(context, snapshot);
        if (warning == null || warning.isEmpty()) {
            warningCard.setVisibility(View.GONE);
        } else {
            warningCard.setVisibility(View.VISIBLE);
            UiFormat.setTextIfChanged(textWarning, warning);
        }

        if (hasData) {
            UiFormat.setTextIfChanged(
                    metricUptime, UiFormat.duration(context, snapshot.uptimeSeconds()));
            UiFormat.setTextIfChanged(metricPeers, UiFormat.count(context, snapshot.peers()));
            UiFormat.setTextIfChanged(metricRouters, UiFormat.count(context, snapshot.routers()));
            UiFormat.setTextIfChanged(metricTunnels,
                    getString(R.string.metric_tunnels_value, snapshot.inboundTunnels(),
                            snapshot.outboundTunnels(), snapshot.transitTunnels()));
            UiFormat.setTextIfChanged(metricReseed, UiFormat.yesNo(context, snapshot.reseeding()));
            UiFormat.setTextIfChanged(metricDownload, UiFormat.rate(context, snapshot.inRate15()));
            UiFormat.setTextIfChanged(metricUpload, UiFormat.rate(context, snapshot.outRate15()));
        } else {
            UiFormat.setTextIfChanged(metricUptime, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(metricPeers, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(metricRouters, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(metricTunnels, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(metricReseed, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(metricDownload, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(metricUpload, getString(R.string.value_unknown));
        }

        buttonStart.setEnabled(UiFormat.canStart(snapshot));
        buttonStop.setEnabled(UiFormat.canStop(snapshot));
        buttonRestart.setEnabled(UiFormat.canRestart(snapshot));

        boolean gracefulActive = UiFormat.gracefulActive(snapshot);
        buttonGraceful.setText(gracefulActive ? R.string.action_cancel_graceful_stop
                                              : R.string.action_graceful_stop);
        buttonGraceful.setEnabled(gracefulActive || UiFormat.canGraceful(snapshot));
    }

    private void confirm(@StringRes int title, @StringRes int message, @StringRes int positive,
            @NonNull Runnable action) {
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(title)
                .setMessage(message)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(positive, (dialog, which) -> action.run())
                .show();
    }
}
