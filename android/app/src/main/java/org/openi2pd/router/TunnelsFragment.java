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
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;

/**
 * Tunnel counts, configuration reload and graceful stop.
 *
 * <p>Transit tunnels are configured by the {@code notransit} option in
 * Settings. The daemon's accept-transit flag is not exposed by
 * {@link RouterSnapshot}, so this screen deliberately shows no switch that
 * would pretend to report or toggle the live value.</p>
 */
public class TunnelsFragment extends Fragment {
    private RouterViewModel viewModel;

    private TextView tunnelsIn;
    private TextView tunnelsOut;
    private TextView tunnelsTransit;
    private TextView tunnelsTotal;
    private MaterialButton reloadTunnels;
    private MaterialButton gracefulStop;
    private TextView graceRemaining;
    private LinearProgressIndicator graceProgress;

    private RouterSnapshot lastSnapshot;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_tunnels, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        viewModel = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);

        tunnelsIn = view.findViewById(R.id.text_tunnels_in);
        tunnelsOut = view.findViewById(R.id.text_tunnels_out);
        tunnelsTransit = view.findViewById(R.id.text_tunnels_transit);
        tunnelsTotal = view.findViewById(R.id.text_tunnels_total);
        reloadTunnels = view.findViewById(R.id.button_reload_tunnels);
        gracefulStop = view.findViewById(R.id.button_graceful_stop);
        graceRemaining = view.findViewById(R.id.text_grace_remaining);
        graceProgress = view.findViewById(R.id.grace_progress);

        reloadTunnels.setOnClickListener(ignored -> {
            viewModel.reloadTunnels();
            Toast.makeText(requireContext(), R.string.tunnels_reloading, Toast.LENGTH_SHORT).show();
        });

        gracefulStop.setOnClickListener(ignored -> {
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

    private void render(@Nullable RouterSnapshot snapshot) {
        lastSnapshot = snapshot;
        boolean hasData = UiFormat.hasLiveMetrics(snapshot);
        if (hasData) {
            UiFormat.setTextIfChanged(
                    tunnelsIn, UiFormat.count(requireContext(), snapshot.inboundTunnels()));
            UiFormat.setTextIfChanged(
                    tunnelsOut, UiFormat.count(requireContext(), snapshot.outboundTunnels()));
            UiFormat.setTextIfChanged(
                    tunnelsTransit, UiFormat.count(requireContext(), snapshot.transitTunnels()));
            long total = (long) snapshot.inboundTunnels() + snapshot.outboundTunnels()
                    + snapshot.transitTunnels();
            UiFormat.setTextIfChanged(tunnelsTotal, UiFormat.count(requireContext(), total));
        } else {
            UiFormat.setTextIfChanged(tunnelsIn, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(tunnelsOut, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(tunnelsTransit, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(tunnelsTotal, getString(R.string.value_unknown));
        }

        boolean gracefulActive = UiFormat.gracefulActive(snapshot);
        gracefulStop.setText(gracefulActive ? R.string.action_cancel_graceful_stop
                                            : R.string.action_graceful_stop);
        gracefulStop.setEnabled(gracefulActive || UiFormat.canGraceful(snapshot));
        reloadTunnels.setEnabled(UiFormat.isConnected(snapshot));

        if (gracefulActive && snapshot != null) {
            graceRemaining.setVisibility(View.VISIBLE);
            UiFormat.setTextIfChanged(graceRemaining,
                    getString(R.string.tunnels_grace_remaining,
                            UiFormat.grace(requireContext(), snapshot.graceRemainingMillis())));
            if (graceProgress != null) {
                long total = Math.max(1L, DaemonWrapper.GRACEFUL_STOP_DELAY_MILLIS);
                long remaining = Math.min(total, snapshot.graceRemainingMillis());
                graceProgress.setMax(1000);
                graceProgress.setProgressCompat(
                        (int) Math.round(remaining * 1000.0d / total), true);
                graceProgress.setVisibility(View.VISIBLE);
            }
        } else {
            graceRemaining.setVisibility(View.GONE);
            if (graceProgress != null) {
                graceProgress.setVisibility(View.GONE);
            }
        }
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
