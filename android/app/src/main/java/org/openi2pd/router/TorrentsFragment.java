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
    /** Highest comment record offset TORRENT-DETAIL accepts (0..255). */
    private static final int MAX_COMMENT_OFFSET = 255;
    /** Records a torrent retains, including hidden entries. */
    private static final int MAX_COMMENT_TOTAL = 256;
    private RouterViewModel model;
    private LinearLayout rows;
    private TextView status;
    private MaterialButton bulkStart, bulkStop, bulkReannounce, bulkRemove;
    private boolean busy;
    private int tunnel;
    private int generation;
    private int importTunnel;
    private String storageTunnel = "", storageHash = "", currentTunnelName = "";
    private final ActivityResultLauncher<android.net.Uri> selectPayloadTree =
            registerForActivityResult(new ActivityResultContracts.OpenDocumentTree(), uri -> {
                if (uri == null || !isAdded())
                    return;
                final int expected = generation;
                // The grant map commit and the provider round trip run on the
                // service's SAF worker; the fragment only renders the typed
                // result and ignores it after its view is gone.
                model.selectPayloadTree(storageTunnel, storageHash, uri, saved -> {
                    if (expected != generation || status == null || !isAdded())
                        return;
                    status.setText(saved ? R.string.torrent_storage_selected
                                         : R.string.torrent_storage_permission_required);
                });
            });
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

    private final ActivityResultLauncher<String[]> createDocumentSource =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null)
                    configureSafCreation(uri, false);
            });
    private final ActivityResultLauncher<android.net.Uri> createTreeSource =
            registerForActivityResult(new ActivityResultContracts.OpenDocumentTree(), uri -> {
                if (uri != null)
                    configureSafCreation(uri, true);
            });
    private final ActivityResultLauncher<String> exportMetadata = registerForActivityResult(
            new ActivityResultContracts.CreateDocument("application/x-bittorrent"), uri -> {
                if (uri != null)
                    model.exportTorrent(uri, success -> {
                        if (status != null)
                            status.setText(success ? R.string.torrent_exported
                                                   : R.string.torrent_export_failed);
                    });
            });

    @Override
    public void onCreate(@Nullable Bundle state) {
        super.onCreate(state);
        if (state != null) {
            tunnel = state.getInt("torrent_tunnel", 0);
            importTunnel = state.getInt("torrent_import_tunnel", tunnel);
            storageTunnel = state.getString("torrent_storage_tunnel", "");
            storageHash = state.getString("torrent_storage_hash", "");
            currentTunnelName = state.getString("torrent_current_tunnel_name", "");
        }
    }

    @Override
    public void onSaveInstanceState(@NonNull Bundle state) {
        super.onSaveInstanceState(state);
        state.putInt("torrent_tunnel", tunnel);
        state.putInt("torrent_import_tunnel", importTunnel);
        state.putString("torrent_storage_tunnel", storageTunnel);
        state.putString("torrent_storage_hash", storageHash);
        state.putString("torrent_current_tunnel_name", currentTunnelName);
    }

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
        button(content, R.string.torrent_refresh, ignored -> discover());
        button(content, R.string.torrent_add_magnet, ignored -> addMagnet());
        bulkStart = button(content, R.string.torrent_start_all,
                ignored -> sendBulk(TorrentControl.Bulk.START_ALL));
        bulkStop = button(content, R.string.torrent_stop_all,
                ignored -> sendBulk(TorrentControl.Bulk.STOP_ALL));
        bulkReannounce = button(content, R.string.torrent_reannounce_all,
                ignored -> sendBulk(TorrentControl.Bulk.REANNOUNCE_ALL));
        bulkRemove = button(content, R.string.torrent_remove, ignored -> removeAllTorrents());
        button(content, R.string.torrent_import, ignored -> {
            if (busy)
                return;
            importTunnel = tunnel;
            openTorrent.launch(
                    new String[] {"application/x-bittorrent", "application/octet-stream"});
        });
        button(content, R.string.torrent_dht,
                ignored -> request("TORRENT-DHT " + tunnel, this::showFields));
        button(content, R.string.torrent_create, ignored -> {
            new MaterialAlertDialogBuilder(requireContext())
                    .setTitle(R.string.torrent_create)
                    .setItems(new String[] {getString(R.string.torrent_create_private_file),
                                      getString(R.string.torrent_create_saf_document),
                                      getString(R.string.torrent_create_saf_folder),
                                      getString(R.string.torrent_export_generated),
                                      getString(R.string.torrent_cancel_saf_creation)},
                            (dialog, item) -> {
                                switch (item) {
                                case 0 -> createTorrent();
                                case 1 -> createDocumentSource.launch(new String[] {"*/*"});
                                case 2 -> createTreeSource.launch(null);
                                case 3 -> exportMetadata.launch("created.torrent");
                                case 4 -> model.cancelSafCreation();
                                }
                            })
                    .show();
        });
        button(content, R.string.torrent_creation_status,
                ignored -> request("TORRENT-SESSION " + tunnel, this::showCreation));
        setBulkEnabled(false);
        rows = new LinearLayout(requireContext());
        rows.setOrientation(LinearLayout.VERTICAL);
        content.addView(rows);
        if (state != null) {
            // Restore the selected session so a rotation or a process restart
            // does not reopen the session picker or show another tunnel's rows.
            tunnel = Math.max(0, state.getInt("torrent_tunnel", 0));
            importTunnel = Math.max(0, state.getInt("torrent_import_tunnel", tunnel));
            storageTunnel = safeTunnelName(state.getString("torrent_storage_tunnel"));
            String hash = state.getString("torrent_storage_hash");
            storageHash = hash != null && hash.matches("[0-9a-fA-F]{40}") ? hash : "";
            currentTunnelName = safeTunnelName(state.getString("torrent_current_tunnel_name"));
        } else {
            storageTunnel = "";
            storageHash = "";
            currentTunnelName = "";
        }
        return scroll;
    }

    @Override
    public void onResume() {
        super.onResume();
        // Do not re-open the session picker on every rotation/return. A
        // restored selection refreshes its session; discover() is only for a
        // first entry or after the session disappeared.
        if (currentTunnelName.isEmpty())
            discover();
        else
            refresh();
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle state) {
        super.onViewCreated(view, state);
        model.getSafCreation().observe(getViewLifecycleOwner(), creation -> {
            if (creation == null)
                return;
            if (creation.running())
                status.setText(getString(R.string.torrent_creation_progress, creation.bytes()));
            else if (creation.error() != null)
                status.setText(creation.error());
            else
                status.setText(R.string.torrent_metadata_ready);
        });
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
        bulkStart = bulkStop = bulkReannounce = bulkRemove = null;
        super.onDestroyView();
    }

    private MaterialButton button(LinearLayout parent, int label, View.OnClickListener action) {
        MaterialButton button = UiFactory.button(requireContext());
        button.setText(label);
        button.setOnClickListener(action);
        parent.addView(button);
        return button;
    }

    private void sendBulk(TorrentControl.Bulk action) {
        if (busy)
            return;
        try {
            String command = TorrentControl.bulk(tunnel, action);
            request(command, ignored -> refresh());
        } catch (IllegalArgumentException error) {
            if (status != null)
                status.setText(getString(R.string.torrent_error, errorText(error)));
        }
    }

    private void setBulkEnabled(boolean enabled) {
        if (bulkStart != null)
            bulkStart.setEnabled(enabled);
        if (bulkStop != null)
            bulkStop.setEnabled(enabled);
        if (bulkReannounce != null)
            bulkReannounce.setEnabled(enabled);
        if (bulkRemove != null)
            bulkRemove.setEnabled(enabled);
    }

    /**
     * Session-wide removal, confirmed like a single-torrent removal. The two
     * variants map to the closed TORRENT-REMOVE-ALL verb; deleting local data
     * keeps its separate destructive confirmation.
     */
    private void removeAllTorrents() {
        if (busy || status == null)
            return;
        String session =
                currentTunnelName.isEmpty() ? getString(R.string.torrent_title) : currentTunnelName;
        String[] options = {
                getString(R.string.torrent_remove), getString(R.string.torrent_remove_files)};
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.torrent_remove)
                .setItems(options, (dialog, choice) -> confirmRemoveAll(choice == 0, session))
                .show();
    }

    private void confirmRemoveAll(boolean keepData, String session) {
        int message =
                keepData ? R.string.torrent_remove_confirm : R.string.torrent_remove_files_confirm;
        TorrentControl.Bulk bulk =
                keepData ? TorrentControl.Bulk.REMOVE_ALL : TorrentControl.Bulk.REMOVE_ALL_FILES;
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.torrent_remove)
                .setMessage(keepData ? getString(message) : getString(message, session))
                .setNegativeButton(R.string.action_cancel, null)
                .setPositiveButton(
                        keepData ? R.string.torrent_remove : R.string.torrent_remove_files,
                        (confirmation, which) -> sendBulk(bulk))
                .show();
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
                status.setText(getString(R.string.torrent_error, errorText(error)));
            }
        });
    }

    /**
     * True when a new control request may be started. A dialog action that
     * cannot be sent is reported instead of being dropped silently, so the
     * dialog never implies a moderation decision the engine never received.
     */
    private boolean ready() {
        if (!busy)
            return true;
        if (status != null)
            status.setText(R.string.torrent_busy);
        return false;
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
                currentTunnelName = "";
                rows.removeAllViews();
                setBulkEnabled(false);
                status.setText(R.string.torrent_no_session);
                return;
            }
            if (count == 1) {
                tunnel = 0;
                currentTunnelName =
                        safeTunnelName(fields.getOrDefault("torrent.tunnel.0.name", ""));
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
                                currentTunnelName = safeTunnelName(names[which]);
                                refresh();
                            })
                    .show();
        });
    }

    private void refresh() {
        request("TORRENTS " + tunnel + " 200", fields -> {
            rows.removeAllViews();
            int count = number(fields, "torrent.count", 200);
            currentTunnelName = safeTunnelName(fields.getOrDefault("torrent.tunnel.name", ""));
            setBulkEnabled(count > 0 && !busy);
            status.setText(currentTunnelName.isEmpty()
                            ? getResources().getQuantityString(
                                      R.plurals.torrent_count, count, count)
                            : getString(R.string.torrent_session_count, currentTunnelName, count));
            for (int i = 0; i < count; ++i) {
                TorrentRow row = TorrentRow.from(fields, "torrent." + i + ".");
                String badge = getString(R.string.torrent_private_badge);
                MaterialButton button = UiFactory.button(requireContext());
                button.setText(row.displayLabel(badge));
                UiFactory.applyRowSemantics(button, row.accessibilityLabel(badge));
                button.setOnClickListener(view -> actions(row));
                rows.addView(button);
            }
        });
    }

    private void actions(TorrentRow selection) {
        String id = selection.id();
        String magnet = selection.magnet();
        String name = selection.name();
        String hash = selection.hash();
        String[] labels = {getString(R.string.torrent_details), getString(R.string.torrent_pause),
                getString(R.string.torrent_resume), getString(R.string.torrent_share),
                getString(R.string.torrent_remove), getString(R.string.torrent_trackers),
                getString(R.string.torrent_comments), getString(R.string.torrent_priorities),
                getString(R.string.torrent_recheck), getString(R.string.torrent_payload_action),
                getString(R.string.torrent_meta), getString(R.string.torrent_remove_files),
                getString(R.string.action_start), getString(R.string.action_stop)};
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(selection.name())
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
                                        .setNegativeButton(R.string.action_cancel, null)
                                        .setPositiveButton(R.string.torrent_remove,
                                                (confirmation, which)
                                                        -> request("TORRENT-REMOVE " + tunnel + " "
                                                                        + id,
                                                                ignored -> {
                                                                    detachStorage(hash);
                                                                    refresh();
                                                                }))
                                        .show();
                            case 5 ->
                                request("TORRENT-DETAIL " + tunnel + " " + id,
                                        fields -> trackers(id, fields));
                            case 6 -> {
                                try {
                                    request(TorrentControl.commentPage(
                                                    tunnel, id, 0, TorrentControl.COMMENT_PAGE),
                                            fields -> comments(id, fields));
                                } catch (IllegalArgumentException error) {
                                    if (status != null)
                                        status.setText(getString(
                                                R.string.torrent_error, errorText(error)));
                                }
                            }
                            case 7 ->
                                request("TORRENT-DETAIL " + tunnel + " " + id,
                                        fields -> priorities(id, fields));
                            case 8 -> {
                                try {
                                    request(TorrentControl.recheck(tunnel, id),
                                            ignored -> refresh());
                                } catch (IllegalArgumentException error) {
                                    status.setText(
                                            getString(R.string.torrent_error, errorText(error)));
                                }
                            }
                            case 9 ->
                                request("TORRENT-DETAIL " + tunnel + " " + id, fields -> {
                                    storageTunnel = currentTunnelName;
                                    storageHash = fields.getOrDefault("detail.hash", "");
                                    if (storageTunnel.isEmpty()
                                            || !storageHash.matches("[0-9a-fA-F]{40}"))
                                        return;
                                    new MaterialAlertDialogBuilder(requireContext())
                                            .setMessage(getString(R.string.torrent_payload_message,
                                                    TorrentSafStorage.status(
                                                            storageTunnel, storageHash)))
                                            .setNegativeButton(R.string.action_cancel, null)
                                            .setPositiveButton(R.string.torrent_select_directory,
                                                    (confirmation, which)
                                                            -> selectPayloadTree.launch(null))
                                            .show();
                                });
                            case 10 -> editMetadata(id);
                            case 11 ->
                                new MaterialAlertDialogBuilder(requireContext())
                                        .setMessage(getString(
                                                R.string.torrent_remove_files_confirm, name))
                                        .setNegativeButton(R.string.action_cancel, null)
                                        .setPositiveButton(R.string.torrent_remove_files,
                                                (confirmation, which) -> {
                                                    try {
                                                        request(TorrentControl.remove(
                                                                        tunnel, id, true),
                                                                ignored -> refresh());
                                                    } catch (IllegalArgumentException error) {
                                                        status.setText(
                                                                getString(R.string.torrent_error,
                                                                        errorText(error)));
                                                    }
                                                })
                                        .show();
                            // 12/13: explicit single-torrent lifecycle verbs. They
                            // are typed TORRENT-START/TORRENT-STOP commands built by
                            // TorrentControl.single(); pause/resume above remain the
                            // state toggle.
                            case 12 -> single(id, TorrentControl.Action.START);
                            case 13 -> single(id, TorrentControl.Action.STOP);
                            default -> {
                            }
                            }
                        })
                .show();
    }

    private void single(String id, TorrentControl.Action action) {
        try {
            request(TorrentControl.single(tunnel, id, action), ignored -> refresh());
        } catch (IllegalArgumentException error) {
            if (status != null)
                status.setText(getString(R.string.torrent_error, errorText(error)));
        }
    }

    private void editMetadata(String id) {
        request("TORRENT-DETAIL " + tunnel + " " + id, fields -> {
            if (!isAdded() || status == null)
                return;
            String currentComment = fields.getOrDefault("detail.comment", "");
            String currentCreatedBy = fields.getOrDefault("detail.createdBy", "");
            LinearLayout form = new LinearLayout(requireContext());
            form.setOrientation(LinearLayout.VERTICAL);
            int padding = getResources().getDimensionPixelSize(R.dimen.margin_medium);
            form.setPadding(padding, padding, padding, 0);
            TextInputLayout commentLayout = new TextInputLayout(requireContext());
            commentLayout.setHint(R.string.torrent_meta_comment);
            commentLayout.setHelperText(getString(R.string.torrent_meta_hint));
            TextInputEditText comment = new TextInputEditText(requireContext());
            comment.setFilters(new android.text.InputFilter[] {
                    new android.text.InputFilter.LengthFilter(1024)});
            comment.setText(currentComment);
            commentLayout.addView(comment);
            form.addView(commentLayout);
            TextInputLayout createdLayout = new TextInputLayout(requireContext());
            createdLayout.setHint(R.string.torrent_meta_created_by);
            TextInputEditText createdBy = new TextInputEditText(requireContext());
            createdBy.setFilters(new android.text.InputFilter[] {
                    new android.text.InputFilter.LengthFilter(255)});
            createdBy.setText(currentCreatedBy);
            createdLayout.addView(createdBy);
            form.addView(createdLayout);
            new MaterialAlertDialogBuilder(requireContext())
                    .setTitle(R.string.torrent_meta)
                    .setView(form)
                    .setNegativeButton(R.string.action_cancel, null)
                    .setPositiveButton(android.R.string.ok,
                            (dialog, which) -> {
                                try {
                                    String commentText = String.valueOf(comment.getText());
                                    String createdText = String.valueOf(createdBy.getText());
                                    boolean commentChanged = !commentText.equals(currentComment);
                                    boolean createdChanged = !createdText.equals(currentCreatedBy);
                                    if (!commentChanged && !createdChanged)
                                        return;
                                    if (commentChanged && commentText.isEmpty()) {
                                        status.setText(getString(R.string.torrent_meta_failed));
                                        return;
                                    }
                                    if (createdChanged && createdText.isEmpty()) {
                                        status.setText(getString(R.string.torrent_meta_failed));
                                        return;
                                    }
                                    if (commentChanged) {
                                        request(TorrentControl.metaComment(tunnel, id, commentText),
                                                ignored
                                                -> finishMetadataEdit(createdChanged, id,
                                                        createdText, currentCreatedBy));
                                    } else {
                                        finishMetadataEdit(true, id, createdText, currentCreatedBy);
                                    }
                                } catch (IllegalArgumentException error) {
                                    status.setText(getString(R.string.torrent_meta_failed));
                                }
                            })
                    .show();
        });
    }

    private void finishMetadataEdit(
            boolean createdChanged, String id, String createdText, String currentCreatedBy) {
        if (!createdChanged || createdText.equals(currentCreatedBy)) {
            refresh();
            return;
        }
        try {
            request(TorrentControl.metaCreatedBy(tunnel, id, createdText), ignored -> refresh());
        } catch (IllegalArgumentException error) {
            status.setText(getString(R.string.torrent_meta_failed));
        }
    }

    private void trackers(String id, Map<String, String> fields) {
        if ("1".equals(fields.get("detail.private"))) {
            status.setText(R.string.torrent_private_trackers_fixed);
            return;
        }
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
                                                                        errorText(error)));
                                                    }
                                                })
                                        .setNeutralButton(R.string.action_cancel, null)
                                        .show())
                .setPositiveButton(
                        R.string.torrent_tracker_add, (dialog, which) -> editTracker(id, null))
                .setNegativeButton(R.string.action_cancel, null)
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
                             .setNegativeButton(R.string.action_cancel, null)
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
                                layout.setError(errorText(error));
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
                             .setNegativeButton(R.string.action_cancel, null)
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
                                layout.setError(errorText(error));
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
                            fields.getOrDefault(
                                    "detail.wanted", fields.getOrDefault("detail.total", "0")),
                            fields.getOrDefault("detail.downRate", "0"),
                            fields.getOrDefault("detail.upRate", "0")))
                    .append('\n')
                    .append("1".equals(fields.get("detail.private"))
                                    ? getString(R.string.torrent_private_summary_yes)
                                    : getString(R.string.torrent_private_summary_no))
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

    private void configureSafCreation(android.net.Uri source, boolean tree) {
        LinearLayout options = new LinearLayout(requireContext());
        options.setOrientation(LinearLayout.VERTICAL);
        TextInputLayout trackersLayout = new TextInputLayout(requireContext());
        trackersLayout.setHint(R.string.torrent_trackers_hint);
        TextInputEditText trackers = new TextInputEditText(trackersLayout.getContext());
        trackers.setMinLines(2);
        trackers.setFilters(
                new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(8192)});
        trackersLayout.addView(trackers);
        options.addView(trackersLayout);
        var privacy = new com.google.android.material.checkbox.MaterialCheckBox(requireContext());
        privacy.setText(R.string.torrent_private_hint);
        options.addView(privacy);
        var size = new com.google.android.material.button.MaterialButton(requireContext());
        int[] pieceLength = {1024 * 1024};
        size.setText(getString(R.string.torrent_piece_size_value, 1024));
        size.setOnClickListener(view
                -> new MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.torrent_piece_size_title)
                        .setItems(new String[] {"256 KiB", "512 KiB", "1 MiB", "2 MiB", "4 MiB"},
                                (dialog, selected) -> {
                                    pieceLength[0] = 262144 << selected;
                                    size.setText(getString(R.string.torrent_piece_size_value,
                                            pieceLength[0] / 1024));
                                })
                        .show());
        options.addView(size);
        var dialog = new MaterialAlertDialogBuilder(requireContext())
                             .setTitle(R.string.torrent_create_saf_title)
                             .setMessage(R.string.torrent_create_saf_message)
                             .setView(options)
                             .setNegativeButton(R.string.action_cancel, null)
                             .setPositiveButton(R.string.torrent_create, null)
                             .create();
        dialog.setOnShowListener(ignored
                -> dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE)
                        .setOnClickListener(view -> {
                            try {
                                String value = String.valueOf(trackers.getText()).trim();
                                String[] addresses =
                                        value.isEmpty() ? new String[0] : value.split("\\R");
                                if (addresses.length > 32
                                        || (privacy.isChecked() && addresses.length == 0))
                                    throw new IllegalArgumentException(
                                            getString(R.string.torrent_private_tracker_required));
                                for (int i = 0; i < addresses.length; ++i) {
                                    addresses[i] = addresses[i].trim();
                                    // A SAF creation has no engine torrent id yet,
                                    // so the creation-scoped validator applies the
                                    // same I2P-only URL rules without one.
                                    TorrentControl.creationTracker(addresses[i]);
                                }
                                model.createSafTorrent(source, tree, pieceLength[0], addresses,
                                        privacy.isChecked());
                                dialog.dismiss();
                            } catch (IllegalArgumentException error) {
                                trackersLayout.setError(errorText(error));
                            }
                        }));
        dialog.show();
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
                             .setNegativeButton(R.string.action_cancel, null)
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
                                layout.setError(errorText(error));
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
                                    .setNegativeButton(R.string.action_cancel, null)
                                    .show();
                        })
                .setNegativeButton(R.string.action_cancel, null)
                .show();
    }

    private String priorityName(String priority) {
        return getString("0".equals(priority)  ? R.string.torrent_priority_skip
                        : "2".equals(priority) ? R.string.torrent_priority_high
                                                 : R.string.torrent_priority_normal);
    }

    /**
     * Comment page size the daemon emits per TORRENT-DETAIL response. The
     * daemon retains up to 256 records (64 hidden) and the dialog pages
     * explicitly instead of expecting one unbounded reply.
     */
    private void comments(String id, Map<String, String> fields) {
        boolean enabled = "1".equals(fields.get("comments.enabled"))
                || "true".equals(fields.get("comments.enabled"));
        boolean privateTorrent = "1".equals(fields.get("detail.private"));
        int offset = number(fields, "comments.offset", MAX_COMMENT_OFFSET);
        int count = number(fields, "comments.count", TorrentControl.COMMENT_PAGE);
        int total = number(fields, "comments.total", MAX_COMMENT_TOTAL);
        int hiddenCount = number(fields, "comments.hidden", MAX_COMMENT_TOTAL);
        int localCount = number(fields, "comments.local", MAX_COMMENT_TOTAL);
        boolean truncated = "1".equals(fields.get("comments.truncated"))
                || "true".equals(fields.get("comments.truncated"));
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
        TextView paging = new TextView(requireContext());
        paging.setText(getString(R.string.torrent_comments_page,
                Integer.toString(count == 0 ? 0 : offset + 1), Integer.toString(offset + count),
                Integer.toString(total), Integer.toString(hiddenCount),
                Integer.toString(localCount)));
        content.addView(paging);
        if (privateTorrent) {
            TextView privateNotice = new TextView(requireContext());
            privateNotice.setText(R.string.torrent_comments_private);
            content.addView(privateNotice);
        }
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
        if (!privateTorrent) {
            button(content,
                    enabled ? R.string.torrent_comments_disable : R.string.torrent_comments_enable,
                    ignored -> {
                        // A dropped moderation request would leave the dialog
                        // showing a state the engine does not have.
                        if (!ready())
                            return;
                        if (enabled) {
                            dialog.dismiss();
                            request("TORRENT-COMMENTS " + tunnel + " " + id + " OFF",
                                    result -> refresh());
                        } else {
                            new MaterialAlertDialogBuilder(requireContext())
                                    .setMessage(R.string.torrent_comments_privacy)
                                    .setNegativeButton(R.string.action_cancel, null)
                                    .setPositiveButton(R.string.torrent_comments_enable,
                                            (confirmation, button) -> {
                                                if (!ready())
                                                    return;
                                                dialog.dismiss();
                                                request("TORRENT-COMMENTS " + tunnel + " " + id
                                                                + " ON",
                                                        result -> refresh());
                                            })
                                    .show();
                        }
                    });
        }
        if (enabled && !privateTorrent)
            button(content, R.string.torrent_comment_add, ignored -> {
                if (!ready())
                    return;
                dialog.dismiss();
                addComment(id);
            });
        int shown = Math.min(count, TorrentControl.COMMENT_PAGE);
        for (int i = 0; i < shown; ++i) {
            String prefix = "comment." + i + ".";
            boolean hidden = "1".equals(fields.get(prefix + "hidden"))
                    || "true".equals(fields.get(prefix + "hidden"));
            boolean local = "1".equals(fields.get(prefix + "local"))
                    || "true".equals(fields.get(prefix + "local"));
            String localId = fields.getOrDefault(prefix + "id", "");
            try {
                if (Long.parseLong(localId) <= 0)
                    throw new IllegalArgumentException("Invalid comment ID");
            } catch (NumberFormatException error) {
                throw new IllegalArgumentException("Invalid comment ID");
            }
            MaterialButton entry = UiFactory.button(requireContext());
            String owner = fields.getOrDefault(prefix + "owner", "");
            String rating = fields.getOrDefault(prefix + "rating", "0");
            StringBuilder label = new StringBuilder();
            if (hidden)
                label.append(getString(R.string.torrent_comment_hidden)).append(" \u00b7 ");
            if (local)
                label.append(getString(R.string.torrent_comment_local)).append(" \u00b7 ");
            label.append(owner.isEmpty() ? getString(R.string.torrent_comment_anonymous) : owner)
                    .append(" \u00b7 ")
                    .append("0".equals(rating) ? getString(R.string.torrent_comment_no_rating)
                                                 : rating + "/5");
            String when = commentAge(fields.getOrDefault(prefix + "time", "0"));
            if (!when.isEmpty())
                label.append(" \u00b7 ").append(when);
            // Hidden records keep the same preview as visible ones: the local
            // store already ships owner, rating, time and text, and an
            // operator needs them to decide whether to unhide.
            label.append("\n").append(fields.getOrDefault(prefix + "text", ""));
            entry.setText(label.toString());
            final boolean unhide = hidden;
            entry.setOnClickListener(ignored
                    -> new MaterialAlertDialogBuilder(requireContext())
                            .setMessage(unhide ? R.string.torrent_comment_unhide_confirm
                                               : R.string.torrent_comment_hide_confirm)
                            .setNegativeButton(R.string.action_cancel, null)
                            .setPositiveButton(unhide ? R.string.torrent_comment_unhide
                                                      : R.string.torrent_comment_hide,
                                    (confirmation, button) -> {
                                        if (!ready())
                                            return;
                                        try {
                                            String command = TorrentControl.commentId(
                                                    tunnel, id, localId, !unhide);
                                            dialog.dismiss();
                                            // Keep the operator on the same page
                                            // after a moderation decision.
                                            request(command,
                                                    result
                                                    -> request(TorrentControl.commentPage(tunnel,
                                                                       id, offset,
                                                                       TorrentControl.COMMENT_PAGE),
                                                            refreshed -> comments(id, refreshed)));
                                        } catch (IllegalArgumentException error) {
                                            status.setText(getString(
                                                    R.string.torrent_error, errorText(error)));
                                        }
                                    })
                            .show());
            content.addView(entry);
        }
        if (shown == 0) {
            TextView empty = new TextView(requireContext());
            empty.setText(R.string.torrent_comments_empty_page);
            content.addView(empty);
        }
        int next = offset + shown;
        if (truncated && shown > 0 && next > offset && next < total && next <= MAX_COMMENT_OFFSET) {
            button(content, R.string.torrent_comments_more, ignored -> {
                if (!ready())
                    return;
                try {
                    String command = TorrentControl.commentPage(
                            tunnel, id, next, TorrentControl.COMMENT_PAGE);
                    dialog.dismiss();
                    request(command, page -> comments(id, page));
                } catch (IllegalArgumentException error) {
                    status.setText(getString(R.string.torrent_error, errorText(error)));
                }
            });
        }
        dialog.show();
    }

    /** Bounded, localized age for a comment epoch-seconds field. */
    private String commentAge(String seconds) {
        try {
            long epoch = Long.parseLong(seconds);
            // Reject non-positive and implausible times: a hostile or broken
            // peer-supplied record must not overflow the millisecond value or
            // render as a date decades away.
            if (epoch <= 0 || epoch > 4102444800L)
                return "";
            return android.text.format.DateUtils
                    .getRelativeTimeSpanString(epoch * 1000L, System.currentTimeMillis(),
                            android.text.format.DateUtils.MINUTE_IN_MILLIS)
                    .toString();
        } catch (NumberFormatException error) {
            return "";
        }
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
        MaterialButton chooseRating = UiFactory.button(requireContext());
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
                             .setNegativeButton(R.string.action_cancel, null)
                             .setPositiveButton(R.string.torrent_comment_publish, null)
                             .create();
        dialog.setOnShowListener(ignored
                -> dialog.getButton(android.content.DialogInterface.BUTTON_POSITIVE)
                        .setOnClickListener(view -> {
                            try {
                                String command = TorrentControl.comment(tunnel, id, rating[0],
                                        String.valueOf(owner.getText()).trim(),
                                        String.valueOf(text.getText()).trim());
                                // Keep the form open with a visible reason
                                // instead of discarding the composed comment.
                                if (!ready())
                                    return;
                                dialog.dismiss();
                                request(command,
                                        result
                                        -> request(TorrentControl.commentPage(tunnel, id, 0,
                                                           TorrentControl.COMMENT_PAGE),
                                                refreshed -> comments(id, refreshed)));
                            } catch (IllegalArgumentException error) {
                                textLayout.setError(errorText(error));
                            }
                        }));
        dialog.show();
    }

    /**
     * A validation failure that names a resource is already localized; a wire
     * diagnostic keeps its daemon-provided English text.
     */
    private String errorText(IllegalArgumentException error) {
        if (error instanceof TorrentControl.UserMessageException typed) {
            return getString(typed.messageRes());
        }
        return error.getMessage();
    }

    private String safeTunnelName(String value) {
        if (value == null || value.isEmpty() || value.length() > 1024)
            return "";
        for (int i = 0; i < value.length(); ++i)
            if (Character.isISOControl(value.charAt(i)))
                return "";
        return value;
    }

    private void detachStorage(String hash) {
        if (hash == null || !hash.matches("[0-9a-fA-F]{40}") || currentTunnelName.isEmpty())
            return;
        // The torrrent is already gone; a failed grant release is reported but
        // never resurrects the row. The operation is off the main thread.
        final int expected = generation;
        model.detachPayload(currentTunnelName, hash, released -> {
            if (expected != generation || status == null || !isAdded())
                return;
            status.setText(released ? R.string.torrent_storage_detached
                                    : R.string.torrent_storage_detach_failed);
        });
    }
}
