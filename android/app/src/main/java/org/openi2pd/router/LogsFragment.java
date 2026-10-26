/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LogsFragment.
 *
 * Log screen with debounced local search, severity chips and a capped visible
 * list so a noisy daemon cannot exhaust the heap. Filtering, empty-state and
 * summary updates run on the main thread from LiveData, and clearing retained
 * logs requires a bound service.
 *
 */

package org.openi2pd.router;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
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
 *
 * <p>Filtering and search are presentation only: they select from the retained
 * entries the view model publishes and never change the native log ring or its
 * level. Search input is debounced on the main looper so a keystroke does not
 * re-diff the list, and the pending debounce is cancelled in
 * {@code onDestroyView} so a retained {@link Handler} cannot touch a released
 * adapter.</p>
 *
 * <p>Bounds: at most {@value #MAX_VISIBLE_LOGS} entries are retained from a
 * published batch and the same cap is applied again after filtering, so both
 * the model list and the adapter input stay bounded no matter how large the
 * published batch was. Clearing is the only mutating action; it needs a bound
 * service because the retained list belongs to the service, and the button
 * stays disabled otherwise.</p>
 *
 * <p>View updates and callbacks run on the main thread; only the adapter's own
 * diff is computed off it. Search text and the selected filter id are the only
 * saved state, so a recreated view restores the filter without replaying any
 * log content.</p>
 */
public class LogsFragment extends Fragment {
    private static final String STATE_QUERY = "log_query";
    private static final String STATE_FILTER = "log_filter";
    private static final int MAX_VISIBLE_LOGS = 1000;
    private static final long SEARCH_DEBOUNCE_MS = 250L;

    private RouterViewModel viewModel;
    private LogAdapter adapter;
    private RecyclerView logsList;
    private TextInputEditText search;
    private ChipGroup filterGroup;
    private View emptyState;
    private TextView emptyText;
    private TextView summary;
    private LinearProgressIndicator progress;
    private MaterialButton clearLogs;
    private Handler searchHandler;

    private final List<LogEntry> allLogs = new ArrayList<>();
    private final Runnable applySearchFilter = () -> applyFilters(false);
    private String query = "";
    private int filterId = R.id.filter_all;
    private LogLevel levelFilter = LogLevel.ALL;
    private boolean serviceConnected;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
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
        emptyText = view.findViewById(R.id.logs_empty_text);
        summary = view.findViewById(R.id.logs_summary);
        progress = view.findViewById(R.id.logs_progress);
        clearLogs = view.findViewById(R.id.button_clear_logs);
        searchHandler = new Handler(Looper.getMainLooper());

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
                if (searchHandler != null) {
                    searchHandler.removeCallbacks(applySearchFilter);
                    searchHandler.postDelayed(applySearchFilter, SEARCH_DEBOUNCE_MS);
                }
            }
        });

        Chip selected = view.findViewById(filterId);
        if (selected != null) {
            selected.setChecked(true);
        }
        levelFilter = levelForFilter(filterId);
        filterGroup.setOnCheckedStateChangeListener((group, checkedIds) -> {
            if (checkedIds.isEmpty()) {
                filterId = R.id.filter_all;
            } else {
                filterId = checkedIds.get(0);
            }
            levelFilter = levelForFilter(filterId);
            if (searchHandler != null) {
                searchHandler.removeCallbacks(applySearchFilter);
            }
            applyFilters(false);
        });

        clearLogs.setOnClickListener(ignored
                -> new MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.confirm_clear_logs_title)
                        .setMessage(R.string.confirm_clear_logs_message)
                        .setNegativeButton(R.string.action_cancel, null)
                        .setPositiveButton(R.string.action_clear_logs,
                                (dialog, which) -> {
                                    viewModel.clearLogs();
                                    Toast.makeText(requireContext(), R.string.logs_cleared,
                                                 Toast.LENGTH_SHORT)
                                            .show();
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

    /**
     * Recompute the visible list from the retained entries and submit it.
     *
     * <p>Main thread. Applies the severity filter and the query, keeps the
     * newest {@value #MAX_VISIBLE_LOGS} matches and posts the empty-state,
     * summary and clear-button updates from the diff completion callback, so the
     * summary always describes the list the adapter actually holds. That
     * callback re-checks {@code isAdded()} and the view because a diff can
     * complete after the view was destroyed.</p>
     *
     * @param scrollToNewest follow the tail when the user was already at the
     *                       bottom; a scrolled-up reader is never yanked away
     */
    private void applyFilters(boolean scrollToNewest) {
        if (!isAdded() || adapter == null) {
            return;
        }
        boolean wasAtBottom = isAtBottom();
        List<LogEntry> filtered = new ArrayList<>();
        String normalizedQuery = query.toLowerCase(Locale.ROOT);
        for (LogEntry entry : allLogs) {
            if (matchesLevel(entry) && matchesQuery(entry, normalizedQuery)) {
                filtered.add(entry);
            }
        }

        int start = Math.max(0, filtered.size() - MAX_VISIBLE_LOGS);
        List<LogEntry> visible =
                start == 0 ? filtered : new ArrayList<>(filtered.subList(start, filtered.size()));

        adapter.submitList(visible, () -> {
            if (!isAdded() || getView() == null) {
                return;
            }
            if (scrollToNewest && wasAtBottom && !visible.isEmpty()) {
                logsList.scrollToPosition(visible.size() - 1);
            }
            updateEmptyState();
            updateSummary();
            updateClearButton();
        });
    }

    private static LogLevel levelForFilter(int id) {
        if (id == R.id.filter_error)
            return LogLevel.ERROR;
        if (id == R.id.filter_warn)
            return LogLevel.WARN;
        if (id == R.id.filter_info)
            return LogLevel.INFO;
        if (id == R.id.filter_debug)
            return LogLevel.DEBUG;
        return LogLevel.ALL;
    }

    private boolean matchesLevel(@NonNull LogEntry entry) {
        return LogFilter.passes(levelFilter, entry.level());
    }

    private boolean matchesQuery(@NonNull LogEntry entry, @NonNull String normalizedQuery) {
        return LogFilter.matchesQuery(entry, normalizedQuery);
    }

    private boolean isAtBottom() {
        if (logsList == null || adapter == null || adapter.getItemCount() == 0) {
            return true;
        }
        RecyclerView.LayoutManager manager = logsList.getLayoutManager();
        if (!(manager instanceof LinearLayoutManager linearManager)) {
            return true;
        }
        return linearManager.findLastVisibleItemPosition() >= adapter.getItemCount() - 2;
    }

    private void updateEmptyState() {
        if (!isAdded() || emptyState == null) {
            return;
        }
        boolean empty = adapter == null || adapter.getItemCount() == 0;
        emptyState.setVisibility(empty ? View.VISIBLE : View.GONE);
        if (empty && emptyText != null) {
            if (allLogs.isEmpty() && !serviceConnected) {
                emptyText.setText(R.string.logs_loading);
            } else if (allLogs.isEmpty()) {
                emptyText.setText(R.string.logs_empty);
            } else {
                emptyText.setText(R.string.logs_empty_filtered);
            }
        }
        if (logsList != null) {
            logsList.setImportantForAccessibility(empty
                            ? View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
                            : View.IMPORTANT_FOR_ACCESSIBILITY_AUTO);
        }
        if (progress != null) {
            progress.setVisibility(empty && !serviceConnected ? View.VISIBLE : View.GONE);
        }
    }

    private void updateSummary() {
        if (!isAdded() || summary == null) {
            return;
        }
        int shown = adapter == null ? 0 : adapter.getItemCount();
        int retained = allLogs.size();
        if (retained == 0) {
            summary.setVisibility(View.GONE);
            return;
        }
        summary.setVisibility(View.VISIBLE);
        UiFormat.setTextIfChanged(summary,
                getResources().getQuantityString(R.plurals.logs_summary, shown, shown, retained));
    }

    private void updateClearButton() {
        if (clearLogs != null) {
            // Without a bound service there is no retained list to clear.
            clearLogs.setEnabled(serviceConnected && !allLogs.isEmpty());
        }
    }

    @Override
    public void onDestroyView() {
        if (searchHandler != null) {
            searchHandler.removeCallbacksAndMessages(null);
            searchHandler = null;
        }
        super.onDestroyView();
    }

    @Override
    public void onSaveInstanceState(@NonNull Bundle outState) {
        outState.putString(STATE_QUERY, query);
        outState.putInt(STATE_FILTER, filterId);
        super.onSaveInstanceState(outState);
    }
}
