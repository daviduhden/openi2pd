/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: AddressBookFragment.
 *
 * Native Material address-book list, add-or-replace dialogs and subscription
 * source administration. The screen talks only through RouterViewModel's
 * worker-backed JNI bridge; it never reads address-book files, key material or
 * storage paths.
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
import com.google.android.material.checkbox.MaterialCheckBox;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;

/** Bounded address-book administration; no files or native handles reach the UI. */
public final class AddressBookFragment extends Fragment {
    /** Bounds enforced by the native JNI entry points. */
    private static final int MAX_NAME = RuntimeAdminPolicy.MAX_ADDRESS_NAME;
    private static final int MAX_DESTINATION = RuntimeAdminPolicy.MAX_DESTINATION;
    private static final int MAX_SUBSCRIPTION_URL = RuntimeAdminPolicy.MAX_SUBSCRIPTION_URL;

    private RouterViewModel model;
    private LinearLayout rows;
    private LinearLayout subscriptionRows;
    private TextView status;
    private boolean busy;
    /**
     * View generation. Bridge callbacks are asynchronous, so the ones that
     * rebuild a row list capture the current value and discard their result
     * once {@link #onDestroyView()} has incremented it; a late result must never
     * address a released list or a detached view.
     */
    private int generation;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
            @Nullable Bundle state) {
        model = new ViewModelProvider(requireActivity()).get(RouterViewModel.class);
        UiFactory.Column column = UiFactory.scrollingColumn(requireContext());
        ScrollView scroll = column.scroll();
        LinearLayout content = column.content();
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

        TextView subscriptionTitle = new TextView(requireContext());
        subscriptionTitle.setText(R.string.address_book_subscriptions);
        content.addView(subscriptionTitle);
        button(content, R.string.address_book_subscription_add, ignored -> addSubscriptionDialog());
        subscriptionRows = new LinearLayout(requireContext());
        subscriptionRows.setOrientation(LinearLayout.VERTICAL);
        content.addView(subscriptionRows);
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
        subscriptionRows = null;
        status = null;
        super.onDestroyView();
    }

    private void button(LinearLayout parent, int label, View.OnClickListener action) {
        MaterialButton button = UiFactory.button(requireContext());
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
                boolean blinded = "1".equals(fields[2]);
                MaterialButton row = UiFactory.button(requireContext());
                row.setText(name + "\n" + destination
                        + (blinded ? " (" + getString(R.string.address_book_blinded) + ")" : ""));
                UiFactory.applyRowSemantics(row,
                        name + ", " + destination
                                + (blinded ? ", " + getString(R.string.address_book_blinded) : ""));
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
        model.readSubscriptions(response -> {
            if (expected != generation || subscriptionRows == null || status == null || !isAdded())
                return;
            subscriptionRows.removeAllViews();
            java.util.List<String> urls = RuntimeAdminPolicy.parseSubscriptions(response);
            for (String url : urls) {
                MaterialButton row = UiFactory.button(requireContext());
                row.setText(url);
                row.setOnClickListener(ignored -> removeSubscription(url));
                subscriptionRows.addView(row);
            }
            if (urls.isEmpty()) {
                TextView empty = new TextView(requireContext());
                empty.setText(R.string.address_book_subscriptions_empty);
                subscriptionRows.addView(empty);
            }
        });
    }

    private void addDialog() {
        LinearLayout form = new LinearLayout(requireContext());
        form.setOrientation(LinearLayout.VERTICAL);
        int padding = getResources().getDimensionPixelSize(R.dimen.margin_medium);
        form.setPadding(padding, padding, padding, 0);
        TextInputLayout nameLayout = new TextInputLayout(requireContext());
        nameLayout.setHint(getString(R.string.address_book_name));
        TextInputEditText name = new TextInputEditText(requireContext());
        name.setFilters(new android.text.InputFilter[] {
                new android.text.InputFilter.LengthFilter(MAX_NAME)});
        nameLayout.addView(name);
        form.addView(nameLayout);
        TextInputLayout destinationLayout = new TextInputLayout(requireContext());
        destinationLayout.setHint(getString(R.string.address_book_destination));
        TextInputEditText destination = new TextInputEditText(requireContext());
        destination.setFilters(new android.text.InputFilter[] {
                new android.text.InputFilter.LengthFilter(MAX_DESTINATION)});
        destinationLayout.addView(destination);
        form.addView(destinationLayout);
        // The daemon never re-points an existing name for a plain add; the
        // checkbox is the explicit overwrite request.
        MaterialCheckBox replace = new MaterialCheckBox(requireContext());
        replace.setText(R.string.address_book_replace_hint);
        form.addView(replace);

        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.address_book_add)
                .setView(form)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.address_book_save,
                        (dialog, which)
                                -> submitAdd(name.getText().toString(),
                                        destination.getText().toString(), replace.isChecked()))
                .show();
    }

    private void submitAdd(String name, String destination, boolean replace) {
        if (busy || status == null)
            return;
        String cleanName = name == null ? "" : name.trim();
        String cleanDestination = destination == null ? "" : destination.trim();
        if (!RuntimeAdminPolicy.isValidAddressName(cleanName)
                || !RuntimeAdminPolicy.isValidDestination(cleanDestination)) {
            status.setText(R.string.address_book_invalid);
            return;
        }
        busy = true;
        status.setText(R.string.address_book_saving);
        int expected = generation;
        model.addAddressChecked(cleanName, cleanDestination, replace, outcome -> {
            if (expected != generation || status == null || !isAdded())
                return;
            busy = false;
            if (RuntimeAdminPolicy.isOk(outcome)) {
                status.setText(
                        replace ? R.string.address_book_replaced : R.string.address_book_added);
                refresh();
            } else if (RuntimeAdminPolicy.isExists(outcome)) {
                // The daemon reports the existing name instead of silently
                // overwriting it; offer the explicit replacement.
                status.setText(R.string.address_book_exists);
                confirmReplace(cleanName, cleanDestination);
            } else if ("invalid".equals(outcome)) {
                status.setText(R.string.address_book_invalid);
            } else {
                status.setText(R.string.address_book_failed);
            }
        });
    }

    private void confirmReplace(String name, String destination) {
        if (busy || status == null || !isAdded())
            return;
        new MaterialAlertDialogBuilder(requireContext())
                .setMessage(getString(R.string.address_book_replace_confirm, name))
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.address_book_replace_hint,
                        (dialog, which) -> submitAdd(name, destination, true))
                .show();
    }

    private void addSubscriptionDialog() {
        TextInputLayout layout = new TextInputLayout(requireContext());
        layout.setHint(R.string.address_book_subscription_hint);
        TextInputEditText input = new TextInputEditText(requireContext());
        input.setFilters(new android.text.InputFilter[] {
                new android.text.InputFilter.LengthFilter(MAX_SUBSCRIPTION_URL)});
        layout.addView(input);
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.address_book_subscription_add)
                .setView(layout)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.address_book_save,
                        (dialog, which) -> {
                            String url = input.getText() == null
                                    ? ""
                                    : input.getText().toString().trim();
                            submitSubscription(url, true);
                        })
                .show();
    }

    private void submitSubscription(String url, boolean add) {
        if (busy || status == null)
            return;
        if (!RuntimeAdminPolicy.isValidSubscriptionUrl(url)) {
            status.setText(R.string.address_book_subscription_failed);
            return;
        }
        busy = true;
        int expected = generation;
        model.subscriptionCommand(url, add, outcome -> {
            if (expected != generation || status == null || !isAdded())
                return;
            busy = false;
            if (RuntimeAdminPolicy.isOk(outcome)) {
                status.setText(add ? R.string.address_book_subscription_added
                                   : R.string.address_book_subscription_removed);
                refresh();
            } else {
                status.setText(R.string.address_book_subscription_failed);
            }
        });
    }

    private void removeSubscription(String url) {
        if (busy)
            return;
        new MaterialAlertDialogBuilder(requireContext())
                .setMessage(getString(R.string.address_book_subscription_remove) + "\n" + url)
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(R.string.address_book_subscription_remove,
                        (dialog, which) -> submitSubscription(url, false))
                .show();
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
