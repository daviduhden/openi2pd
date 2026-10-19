/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: StatisticsFragment.
 *
 * Traffic chart and lifetime counters fed only by published snapshots, with
 * sample history preserved across configuration changes. A sample is appended
 * once per uptime value and the chart is cleared when uptime moves backwards,
 * so a restarted daemon does not inherit stale history.
 *
 */

package org.openi2pd.router;

import android.os.Bundle;
import android.text.TextUtils;
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
    private static final String STATE_LAST_SAMPLE_UPTIME = "chart_last_uptime";

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
    private long lastSampleUptime = -1L;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
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
            trafficChart.restoreHistory(savedInstanceState.getLongArray(STATE_CHART_DOWNLOAD),
                    savedInstanceState.getLongArray(STATE_CHART_UPLOAD));
            lastSampleUptime = savedInstanceState.getLong(STATE_LAST_SAMPLE_UPTIME, -1L);
        }

        render(null);
        viewModel.getSnapshot().observe(getViewLifecycleOwner(), this::render);
    }

    private void render(@Nullable RouterSnapshot snapshot) {
        boolean hasData = UiFormat.hasLiveMetrics(snapshot);
        String download = hasData ? UiFormat.rate(requireContext(), snapshot.inRate15())
                                  : getString(R.string.value_unknown);
        String upload = hasData ? UiFormat.rate(requireContext(), snapshot.outRate15())
                                : getString(R.string.value_unknown);

        UiFormat.setTextIfChanged(downloadRate, download);
        UiFormat.setTextIfChanged(uploadRate, upload);
        CharSequence chartDescription =
                getString(R.string.statistics_chart_description, download, upload);
        if (!TextUtils.equals(trafficChart.getContentDescription(), chartDescription)) {
            trafficChart.setContentDescription(chartDescription);
        }

        if (hasData) {
            if (snapshot.uptimeSeconds() < lastSampleUptime) {
                // The daemon restarted or the counter wrapped; do not append a
                // stale-history sample to the new run.
                trafficChart.clear();
                lastSampleUptime = -1L;
            }
            if (snapshot.uptimeSeconds() != lastSampleUptime) {
                trafficChart.addSample(snapshot.inRate15(), snapshot.outRate15());
                lastSampleUptime = snapshot.uptimeSeconds();
            }
        }

        if (hasData) {
            UiFormat.setTextIfChanged(
                    totalReceived, UiFormat.bytes(requireContext(), snapshot.totalReceived()));
            UiFormat.setTextIfChanged(
                    totalSent, UiFormat.bytes(requireContext(), snapshot.totalSent()));
            UiFormat.setTextIfChanged(
                    totalTransit, UiFormat.bytes(requireContext(), snapshot.totalTransit()));
            UiFormat.setTextIfChanged(
                    uptime, UiFormat.duration(requireContext(), snapshot.uptimeSeconds()));
            UiFormat.setTextIfChanged(peers, UiFormat.count(requireContext(), snapshot.peers()));
            UiFormat.setTextIfChanged(
                    routers, UiFormat.count(requireContext(), snapshot.routers()));
            long totalTunnels = (long) snapshot.inboundTunnels() + snapshot.outboundTunnels()
                    + snapshot.transitTunnels();
            UiFormat.setTextIfChanged(tunnels, UiFormat.count(requireContext(), totalTunnels));
        } else {
            UiFormat.setTextIfChanged(totalReceived, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(totalSent, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(totalTransit, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(uptime, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(peers, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(routers, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(tunnels, getString(R.string.value_unknown));
        }
    }

    @Override
    public void onSaveInstanceState(@NonNull Bundle outState) {
        if (trafficChart != null) {
            outState.putLongArray(STATE_CHART_DOWNLOAD, trafficChart.copyDownloadHistory());
            outState.putLongArray(STATE_CHART_UPLOAD, trafficChart.copyUploadHistory());
        }
        outState.putLong(STATE_LAST_SAMPLE_UPTIME, lastSampleUptime);
        super.onSaveInstanceState(outState);
    }
}
