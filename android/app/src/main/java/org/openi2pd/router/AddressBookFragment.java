/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: AddressBookFragment.
 *
 * Native Material address-book list and add/remove dialogs. The screen talks
 * only through RouterViewModel's worker-backed JNI bridge; it never reads
 * address-book files, key material or storage paths.
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
import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;

/** Bounded address-book administration; no files or native handles reach the UI. */
public final class AddressBookFragment extends Fragment {
    private RouterViewModel model;
    private LinearLayout rows;
    private TextView status;
    private boolean busy;
    private int generation;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
            @Nullable Bundle state) {
        model = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);
        ScrollView scroll = new ScrollView(requireContext());
        LinearLayout content = new LinearLayout(requireContext());
        content.setOrientation(LinearLayout.VERTICAL);
        int padding = Math.round(16 * getResources().getDisplayMetrics().density);
        content.setPadding(padding, padding, padding, padding);
        scroll.addView(content);
        status = new TextView(requireContext());
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        content.addView(status);
        button(content, R.string.address_book_refresh, ignored -> refresh());
        button(content, R.string.address_book_add, ignored -> addDialog());
        button(content, R.string.address_book_block, ignored -> blockDialog());
        button(content, R.string.address_book_blocked_list, ignored -> blockedDialog());
        rows = new LinearLayout(requireContext());
        rows.setOrientation(LinearLayout.VERTICAL);
        content.addView(rows);
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
        super.onDestroyView();
    }

    private void button(LinearLayout parent, int label, View.OnClickListener action) {
        MaterialButton button = new MaterialButton(requireContext());
        button.setText(label);
        button.setOnClickListener(action);
        parent.addView(button);
    }

    private void refresh() {
        if (busy || status == null)
            return;
        busy = true;
        status.setText(R.string.address_book_loading);
        int expected = generation;
        model.readAddressBook(response -> {
            if (expected != generation || status == null || !isAdded())
                return;
            busy = false;
            rows.removeAllViews();
            int count = 0;
            for (String line : response.split("\\n")) {
                String[] fields = line.split("\\|", -1);
                if (fields.length != 3 || fields[0].isEmpty() || fields[1].isEmpty())
                    continue;
                String name = fields[0];
                String destination = fields[1];
                MaterialButton row = new MaterialButton(requireContext());
                row.setText(
                        name + "\n" + destination + ("1".equals(fields[2]) ? " (blinded)" : ""));
                row.setOnClickListener(ignored -> removeDialog(name));
                rows.addView(row);
                if (++count >= 200)
                    break;
            }
            if (count == 0)
                status.setText(R.string.address_book_empty);
            else
                status.setText("");
        });
    }

    private void addDialog() {
        LinearLayout form = new LinearLayout(requireContext());
        form.setOrientation(LinearLayout.VERTICAL);
        int padding = Math.round(16 * getResources().getDisplayMetrics().density);
        form.setPadding(padding, padding, padding, 0);
        TextInputLayout nameLayout = new TextInputLayout(requireContext());
        nameLayout.setHint(getString(R.string.address_book_name));
        TextInputEditText name = new TextInputEditText(requireContext());
        nameLayout.addView(name);
        form.addView(nameLayout);
        TextInputLayout destinationLayout = new TextInputLayout(requireContext());
        destinationLayout.setHint(getString(R.string.address_book_destination));
        TextInputEditText destination = new TextInputEditText(requireContext());
        destinationLayout.addView(destination);
        form.addView(destinationLayout);

        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.address_book_add)
                .setView(form)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.address_book_save,
                        (dialog, which)
                                -> submitAdd(name.getText().toString(),
                                        destination.getText().toString()))
                .show();
    }

    private void submitAdd(String name, String destination) {
        if (busy || status == null)
            return;
        String cleanName = name == null ? "" : name.trim();
        String cleanDestination = destination == null ? "" : destination.trim();
        if (cleanName.isEmpty() || cleanDestination.isEmpty()) {
            status.setText(R.string.address_book_invalid);
            return;
        }
        busy = true;
        status.setText(R.string.address_book_saving);
        int expected = generation;
        model.addAddress(cleanName, cleanDestination, added -> {
            if (expected != generation || status == null || !isAdded())
                return;
            busy = false;
            status.setText(added ? R.string.address_book_added : R.string.address_book_failed);
            if (added)
                refresh();
        });
    }

    private void blockDialog() {
        TextInputLayout layout = new TextInputLayout(requireContext());
        layout.setHint(R.string.address_book_block_hint);
        TextInputEditText input = new TextInputEditText(requireContext());
        input.setFilters(
                new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(255)});
        layout.addView(input);
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.address_book_block)
                .setView(layout)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.address_book_save,
                        (dialog, which) -> {
                            String pattern = input.getText() == null
                                    ? ""
                                    : input.getText().toString().trim();
                            if (pattern.isEmpty() || busy || status == null)
                                return;
                            busy = true;
                            int expected = generation;
                            model.blockAddress(pattern, blocked -> {
                                if (expected != generation || status == null || !isAdded())
                                    return;
                                busy = false;
                                status.setText(blocked ? R.string.address_book_blocked
                                                       : R.string.address_book_block_failed);
                                if (blocked)
                                    refresh();
                            });
                        })
                .show();
    }

    private void blockedDialog() {
        model.readBlockedAddresses(response -> {
            if (status == null || !isAdded())
                return;
            java.util.ArrayList<String> patterns = new java.util.ArrayList<>();
            for (String line : response.split("\\n")) {
                if (!line.isEmpty() && patterns.size() < 200)
                    patterns.add(line);
            }
            if (patterns.isEmpty()) {
                status.setText(R.string.address_book_blocked_empty);
                return;
            }
            new MaterialAlertDialogBuilder(requireContext())
                    .setTitle(R.string.address_book_blocked_list)
                    .setItems(patterns.toArray(new String[0]),
                            (dialog, which) -> {
                                String pattern = patterns.get(which);
                                if (busy)
                                    return;
                                busy = true;
                                int expected = generation;
                                model.unblockAddress(pattern, unblocked -> {
                                    if (expected != generation || status == null || !isAdded())
                                        return;
                                    busy = false;
                                    status.setText(unblocked ? R.string.address_book_unblocked
                                                             : R.string.address_book_block_failed);
                                    if (unblocked)
                                        refresh();
                                });
                            })
                    .setNegativeButton(R.string.action_cancel, null)
                    .show();
        });
    }

    private void removeDialog(String name) {
        new MaterialAlertDialogBuilder(requireContext())
                .setMessage(getString(R.string.address_book_remove_confirm, name))
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.address_book_remove,
                        (dialog, which) -> {
                            if (busy || status == null)
                                return;
                            busy = true;
                            int expected = generation;
                            model.removeAddress(name, removed -> {
                                if (expected != generation || status == null || !isAdded())
                                    return;
                                busy = false;
                                status.setText(removed ? R.string.address_book_removed
                                                       : R.string.address_book_failed);
                                if (removed)
                                    refresh();
                            });
                        })
                .show();
    }
}
