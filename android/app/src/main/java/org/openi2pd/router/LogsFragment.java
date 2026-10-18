/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.chip.Chip;
import com.google.android.material.chip.ChipGroup;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import com.google.android.material.textfield.TextInputEditText;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Recycler-backed log screen with local search and severity filters. The list
 * is capped so a noisy native log stream cannot exhaust heap.
 */
public class LogsFragment extends Fragment {

    private static final String STATE_QUERY = "log_query";
    private static final String STATE_FILTER = "log_filter";
    private static final int MAX_VISIBLE_LOGS = 1000;

    private RouterViewModel viewModel;
    private LogAdapter adapter;
    private RecyclerView logsList;
    private TextInputEditText search;
    private ChipGroup filterGroup;
    private TextView emptyState;
    private LinearProgressIndicator progress;
    private MaterialButton clearLogs;

    private final List<LogEntry> allLogs = new ArrayList<>();
    private String query = "";
    private int filterId = R.id.filter_all;
    private boolean serviceConnected;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_logs, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        viewModel = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);

        logsList = view.findViewById(R.id.logs_list);
        search = view.findViewById(R.id.logs_search);
        filterGroup = view.findViewById(R.id.logs_filter_group);
        emptyState = view.findViewById(R.id.logs_empty);
        progress = view.findViewById(R.id.logs_progress);
        clearLogs = view.findViewById(R.id.button_clear_logs);

        if (savedInstanceState != null) {
            query = savedInstanceState.getString(STATE_QUERY, "");
            filterId = savedInstanceState.getInt(STATE_FILTER, R.id.filter_all);
        }

        adapter = new LogAdapter(requireContext());
        LinearLayoutManager layoutManager = new LinearLayoutManager(requireContext());
        layoutManager.setStackFromEnd(false);
        logsList.setLayoutManager(layoutManager);
        logsList.setItemAnimator(null);
        logsList.setAdapter(adapter);

        search.setText(query);
        if (query != null) {
            search.setSelection(query.length());
        }
        search.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence text, int start, int count, int after) {
                // Not needed.
            }

            @Override
            public void onTextChanged(CharSequence text, int start, int before, int count) {
                // Not needed.
            }

            @Override
            public void afterTextChanged(Editable editable) {
                query = editable == null ? "" : editable.toString().trim();
                applyFilters(false);
            }
        });

        Chip selected = view.findViewById(filterId);
        if (selected != null) {
            selected.setChecked(true);
        }
        filterGroup.setOnCheckedStateChangeListener((group, checkedIds) -> {
            if (checkedIds.isEmpty()) {
                filterId = R.id.filter_all;
            } else {
                filterId = checkedIds.get(0);
            }
            applyFilters(false);
        });

        clearLogs.setOnClickListener(ignored ->
                new MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.confirm_clear_logs_title)
                        .setMessage(R.string.confirm_clear_logs_message)
                        .setNegativeButton(R.string.action_cancel, null)
                        .setPositiveButton(R.string.action_clear_logs, (dialog, which) -> {
                            viewModel.clearLogs();
                            Toast.makeText(requireContext(), R.string.logs_cleared,
                                    Toast.LENGTH_SHORT).show();
                        })
                        .show());

        progress.setContentDescription(getString(R.string.logs_loading));

        viewModel.getLogs().observe(getViewLifecycleOwner(), entries -> {
            allLogs.clear();
            if (entries != null) {
                int start = Math.max(0, entries.size() - MAX_VISIBLE_LOGS);
                for (int i = start; i < entries.size(); i++) {
                    LogEntry entry = entries.get(i);
                    if (entry != null) {
                        allLogs.add(entry);
                    }
                }
            }
            applyFilters(true);
        });

        viewModel.getServiceConnected().observe(getViewLifecycleOwner(), connected -> {
            serviceConnected = Boolean.TRUE.equals(connected);
            updateEmptyState();
            updateClearButton();
        });

        applyFilters(false);
    }

    private void applyFilters(boolean scrollToNewest) {
        if (!isAdded() || adapter == null) {
            return;
        }
        boolean wasAtBottom = isAtBottom();
        List<LogEntry> filtered = new ArrayList<>();
        String normalizedQuery = query.toLowerCase(Locale.getDefault());
        for (LogEntry entry : allLogs) {
            if (matchesLevel(entry) && matchesQuery(entry, normalizedQuery)) {
                filtered.add(entry);
            }
        }

        int start = Math.max(0, filtered.size() - MAX_VISIBLE_LOGS);
        List<LogEntry> visible = start == 0
                ? filtered : new ArrayList<>(filtered.subList(start, filtered.size()));

        adapter.submitList(visible, () -> {
            if (!isAdded() || getView() == null) {
                return;
            }
            if (scrollToNewest && wasAtBottom && !visible.isEmpty()) {
                logsList.scrollToPosition(visible.size() - 1);
            }
            updateEmptyState();
            updateClearButton();
        });
    }

    private boolean matchesLevel(@NonNull LogEntry entry) {
        if (filterId == R.id.filter_all) {
            return true;
        }
        String level = entry.level() == null
                ? "" : entry.level().toLowerCase(Locale.ROOT);
        if (filterId == R.id.filter_error) {
            return level.contains("error") || level.contains("fatal")
                    || level.contains("crit") || level.contains("fail");
        }
        if (filterId == R.id.filter_warn) {
            return level.contains("warn");
        }
        if (filterId == R.id.filter_info) {
            return level.contains("info") || level.contains("notice");
        }
        if (filterId == R.id.filter_debug) {
            return level.contains("debug") || level.contains("trace");
        }
        return true;
    }

    private boolean matchesQuery(@NonNull LogEntry entry, @NonNull String normalizedQuery) {
        if (normalizedQuery.isEmpty()) {
            return true;
        }
        return (entry.message() != null
                && entry.message().toLowerCase(Locale.getDefault()).contains(normalizedQuery))
                || (entry.level() != null
                && entry.level().toLowerCase(Locale.getDefault()).contains(normalizedQuery));
    }

    private boolean isAtBottom() {
        if (logsList == null || adapter == null || adapter.getItemCount() == 0) {
            return true;
        }
        RecyclerView.LayoutManager manager = logsList.getLayoutManager();
        if (!(manager instanceof LinearLayoutManager linearManager)) {
            return true;
        }
        return linearManager.findLastVisibleItemPosition()
                >= adapter.getItemCount() - 2;
    }

    private void updateEmptyState() {
        if (!isAdded() || emptyState == null) {
            return;
        }
        boolean empty = adapter == null || adapter.getItemCount() == 0;
        emptyState.setVisibility(empty ? View.VISIBLE : View.GONE);
        if (empty) {
            if (allLogs.isEmpty() && !serviceConnected) {
                emptyState.setText(R.string.logs_loading);
            } else if (allLogs.isEmpty()) {
                emptyState.setText(R.string.logs_empty);
            } else {
                emptyState.setText(R.string.logs_empty_filtered);
            }
        }
        progress.setVisibility(empty && !serviceConnected ? View.VISIBLE : View.GONE);
    }

    private void updateClearButton() {
        if (clearLogs != null) {
            // Without a bound service there is no retained list to clear.
            clearLogs.setEnabled(serviceConnected && !allLogs.isEmpty());
        }
    }

    @Override
    public void onSaveInstanceState(@NonNull Bundle outState) {
        outState.putString(STATE_QUERY, query);
        outState.putInt(STATE_FILTER, filterId);
        super.onSaveInstanceState(outState);
    }
}
