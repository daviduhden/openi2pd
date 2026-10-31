/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: NetworkFragment.
 *
 * Read-only network screen showing connectivity, per-family transport
 * addresses, NTCP2/SSU2 and the optional HTTP, SOCKS, BOB, SAM and I2CP
 * services. All values come from the service snapshot and render as
 * unknown until live status has been observed.
 *
 */

package org.openi2pd.router;

import android.content.Context;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;

import com.google.android.material.chip.Chip;

/**
 * Network and client-service status. All values come from the
 * service-owned snapshot; missing values are rendered as an em dash
 * instead of invented state.
 *
 * <p>Read-only by construction: the only control asks the view model
 * for a fresh poll, and no value on this screen starts, stops or
 * reconfigures a service. Addresses, transport names and service flags
 * are copied from the native snapshot into display strings; the host
 * text is treated as opaque and is never resolved, opened or
 * interpreted here.</p>
 *
 * <p>Runs on the main thread. The observer uses {@code
 * getViewLifecycleOwner()} so it stops at view destruction, and {@link
 * #render} requires the attached view: it calls {@code
 * requireContext()}. The address list is already bounded by {@link
 * RouterStatusParser} (at most 32 entries), and each family is rendered
 * from that bounded list rather than from a live query.</p>
 */
public class NetworkFragment extends Fragment {
    private RouterViewModel viewModel;

    private TextView networkState;
    private Chip chipIpv4;
    private Chip chipIpv6;
    private TextView textIpv4;
    private TextView textIpv6;
    private TextView textNtcp2;
    private TextView textSsu2;
    private TextView statusHttp;
    private TextView statusSocks;
    private TextView statusBob;
    private TextView statusSam;
    private TextView statusI2cp;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_network, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        viewModel = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);

        networkState = view.findViewById(R.id.text_network_state);
        chipIpv4 = view.findViewById(R.id.chip_ipv4);
        chipIpv6 = view.findViewById(R.id.chip_ipv6);
        textIpv4 = view.findViewById(R.id.text_ipv4);
        textIpv6 = view.findViewById(R.id.text_ipv6);
        textNtcp2 = view.findViewById(R.id.text_ntcp2);
        textSsu2 = view.findViewById(R.id.text_ssu2);
        statusHttp = view.findViewById(R.id.status_http);
        statusSocks = view.findViewById(R.id.status_socks);
        statusBob = view.findViewById(R.id.status_bob);
        statusSam = view.findViewById(R.id.status_sam);
        statusI2cp = view.findViewById(R.id.status_i2cp);

        view.findViewById(R.id.button_refresh_network)
                .setOnClickListener(ignored -> viewModel.refresh());

        render(null);
        viewModel.getSnapshot().observe(getViewLifecycleOwner(), this::render);
    }

    /**
     * Render one snapshot, or the unknown placeholders for {@code
     * null}.
     *
     * <p>Main thread, attached view only. {@code hasData} gates every
     * metric so the zero-initialised defaults of a start-in-progress
     * snapshot are shown as unknown rather than as "offline"/"no
     * addresses"; the IPv4/IPv6 chips are additionally gated on the
     * published address list being non-empty. A snapshot is display
     * input only and never implies that a client service is actually
     * reachable.</p>
     */
    private void render(@Nullable RouterSnapshot snapshot) {
        Context context = requireContext();
        boolean hasData = UiFormat.hasLiveMetrics(snapshot);
        boolean hasAddresses =
                hasData && snapshot.addresses() != null && !snapshot.addresses().isEmpty();

        if (hasData) {
            UiFormat.setTextIfChanged(networkState, UiFormat.connectivity(context, snapshot));
        } else {
            UiFormat.setTextIfChanged(networkState,
                    snapshot == null
                            ? getString(R.string.value_unknown)
                            : getString(UiFormat.stateLabelRes(UiFormat.stateOf(snapshot))));
        }

        boolean ipv4 = hasAddresses && UiFormat.hasAddress(snapshot.addresses(), false);
        boolean ipv6 = hasAddresses && UiFormat.hasAddress(snapshot.addresses(), true);
        UiFormat.setTextIfChanged(chipIpv4,
                hasData ? getString(ipv4 ? R.string.network_ipv4_available
                                         : R.string.network_ipv4_unavailable)
                        : getString(R.string.value_unknown));
        UiFormat.setTextIfChanged(chipIpv6,
                hasData ? getString(ipv6 ? R.string.network_ipv6_available
                                         : R.string.network_ipv6_unavailable)
                        : getString(R.string.value_unknown));

        if (hasData) {
            UiFormat.setTextIfChanged(
                    textIpv4, UiFormat.addressList(context, snapshot.addresses(), false));
            UiFormat.setTextIfChanged(
                    textIpv6, UiFormat.addressList(context, snapshot.addresses(), true));
            UiFormat.setTextIfChanged(textNtcp2, UiFormat.transport(context, snapshot, true));
            UiFormat.setTextIfChanged(textSsu2, UiFormat.transport(context, snapshot, false));
        } else {
            UiFormat.setTextIfChanged(textIpv4, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(textIpv6, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(textNtcp2, getString(R.string.value_unknown));
            UiFormat.setTextIfChanged(textSsu2, getString(R.string.value_unknown));
        }

        ServiceStates services = snapshot == null ? null : snapshot.services();
        boolean serviceData = hasData && services != null;
        setService(statusHttp, R.string.services_http_proxy,
                serviceData ? services.httpProxy() : null, context);
        setService(statusSocks, R.string.services_socks_proxy,
                serviceData ? services.socksProxy() : null, context);
        setService(statusBob, R.string.services_bob, serviceData ? services.bob() : null, context);
        setService(statusSam, R.string.services_sam, serviceData ? services.sam() : null, context);
        setService(
                statusI2cp, R.string.services_i2cp, serviceData ? services.i2cp() : null, context);
    }

    private void setService(@NonNull TextView view, int labelRes, @Nullable Boolean running,
            @NonNull Context context) {
        UiFormat.setTextIfChanged(view,
                getString(R.string.network_service_format, getString(labelRes),
                        running == null ? getString(R.string.value_unknown)
                                        : UiFormat.serviceState(context, running)));
    }
}
