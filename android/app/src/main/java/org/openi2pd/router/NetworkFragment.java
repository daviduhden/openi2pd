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
 * Network and client-service status. All values come from the service-owned
 * snapshot; missing values are rendered as an em dash instead of invented
 * state.
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
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
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

        viewModel.getSnapshot().observe(getViewLifecycleOwner(), this::render);
    }

    private void render(@Nullable RouterSnapshot snapshot) {
        Context context = requireContext();
        boolean hasData = snapshot != null && UiFormat.isRunning(snapshot);
        boolean hasAddresses = snapshot != null && snapshot.addresses() != null
                && !snapshot.addresses().isEmpty();

        networkState.setText(UiFormat.connectivity(context, snapshot));

        boolean ipv4 = hasAddresses && UiFormat.hasAddress(snapshot.addresses(), false);
        boolean ipv6 = hasAddresses && UiFormat.hasAddress(snapshot.addresses(), true);
        if (snapshot == null) {
            chipIpv4.setText(R.string.value_unknown);
            chipIpv6.setText(R.string.value_unknown);
        } else {
            chipIpv4.setText(ipv4
                    ? R.string.network_ipv4_available : R.string.network_ipv4_unavailable);
            chipIpv6.setText(ipv6
                    ? R.string.network_ipv6_available : R.string.network_ipv6_unavailable);
        }

        if (snapshot == null) {
            textIpv4.setText(R.string.value_unknown);
            textIpv6.setText(R.string.value_unknown);
            textNtcp2.setText(R.string.value_unknown);
            textSsu2.setText(R.string.value_unknown);
        } else {
            textIpv4.setText(UiFormat.addressList(context, snapshot.addresses(), false));
            textIpv6.setText(UiFormat.addressList(context, snapshot.addresses(), true));
            textNtcp2.setText(getString(R.string.network_transport_format,
                    getString(R.string.network_ntcp2),
                    UiFormat.enabledDisabled(context, snapshot.ntcp2())));
            textSsu2.setText(getString(R.string.network_transport_format,
                    getString(R.string.network_ssu2),
                    UiFormat.enabledDisabled(context, snapshot.ssu2())));
        }

        ServiceStates services = snapshot == null ? null : snapshot.services();
        boolean serviceData = hasData && services != null;
        setService(statusHttp, R.string.services_http_proxy,
                serviceData ? services.httpProxy() : null, context);
        setService(statusSocks, R.string.services_socks_proxy,
                serviceData ? services.socksProxy() : null, context);
        setService(statusBob, R.string.services_bob,
                serviceData ? services.bob() : null, context);
        setService(statusSam, R.string.services_sam,
                serviceData ? services.sam() : null, context);
        setService(statusI2cp, R.string.services_i2cp,
                serviceData ? services.i2cp() : null, context);
    }

    private void setService(@NonNull TextView view, int labelRes,
                            @Nullable Boolean running, @NonNull Context context) {
        view.setText(getString(R.string.network_service_format,
                getString(labelRes),
                running == null
                        ? getString(R.string.value_unknown)
                        : UiFormat.serviceState(context, running)));
    }
}
