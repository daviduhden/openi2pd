/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterViewModel.
 *
 * {@link AndroidViewModel} that owns one {@link RouterRepository} and exposes
 * its LiveData, forwarding start/stop/reload/log and torrent commands. All
 * public methods are main-thread safe, first-launch auto-start consults
 * persisted run state, and {@code onCleared} releases the service binding.
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
