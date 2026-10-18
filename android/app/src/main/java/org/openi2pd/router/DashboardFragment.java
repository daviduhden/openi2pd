/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
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
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
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
            Toast.makeText(requireContext(), R.string.toast_starting,
                    Toast.LENGTH_SHORT).show();
        });
        buttonStop.setOnClickListener(ignored ->
                confirm(R.string.confirm_stop_title, R.string.confirm_stop_message,
                        R.string.action_stop, () -> {
                            viewModel.stopRouter();
                            Toast.makeText(requireContext(), R.string.toast_stopping,
                                    Toast.LENGTH_SHORT).show();
                        }));
        buttonRestart.setOnClickListener(ignored ->
                confirm(R.string.confirm_restart_title, R.string.confirm_restart_message,
                        R.string.action_restart, () -> {
                            viewModel.restartRouter();
                            Toast.makeText(requireContext(), R.string.toast_restarting,
                                    Toast.LENGTH_SHORT).show();
                        }));
        buttonGraceful.setOnClickListener(ignored -> {
            if (UiFormat.gracefulActive(lastSnapshot)) {
                viewModel.cancelGracefulStop();
                Toast.makeText(requireContext(), R.string.toast_graceful_cancelled,
                        Toast.LENGTH_SHORT).show();
            } else {
                confirm(R.string.confirm_graceful_title,
                        R.string.confirm_graceful_message,
                        R.string.action_graceful_stop, () -> {
                            viewModel.beginGracefulStop();
                            Toast.makeText(requireContext(),
                                    R.string.toast_graceful_started,
                                    Toast.LENGTH_SHORT).show();
                        });
            }
        });

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
        boolean hasData = UiFormat.isRunning(snapshot);

        statusTitle.setText(UiFormat.stateLabelRes(state));
        statusSubtitle.setText(UiFormat.statusSubtitle(context, snapshot));
        chipConnectivity.setText(UiFormat.connectivity(context, snapshot));
        chipTransports.setText(UiFormat.transports(context, snapshot));

        boolean busy = snapshot == null
                || state == UiFormat.RouterState.STARTING
                || state == UiFormat.RouterState.STOPPING;
        statusProgress.setVisibility(busy ? View.VISIBLE : View.GONE);
        statusProgress.setContentDescription(getString(state == UiFormat.RouterState.STOPPING
                ? R.string.state_stopping : R.string.state_starting));

        String warning = UiFormat.warningText(context, snapshot);
        if (warning == null || warning.isEmpty()) {
            warningCard.setVisibility(View.GONE);
        } else {
            warningCard.setVisibility(View.VISIBLE);
            textWarning.setText(warning);
        }

        if (hasData) {
            metricUptime.setText(UiFormat.duration(context, snapshot.uptimeSeconds()));
            metricPeers.setText(UiFormat.count(context, snapshot.peers()));
            metricRouters.setText(UiFormat.count(context, snapshot.routers()));
            metricTunnels.setText(getString(R.string.metric_tunnels_value,
                    snapshot.inboundTunnels(), snapshot.outboundTunnels(),
                    snapshot.transitTunnels()));
            metricReseed.setText(UiFormat.yesNo(context, snapshot.reseeding()));
            metricDownload.setText(UiFormat.rate(context, snapshot.inRate()));
            metricUpload.setText(UiFormat.rate(context, snapshot.outRate()));
        } else {
            metricUptime.setText(R.string.value_unknown);
            metricPeers.setText(R.string.value_unknown);
            metricRouters.setText(R.string.value_unknown);
            metricTunnels.setText(R.string.value_unknown);
            metricReseed.setText(R.string.value_unknown);
            metricDownload.setText(R.string.value_unknown);
            metricUpload.setText(R.string.value_unknown);
        }

        buttonStart.setEnabled(UiFormat.canStart(snapshot));
        buttonStop.setEnabled(UiFormat.canStop(snapshot));
        buttonRestart.setEnabled(UiFormat.canRestart(snapshot));

        boolean gracefulActive = UiFormat.gracefulActive(snapshot);
        buttonGraceful.setText(gracefulActive
                ? R.string.action_cancel_graceful_stop : R.string.action_graceful_stop);
        buttonGraceful.setEnabled(gracefulActive || UiFormat.canRestart(snapshot));
    }

    private void confirm(@StringRes int title, @StringRes int message,
                         @StringRes int positive, @NonNull Runnable action) {
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(title)
                .setMessage(message)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(positive, (dialog, which) -> action.run())
                .show();
    }
}
