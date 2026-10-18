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

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;

/**
 * Traffic history and lifetime counters. The chart is a bounded custom view;
 * samples are appended only when the service publishes a snapshot.
 */
public class StatisticsFragment extends Fragment {

    private static final String STATE_CHART_DOWNLOAD = "chart_download";
    private static final String STATE_CHART_UPLOAD = "chart_upload";

    private RouterViewModel viewModel;
    private TrafficChartView trafficChart;
    private TextView downloadRate;
    private TextView uploadRate;
    private TextView totalReceived;
    private TextView totalSent;
    private TextView totalTransit;
    private TextView uptime;
    private TextView peers;
    private TextView routers;
    private TextView tunnels;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_statistics, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        viewModel = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);

        trafficChart = view.findViewById(R.id.traffic_chart);
        downloadRate = view.findViewById(R.id.stat_download_rate);
        uploadRate = view.findViewById(R.id.stat_upload_rate);
        totalReceived = view.findViewById(R.id.stat_total_received);
        totalSent = view.findViewById(R.id.stat_total_sent);
        totalTransit = view.findViewById(R.id.stat_total_transit);
        uptime = view.findViewById(R.id.stat_uptime);
        peers = view.findViewById(R.id.stat_peers);
        routers = view.findViewById(R.id.stat_routers);
        tunnels = view.findViewById(R.id.stat_tunnels);

        if (savedInstanceState != null) {
            trafficChart.restoreHistory(
                    savedInstanceState.getLongArray(STATE_CHART_DOWNLOAD),
                    savedInstanceState.getLongArray(STATE_CHART_UPLOAD));
        }

        viewModel.getSnapshot().observe(getViewLifecycleOwner(), this::render);
    }

    private void render(@Nullable RouterSnapshot snapshot) {
        boolean hasData = snapshot != null && UiFormat.isRunning(snapshot);
        String download = hasData
                ? UiFormat.rate(requireContext(), snapshot.inRate())
                : getString(R.string.value_unknown);
        String upload = hasData
                ? UiFormat.rate(requireContext(), snapshot.outRate())
                : getString(R.string.value_unknown);

        downloadRate.setText(download);
        uploadRate.setText(upload);
        trafficChart.setContentDescription(getString(
                R.string.statistics_chart_description, download, upload));

        if (snapshot != null) {
            trafficChart.addSample(snapshot.inRate(), snapshot.outRate());
        }

        if (hasData) {
            totalReceived.setText(UiFormat.bytes(requireContext(), snapshot.totalReceived()));
            totalSent.setText(UiFormat.bytes(requireContext(), snapshot.totalSent()));
            totalTransit.setText(UiFormat.bytes(requireContext(), snapshot.totalTransit()));
            uptime.setText(UiFormat.duration(requireContext(), snapshot.uptimeSeconds()));
            peers.setText(UiFormat.count(requireContext(), snapshot.peers()));
            routers.setText(UiFormat.count(requireContext(), snapshot.routers()));
            long totalTunnels = (long) snapshot.inboundTunnels()
                    + snapshot.outboundTunnels() + snapshot.transitTunnels();
            tunnels.setText(UiFormat.count(requireContext(), totalTunnels));
        } else {
            totalReceived.setText(R.string.value_unknown);
            totalSent.setText(R.string.value_unknown);
            totalTransit.setText(R.string.value_unknown);
            uptime.setText(R.string.value_unknown);
            peers.setText(R.string.value_unknown);
            routers.setText(R.string.value_unknown);
            tunnels.setText(R.string.value_unknown);
        }
    }

    @Override
    public void onSaveInstanceState(@NonNull Bundle outState) {
        if (trafficChart != null) {
            outState.putLongArray(STATE_CHART_DOWNLOAD,
                    trafficChart.copyDownloadHistory());
            outState.putLongArray(STATE_CHART_UPLOAD,
                    trafficChart.copyUploadHistory());
        }
        super.onSaveInstanceState(outState);
    }
}
