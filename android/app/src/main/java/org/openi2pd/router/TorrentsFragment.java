/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsFragment.
 *
 * Administrative torrent screen that talks only through the bounded
 * {@link RouterViewModel} control commands: discovery, magnet add, SAF import,
 * DHT, creation status, priorities, trackers and comments. Closing the screen
 * never stops the service or the torrents, and picker state survives Activity
 * or Fragment recreation through a registered result launcher.
 *
 */

package org.openi2pd.router;

import android.content.Intent;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;
import java.util.Map;
import java.util.function.Consumer;

/** Administrative views only: closing this screen never stops the service or torrents. */
public final class TorrentsFragment extends Fragment {
    private RouterViewModel model;
    private LinearLayout rows;
    private TextView status;
    private boolean busy;
    private int tunnel;
    private int generation;
    private int importTunnel;
    // Registration is unconditional and precedes STARTED, so the registry
    // restores picker results across Activity/Fragment recreation.
    private final ActivityResultLauncher<String[]> openTorrent =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), document -> {
                if (document == null || status == null || !isAdded())
                    return;
                if (busy) {
                    status.setText(R.string.torrent_busy);
                    return;
                }
                busy = true;
                status.setText(R.string.torrent_importing);
                model.importTorrent(importTunnel, document);
            });

    @Override
    public void onCreate(@Nullable Bundle state) {
        super.onCreate(state);
        if (state != null) {
            tunnel = state.getInt("torrent_tunnel", 0);
            importTunnel = state.getInt("torrent_import_tunnel", tunnel);
        }
    }

    @Override
    public void onSaveInstanceState(@NonNull Bundle state) {
        super.onSaveInstanceState(state);
        state.putInt("torrent_tunnel", tunnel);
        state.putInt("torrent_import_tunnel", importTunnel);
    }

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
        button(content, R.string.torrent_refresh, ignored -> discover());
        button(content, R.string.torrent_add_magnet, ignored -> addMagnet());
        button(content, R.string.torrent_import, ignored -> {
            if (busy)
                return;
            importTunnel = tunnel;
            openTorrent.launch(
                    new String[] {"application/x-bittorrent", "application/octet-stream"});
        });
        button(content, R.string.torrent_dht,
                ignored -> request("TORRENT-DHT " + tunnel, this::showFields));
        button(content, R.string.torrent_create, ignored -> createTorrent());
        button(content, R.string.torrent_creation_status,
                ignored -> request("TORRENT-SESSION " + tunnel, this::showCreation));
        rows = new LinearLayout(requireContext());
        rows.setOrientation(LinearLayout.VERTICAL);
        content.addView(rows);
        return scroll;
    }

    @Override
    public void onResume() {
        super.onResume();
        discover();
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle state) {
        super.onViewCreated(view, state);
        busy = model.isImportingTorrent();
        if (busy)
            status.setText(R.string.torrent_importing);
        model.getTorrentImportResult().observe(getViewLifecycleOwner(), result -> {
            if (result == null)
                return;
            model.clearTorrentImportResult();
            busy = false;
            try {
                TorrentControl.fields(result.response());
                tunnel = result.tunnel();
                refresh();
            } catch (IllegalArgumentException error) {
                status.setText(R.string.torrent_import_failed);
            }
        });
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

    private void request(String command, Consumer<Map<String, String>> done) {
        if (busy || status == null)
            return;
        busy = true;
        status.setText(R.string.torrent_loading);
        int expected = generation;
        model.torrentCommand("I2PDTUI/1 " + command, response -> {
            if (expected != generation || status == null || !isAdded())
                return;
            busy = false;
            try {
                Map<String, String> fields = TorrentControl.fields(response);
                status.setText("");
                done.accept(fields);
            } catch (IllegalArgumentException error) {
                status.setText(getString(R.string.torrent_error, error.getMessage()));
            }
        });
    }

    private int number(Map<String, String> fields, String key, int max) {
        int result = Integer.parseInt(fields.getOrDefault(key, "0"));
        if (result < 0 || result > max)
            throw new IllegalArgumentException("Invalid snapshot count");
        return result;
    }

    private void discover() {
        request("TORRENT-TUNNELS", fields -> {
            int count = number(fields, "torrent.tunnels", 200);
            if (count == 0) {
                rows.removeAllViews();
                status.setText(R.string.torrent_no_session);
                return;
            }
            if (count == 1) {
                tunnel = 0;
                refresh();
                return;
            }
            String[] names = new String[count];
            for (int i = 0; i < count; ++i)
                names[i] = fields.getOrDefault("torrent.tunnel." + i + ".name", "");
            new MaterialAlertDialogBuilder(requireContext())
                    .setTitle(R.string.torrent_session)
                    .setItems(names,
                            (dialog, which) -> {
                                tunnel = which;
                                refresh();
                            })
                    .show();
        });
    }

    private void refresh() {
        request("TORRENTS " + tunnel + " 200", fields -> {
            rows.removeAllViews();
            int count = number(fields, "torrent.count", 200);
            status.setText(getString(R.string.torrent_count, count));
            for (int i = 0; i < count; ++i) {
                String prefix = "torrent." + i + ".";
                String id = Integer.toString(number(fields, prefix + "id", Integer.MAX_VALUE));
                MaterialButton row = new MaterialButton(requireContext());
                row.setText(fields.getOrDefault(prefix + "name", "") + "\n"
                        + fields.getOrDefault(prefix + "status", "") + " · "
                        + fields.getOrDefault(prefix + "percent", "0") + "%");
                row.setOnClickListener(
                        view -> actions(id, fields.getOrDefault(prefix + "magnet", "")));
                rows.addView(row);
            }
        });
    }

    private void actions(String id, String magnet) {
        String[] labels = {getString(R.string.torrent_details), getString(R.string.torrent_pause),
                getString(R.string.torrent_resume), getString(R.string.torrent_share),
                getString(R.string.torrent_remove), getString(R.string.torrent_trackers),
                getString(R.string.torrent_comments), getString(R.string.torrent_priorities)};
        new MaterialAlertDialogBuilder(requireContext())
                .setItems(labels,
                        (dialog, action) -> {
                            switch (action) {
                            case 0 ->
                                request("TORRENT-DETAIL " + tunnel + " " + id, this::showFields);
                            case 1 ->
                                request("TORRENT-PAUSE " + tunnel + " " + id, ignored -> refresh());
                            case 2 ->
                                request("TORRENT-RESUME " + tunnel + " " + id,
                                        ignored -> refresh());
                            case 3 -> {
                                if (!magnet.startsWith("magnet:?") || magnet.length() > 2048)
                                    return;
                                Intent intent = new Intent(Intent.ACTION_SEND)
                                                        .setType("text/plain")
                                                        .putExtra(Intent.EXTRA_TEXT, magnet);
                                startActivity(Intent.createChooser(
                                        intent, getString(R.string.torrent_share)));
                            }
                            case 4 ->
                                new MaterialAlertDialogBuilder(requireContext())
                                        .setMessage(R.string.torrent_remove_confirm)
                                        .setNegativeButton(android.R.string.cancel, null)
                                        .setPositiveButton(R.string.torrent_remove,
                                                (confirmation, which)
                                                        -> request("TORRENT-REMOVE " + tunnel + " "
                                                                        + id,
                                                                ignored -> refresh()))
                                        .show();
                            case 5 ->
                                request("TORRENT-DETAIL " + tunnel + " " + id,
                                        fields -> trackers(id, fields));
                            case 6 ->
                                request("TORRENT-DETAIL " + tunnel + " " + id,
                                        fields -> comments(id, fields));
                            case 7 ->
                                request("TORRENT-DETAIL " + tunnel + " " + id,
                                        fields -> priorities(id, fields));
                            default -> {
                            }
                            }
                        })
                .show();
    }

    private void trackers(String id, Map<String, String> fields) {
        java.util.List<String> addresses = new java.util.ArrayList<>();
        for (int i = 0; i < 100 && fields.containsKey("tracker." + i + ".announce"); ++i) {
            addresses.add(fields.get("tracker." + i + ".announce"));
        }
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.torrent_trackers)
                .setItems(addresses.toArray(new String[0]),
                        (dialog, which)
                                -> new MaterialAlertDialogBuilder(requireContext())
                                        .setMessage(addresses.get(which))
                                        .setPositiveButton(R.string.torrent_tracker_edit,
                                                (choice, button)
                                                        -> editTracker(id, addresses.get(which)))
                                        .setNegativeButton(R.string.torrent_tracker_remove,
                                                (choice, button) -> {
                                                    try {
                                                        request(TorrentControl.tracker(false,
                                                                        tunnel, id,
                                                                        addresses.get(which)),
                                                                ignored -> refresh());
                                                    } catch (IllegalArgumentException error) {
                                                        status.setText(
                                                                getString(R.string.torrent_error,
                                                                        error.getMessage()));
                                                    }
                                                })
                                        .setNeutralButton(android.R.string.cancel, null)
                                        .show())
                .setPositiveButton(
                        R.string.torrent_tracker_add, (dialog, which) -> editTracker(id, null))
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void editTracker(String id, @Nullable String previous) {
        TextInputLayout layout = new TextInputLayout(requireContext());
        layout.setHelperText(getString(R.string.torrent_tracker_paused));
        TextInputEditText input = new TextInputEditText(layout.getContext());
        input.setHint(R.string.torrent_tracker_hint);
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        input.setFilters(
                new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(2048)});
        if (previous != null)
            input.setText(previous);
        layout.addView(input);
        var dialog = new MaterialAlertDialogBuilder(requireContext())
                             .setTitle(previous == null ? R.string.torrent_tracker_add
                                                        : R.string.torrent_tracker_edit)
                             .setView(layout)
                             .setNegativeButton(android.R.string.cancel, null)
                             .setPositiveButton(android.R.string.ok, null)
                             .create();
        dialog.setOnShowListener(ignored
                -> dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE)
                        .setOnClickListener(view -> {
                            try {
                                String address = String.valueOf(input.getText()).trim();
                                String command = TorrentControl.tracker(true, tunnel, id, address);
                                if (busy)
                                    return;
                                if (address.equals(previous)) {
                                    dialog.dismiss();
                                    return;
                                }
                                dialog.dismiss();
                                // Validate/add first. If it fails, preserve the old
                                // tracker; an interrupted replacement keeps both.
                                request(command, result -> {
                                    if (previous == null)
                                        refresh();
                                    else
                                        request(TorrentControl.tracker(false, tunnel, id, previous),
                                                removed -> refresh());
                                });
                            } catch (IllegalArgumentException error) {
                                layout.setError(error.getMessage());
                            }
                        }));
        dialog.show();
    }

    private void addMagnet() {
        TextInputLayout layout = new TextInputLayout(requireContext());
        TextInputEditText input = new TextInputEditText(layout.getContext());
        input.setHint(R.string.torrent_magnet_hint);
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        input.setFilters(
                new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(2048)});
        layout.addView(input);
        var dialog = new MaterialAlertDialogBuilder(requireContext())
                             .setTitle(R.string.torrent_add_magnet)
                             .setView(layout)
                             .setNegativeButton(android.R.string.cancel, null)
                             .setPositiveButton(R.string.torrent_add_magnet, null)
                             .create();
        dialog.setOnShowListener(ignored
                -> dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE)
                        .setOnClickListener(view -> {
                            try {
                                String command = TorrentControl.magnet(
                                        tunnel, String.valueOf(input.getText()).trim());
                                if (busy)
                                    return;
                                dialog.dismiss();
                                request(command, result -> refresh());
                            } catch (IllegalArgumentException error) {
                                layout.setError(error.getMessage());
                            }
                        }));
        dialog.show();
    }

    private void showFields(Map<String, String> fields) {
        StringBuilder text = new StringBuilder();
        if (fields.containsKey("dht.numNodes")) {
            text.append(getString(R.string.torrent_dht_summary,
                    fields.getOrDefault("dht.numNodes", "0"),
                    fields.getOrDefault("dht.numBuckets", "0"),
                    fields.getOrDefault("dht.numTorrents", "0"),
                    fields.getOrDefault("dht.numFailedPeers", "0")));
        } else {
            text.append(fields.getOrDefault("detail.name", ""))
                    .append('\n')
                    .append(fields.getOrDefault("detail.status", ""))
                    .append('\n')
                    .append(fields.getOrDefault("detail.hash", ""))
                    .append('\n')
                    .append(getString(R.string.torrent_transfer_summary,
                            fields.getOrDefault("detail.have", "0"),
                            fields.getOrDefault("detail.total", "0"),
                            fields.getOrDefault("detail.downRate", "0"),
                            fields.getOrDefault("detail.upRate", "0")))
                    .append('\n')
                    .append(fields.getOrDefault("detail.error", ""));
            text.append('\n').append(getString(R.string.torrent_files)).append('\n');
            for (int i = 0; i < 1000 && fields.containsKey("file." + i + ".name"); ++i) {
                String prefix = "file." + i + ".";
                text.append(fields.get(prefix + "name"))
                        .append("\n")
                        .append(fields.getOrDefault(prefix + "completed", "0"))
                        .append(" / ")
                        .append(fields.getOrDefault(prefix + "length", "0"))
                        .append(" B\n");
            }
            text.append('\n').append(getString(R.string.torrent_peers)).append('\n');
            for (int i = 0; i < 200 && fields.containsKey("peer." + i + ".ident"); ++i) {
                String prefix = "peer." + i + ".";
                text.append(fields.get(prefix + "ident"))
                        .append('\n')
                        .append(fields.getOrDefault(prefix + "client", ""))
                        .append(" · ")
                        .append(fields.getOrDefault(prefix + "progress", "0"))
                        .append("%\n");
            }
            text.append('\n').append(getString(R.string.torrent_trackers)).append('\n');
            for (int i = 0; i < 100 && fields.containsKey("tracker." + i + ".announce"); ++i) {
                String prefix = "tracker." + i + ".";
                text.append(fields.get(prefix + "announce"))
                        .append('\n')
                        .append(fields.getOrDefault(prefix + "lastResult", ""))
                        .append('\n');
            }
        }
        TextView body = new TextView(requireContext());
        body.setText(text);
        body.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(requireContext());
        scroll.addView(body);
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.torrent_details)
                .setView(scroll)
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    private void createTorrent() {
        TextInputLayout layout = new TextInputLayout(requireContext());
        layout.setHelperText(getString(R.string.torrent_create_help));
        TextInputEditText input = new TextInputEditText(layout.getContext());
        input.setHint(R.string.torrent_create_source);
        input.setFilters(
                new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(2048)});
        layout.addView(input);
        var dialog = new MaterialAlertDialogBuilder(requireContext())
                             .setTitle(R.string.torrent_create)
                             .setView(layout)
                             .setNegativeButton(android.R.string.cancel, null)
                             .setPositiveButton(R.string.torrent_create, null)
                             .create();
        dialog.setOnShowListener(ignored
                -> dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE)
                        .setOnClickListener(view -> {
                            try {
                                String command = TorrentControl.create(
                                        tunnel, String.valueOf(input.getText()).trim());
                                if (busy)
                                    return;
                                dialog.dismiss();
                                request(command,
                                        result
                                        -> request(
                                                "TORRENT-SESSION " + tunnel, this::showCreation));
                            } catch (IllegalArgumentException error) {
                                layout.setError(error.getMessage());
                            }
                        }));
        dialog.show();
    }

    private void showCreation(Map<String, String> fields) {
        boolean pending = "1".equals(fields.get("creation.pending"))
                || "true".equals(fields.get("creation.pending"));
        String message = getString(pending ? R.string.torrent_creation_pending
                                           : R.string.torrent_creation_finished)
                + "\n" + fields.getOrDefault("creation.message", "");
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.torrent_creation_status)
                .setMessage(message)
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    private void priorities(String id, Map<String, String> fields) {
        java.util.List<String> files = new java.util.ArrayList<>();
        for (int i = 0; i < 1000 && fields.containsKey("file." + i + ".name"); ++i) {
            String priority = fields.getOrDefault("file." + i + ".priority", "1");
            files.add(fields.get("file." + i + ".name") + " · " + priorityName(priority));
        }
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.torrent_priorities)
                .setItems(files.toArray(new String[0]),
                        (dialog, index) -> {
                            String[] options = {getString(R.string.torrent_priority_skip),
                                    getString(R.string.torrent_priority_normal),
                                    getString(R.string.torrent_priority_high)};
                            int selected = number(fields, "file." + index + ".priority", 2);
                            new MaterialAlertDialogBuilder(requireContext())
                                    .setTitle(files.get(index))
                                    .setSingleChoiceItems(options, selected,
                                            (choice, priority) -> {
                                                choice.dismiss();
                                                request("TORRENT-FILE-PRIORITY " + tunnel + " " + id
                                                                + " " + index + " " + priority,
                                                        result -> refresh());
                                            })
                                    .setNegativeButton(android.R.string.cancel, null)
                                    .show();
                        })
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private String priorityName(String priority) {
        return getString("0".equals(priority)  ? R.string.torrent_priority_skip
                        : "2".equals(priority) ? R.string.torrent_priority_high
                                                 : R.string.torrent_priority_normal);
    }

    private void comments(String id, Map<String, String> fields) {
        boolean enabled = "1".equals(fields.get("comments.enabled"))
                || "true".equals(fields.get("comments.enabled"));
        ScrollView scroll = new ScrollView(requireContext());
        LinearLayout content = new LinearLayout(requireContext());
        content.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(content);
        TextView privacy = new TextView(requireContext());
        privacy.setText(R.string.torrent_comments_privacy);
        content.addView(privacy);
        TextView summary = new TextView(requireContext());
        summary.setText(getString(R.string.torrent_comments_summary,
                fields.getOrDefault("comments.total", "0"),
                fields.getOrDefault("comments.average", "0"),
                fields.getOrDefault("comments.ratingCount", "0")));
        content.addView(summary);
        String failure = fields.getOrDefault("comments.persistenceError", "");
        if (!failure.isEmpty()) {
            TextView persistence = new TextView(requireContext());
            persistence.setText(getString(R.string.torrent_error, failure));
            content.addView(persistence);
        }
        var dialog = new MaterialAlertDialogBuilder(requireContext())
                             .setTitle(R.string.torrent_comments)
                             .setView(scroll)
                             .setNegativeButton(android.R.string.ok, null)
                             .create();
        button(content,
                enabled ? R.string.torrent_comments_disable : R.string.torrent_comments_enable,
                ignored -> {
                    if (busy)
                        return;
                    if (enabled) {
                        dialog.dismiss();
                        request("TORRENT-COMMENTS " + tunnel + " " + id + " OFF",
                                result -> refresh());
                    } else {
                        new MaterialAlertDialogBuilder(requireContext())
                                .setMessage(R.string.torrent_comments_privacy)
                                .setNegativeButton(android.R.string.cancel, null)
                                .setPositiveButton(R.string.torrent_comments_enable,
                                        (confirmation, button) -> {
                                            dialog.dismiss();
                                            request("TORRENT-COMMENTS " + tunnel + " " + id + " ON",
                                                    result -> refresh());
                                        })
                                .show();
                    }
                });
        if (enabled)
            button(content, R.string.torrent_comment_add, ignored -> {
                dialog.dismiss();
                addComment(id);
            });
        int count = number(fields, "comments.count", 256);
        for (int i = 0; i < count; ++i) {
            String prefix = "comment." + i + ".";
            if ("1".equals(fields.get(prefix + "hidden"))
                    || "true".equals(fields.get(prefix + "hidden")))
                continue;
            String localId = fields.getOrDefault(prefix + "id", "");
            try {
                if (Long.parseLong(localId) <= 0)
                    throw new IllegalArgumentException("Invalid comment ID");
            } catch (NumberFormatException error) {
                throw new IllegalArgumentException("Invalid comment ID");
            }
            MaterialButton entry = new MaterialButton(requireContext());
            String owner = fields.getOrDefault(prefix + "owner", "");
            entry.setText((owner.isEmpty() ? getString(R.string.torrent_comment_anonymous) : owner)
                    + " · " + fields.getOrDefault(prefix + "rating", "0") + "/5\n"
                    + fields.getOrDefault(prefix + "text", ""));
            entry.setOnClickListener(ignored
                    -> new MaterialAlertDialogBuilder(requireContext())
                            .setMessage(R.string.torrent_comment_hide_confirm)
                            .setNegativeButton(android.R.string.cancel, null)
                            .setPositiveButton(R.string.torrent_comment_hide,
                                    (confirmation, button) -> {
                                        dialog.dismiss();
                                        request("TORRENT-COMMENT-HIDE " + tunnel + " " + id + " "
                                                        + localId,
                                                result
                                                -> request("TORRENT-DETAIL " + tunnel + " " + id,
                                                        next -> comments(id, next)));
                                    })
                            .show());
            content.addView(entry);
        }
        dialog.show();
    }

    private void addComment(String id) {
        LinearLayout form = new LinearLayout(requireContext());
        form.setOrientation(LinearLayout.VERTICAL);
        TextView privacy = new TextView(requireContext());
        privacy.setText(R.string.torrent_comments_privacy);
        form.addView(privacy);
        TextInputLayout ownerLayout = new TextInputLayout(requireContext());
        TextInputEditText owner = new TextInputEditText(ownerLayout.getContext());
        owner.setHint(R.string.torrent_comment_owner);
        owner.setFilters(
                new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(32)});
        ownerLayout.addView(owner);
        form.addView(ownerLayout);
        TextInputLayout textLayout = new TextInputLayout(requireContext());
        TextInputEditText text = new TextInputEditText(textLayout.getContext());
        text.setHint(R.string.torrent_comment_text);
        text.setFilters(
                new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(512)});
        textLayout.addView(text);
        form.addView(textLayout);
        final int[] rating = {0};
        MaterialButton chooseRating = new MaterialButton(requireContext());
        chooseRating.setText(R.string.torrent_comment_no_rating);
        chooseRating.setOnClickListener(ignored -> {
            String[] options = {getString(R.string.torrent_comment_no_rating), "1/5", "2/5", "3/5",
                    "4/5", "5/5"};
            new MaterialAlertDialogBuilder(requireContext())
                    .setTitle(R.string.torrent_comment_rating)
                    .setSingleChoiceItems(options, rating[0],
                            (choice, value) -> {
                                rating[0] = value;
                                chooseRating.setText(options[value]);
                                choice.dismiss();
                            })
                    .show();
        });
        form.addView(chooseRating);
        var dialog = new MaterialAlertDialogBuilder(requireContext())
                             .setTitle(R.string.torrent_comment_add)
                             .setView(form)
                             .setNegativeButton(android.R.string.cancel, null)
                             .setPositiveButton(R.string.torrent_comment_publish, null)
                             .create();
        dialog.setOnShowListener(ignored
                -> dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE)
                        .setOnClickListener(view -> {
                            try {
                                String command = TorrentControl.comment(tunnel, id, rating[0],
                                        String.valueOf(owner.getText()).trim(),
                                        String.valueOf(text.getText()).trim());
                                if (busy)
                                    return;
                                dialog.dismiss();
                                request(command,
                                        result
                                        -> request("TORRENT-DETAIL " + tunnel + " " + id,
                                                next -> comments(id, next)));
                            } catch (IllegalArgumentException error) {
                                textLayout.setError(error.getMessage());
                            }
                        }));
        dialog.show();
    }
}
