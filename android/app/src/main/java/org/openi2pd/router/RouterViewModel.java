/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterViewModel.
 *
 * {@link AndroidViewModel} that owns one {@link RouterRepository} and exposes
 * its LiveData, forwarding start/stop/reload/log, reseed, runtime log-level and
 * torrent commands. All public methods are main-thread safe, first-launch
 * auto-start consults persisted run state, and {@code onCleared} releases the
 * service binding.
 *
 */

package org.openi2pd.router;

import android.app.Application;

import androidx.lifecycle.AndroidViewModel;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;

import java.util.List;

/**
 * Activity/Fragment-facing state holder.
 *
 * <p>All public methods are main-thread safe. The native calls happen in
 * {@link RouterService}'s worker; this class only observes LiveData and sends
 * binder/intent commands.</p>
 */
public class RouterViewModel extends AndroidViewModel {
    private final RouterRepository repository;
    public record TorrentImportResult(int tunnel, String response) {}
    private final MutableLiveData<TorrentImportResult> torrentImportResult =
            new MutableLiveData<>();
    private boolean importingTorrent;

    public RouterViewModel(Application application) {
        super(application);
        repository = new RouterRepository(application);
        repository.bind();
    }

    public LiveData<RouterSnapshot> getSnapshot() {
        return repository.getSnapshot();
    }

    public LiveData<List<LogEntry>> getLogs() {
        return repository.getLogs();
    }

    public LiveData<Boolean> getServiceConnected() {
        return repository.getServiceConnected();
    }

    /** Pause/resume service observation when the activity leaves the foreground. */
    public void setUiVisible(boolean visible) {
        repository.setUiVisible(visible);
    }

    public void refresh() {
        repository.refresh();
    }

    public void torrentCommand(String request, java.util.function.Consumer<String> callback) {
        repository.torrentCommand(request, callback);
    }

    public void readAddressBook(java.util.function.Consumer<String> callback) {
        repository.readAddressBook(callback);
    }

    public void readBlockedAddresses(java.util.function.Consumer<String> callback) {
        repository.readBlockedAddresses(callback);
    }

    public void blockAddress(String name, java.util.function.Consumer<Boolean> callback) {
        repository.blockAddress(name, callback);
    }

    public void unblockAddress(String name, java.util.function.Consumer<Boolean> callback) {
        repository.unblockAddress(name, callback);
    }

    public void removeAddress(String name, java.util.function.Consumer<Boolean> callback) {
        repository.removeAddress(name, callback);
    }

    public LiveData<TorrentImportResult> getTorrentImportResult() {
        return torrentImportResult;
    }

    public boolean isImportingTorrent() {
        return importingTorrent;
    }

    public void clearTorrentImportResult() {
        torrentImportResult.setValue(null);
    }

    public void importTorrent(int tunnel, android.net.Uri document) {
        if (importingTorrent)
            return;
        importingTorrent = true;
        torrentImportResult.setValue(null);
        repository.importTorrent(tunnel, document, response -> {
            importingTorrent = false;
            torrentImportResult.setValue(new TorrentImportResult(tunnel, response));
        });
    }

    public record SafCreation(boolean running, long bytes, byte[] metadata, String error) {}
    private final MutableLiveData<SafCreation> safCreation = new MutableLiveData<>();
    public LiveData<SafCreation> getSafCreation() {
        return safCreation;
    }
    public void createSafTorrent(android.net.Uri source, boolean tree, int pieceLength,
            String[] trackers, boolean privateTorrent) {
        SafCreation old = safCreation.getValue();
        if (old != null && old.running())
            return;
        safCreation.setValue(new SafCreation(true, 0, null, null));
        repository.createSafTorrent(source, tree, pieceLength, trackers, privateTorrent,
                bytes
                -> safCreation.setValue(new SafCreation(true, bytes, null, null)),
                metadata
                -> safCreation.setValue(new SafCreation(false, 0, metadata,
                        metadata == null
                                ? getApplication().getString(
                                        R.string.torrent_creation_failed_unknown)
                                : null)));
    }
    public void cancelSafCreation() {
        repository.cancelSafCreation();
    }
    public void exportTorrent(android.net.Uri target, java.util.function.Consumer<Boolean> result) {
        SafCreation state = safCreation.getValue();
        if (state == null || state.metadata() == null) {
            result.accept(false);
            return;
        }
        repository.exportTorrent(target, state.metadata(), result);
    }

    /** Persist or release a SAF payload grant away from the UI thread. */
    public void selectPayloadTree(String tunnel, String hash, android.net.Uri tree,
            java.util.function.Consumer<Boolean> callback) {
        repository.selectPayloadTree(tunnel, hash, tree, callback);
    }

    public void detachPayload(String tunnel, String hash,
            java.util.function.Consumer<Boolean> callback) {
        repository.detachPayload(tunnel, hash, callback);
    }

    public void startRouter() {
        repository.startRouter();
    }

    public void stopRouter() {
        repository.stopRouter();
    }

    public void restartRouter() {
        repository.restartRouter();
    }

    public void beginGracefulStop() {
        repository.beginGracefulStop();
    }

    public void cancelGracefulStop() {
        repository.cancelGracefulStop();
    }

    public void reloadTunnels() {
        repository.reloadTunnels();
    }

    /** Request a bounded, non-blocking NetDb reseed; the callback runs on the main thread. */
    public void requestReseed(java.util.function.Consumer<String> callback) {
        repository.requestReseed(callback);
    }

    /** Bounded reseed phase/attempt/error record; empty when the router is stopped. */
    public void readReseedStatus(java.util.function.Consumer<String> callback) {
        repository.readReseedStatus(callback);
    }

    public void readTunnelServices(java.util.function.Consumer<String> callback) {
        repository.readTunnelServices(callback);
    }

    public void tunnelCommand(
            String name, boolean start, java.util.function.Consumer<String> callback) {
        repository.tunnelCommand(name, start, callback);
    }

    public void addAddressChecked(
            String name, String destination, boolean replace,
            java.util.function.Consumer<String> callback) {
        repository.addAddressChecked(name, destination, replace, callback);
    }

    public void readSubscriptions(java.util.function.Consumer<String> callback) {
        repository.readSubscriptions(callback);
    }

    public void subscriptionCommand(
            String url, boolean add, java.util.function.Consumer<String> callback) {
        repository.subscriptionCommand(url, add, callback);
    }

    public void banCommand(String hash, boolean ban, java.util.function.Consumer<String> callback) {
        repository.banCommand(hash, ban, callback);
    }

    /** Apply a runtime log level to the running logger; the file is written separately. */
    public void applyRuntimeLogLevel(String level, java.util.function.Consumer<Boolean> callback) {
        repository.applyRuntimeLogLevel(level, callback);
    }

    public void clearLogs() {
        repository.clearLogs();
    }

    /**
     * First-launch policy: auto-start only before any run state has been
     * persisted. The Activity calls this only for a genuinely fresh task
     * (savedInstanceState == null), so rotation cannot respawn a daemon and a
     * router stopped explicitly (or with the Quick Settings tile) stays
     * stopped across task recreation and process restarts.
     */
    public void startOnFirstLaunchIfNeeded() {
        if (repository.shouldStartOnFirstLaunch()) {
            repository.startRouter();
        }
    }

    /** Optional; the UI may instead filter its adapter locally. */
    public void setLogFilterLevel(String level) {
        repository.setLogFilterLevel(level);
    }

    @Override
    protected void onCleared() {
        repository.close();
        super.onCleared();
    }
}
