/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: TorrentSafCreator.
 *
 * Brokers bounded SAF enumeration and incremental hashing. Native C++
 * owns metadata serialization. Source documents are never converted
 * into paths.
 */
package org.openi2pd.router;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.CancellationSignal;
import android.provider.DocumentsContract;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.function.LongConsumer;

/**
 * Builds v1 metainfo from provider streams without converting a content
 * URI into a filesystem path. The caller owns the worker and
 * cancellation signal; cursors, descriptors and streams opened here are
 * closed before return.
 *
 * <p><b>Execution.</b> This performs blocking provider I/O and hashing,
 * so it must run on the torrent worker, never on the main thread.
 * Progress is reported to the caller's consumer on that same thread and
 * is throttled to roughly four updates per second; it is a hint, not a
 * completion signal.</p>
 *
 * <p><b>Bounds.</b> At most {@value #MAX_FILES} files, {@value
 * #MAX_BYTES} total source bytes, 32 tracker URLs, 32 directory levels,
 * 20000 visited directory ids, 5 MiB of accumulated piece hashes, a
 * 16 KiB..4 MiB power-of-two piece size and 255-byte UTF-8 path
 * components. Paths and provider authority text are separately bounded
 * by {@link TorrentSafCreationBudget} before application retention.
 * A source whose provider-reported length disagrees with
 * the bytes actually read fails the whole creation, so metainfo never
 * describes content that is no longer there.</p>
 *
 * <p><b>Ownership.</b> The result is serialized metadata only. It is
 * not a persisted grant: seeding the created torrent still needs a
 * selected tree or document authority, and the caller must persist that
 * separately.</p>
 */
final class TorrentSafCreator {
    private record Source(Uri uri, String path, long length) {}
    private static final int MAX_FILES = 10000;

    private static final long MAX_BYTES = 64L * 1024 * 1024 * 1024;
    private TorrentSafCreator() {
    }

    /**
     * Enumerate a document or tree in stable path order and hash the
     * byte stream across file boundaries. Provider-reported lengths,
     * when present, are checked against bytes actually read before
     * metainfo is encoded.
     *
     * <p><b>Cancellation.</b> Cancellation is observed at the next read
     * boundary; the descriptor is never closed from the signal
     * callback, because that listener runs on the cancelling thread
     * while this thread may be inside {@code read()}. A provider that
     * ignores cancellation can therefore stay blocked past the caller's
     * deadline.
     *
     * @param resolver resolver with a read grant for {@code uri}
     * @param uri selected content document or tree
     * @param tree whether {@code uri} identifies a document tree
     * @param cancel signal checked during enumeration and reading
     * @param progress receives cumulative source bytes on the calling
     *                 thread
     * @param pieceLength v1 piece size, a power of two from 16 KiB to 4
     *                    MiB
     * @param trackers tracker URLs checked by the native encoder
     * @param privateTorrent whether to mark the metainfo private
     * @return serialized metainfo, not a persistent grant to seed the
     *                    sources
     * @throws Exception if provider access, validation, hashing or
     *                   encoding fails
     */
    static byte[] create(ContentResolver resolver, Uri uri, boolean tree, CancellationSignal cancel,
            LongConsumer progress, int pieceLength, String[] trackers, boolean privateTorrent)
            throws Exception {
        if (uri == null || !"content".equals(uri.getScheme()))
            throw new IOException("Invalid source");
        TorrentSafCreationBudget.uri(uri.toString());
        if (pieceLength < 16384 || pieceLength > 4 * 1024 * 1024
                || (pieceLength & (pieceLength - 1)) != 0 || trackers == null
                || trackers.length > 32 || (privateTorrent && trackers.length == 0))
            throw new IOException("Invalid creation options");
        // Reject unusable tracker hints before provider enumeration or
        // hashing. The native encoder independently checks them again.
        for (String tracker : trackers)
            TorrentControl.creationTracker(tracker);
        List<Source> files = new ArrayList<>();
        var budget = new TorrentSafCreationBudget();
        Uri root = tree ? DocumentsContract.buildDocumentUriUsingTree(
                                  uri, DocumentsContract.getTreeDocumentId(uri))
                        : uri;
        TorrentSafCreationBudget.uri(root.toString());
        String name;
        try (Cursor cursor = resolver.query(root,
                     new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null,
                     null, cancel)) {
            if (cursor == null || !cursor.moveToFirst())
                throw new IOException("Source unavailable");
            name = safe(cursor.getString(0));
        }
        budget.document(tree ? DocumentsContract.getTreeDocumentId(uri)
                             : "document", name);
        if (tree)
            enumerate(resolver, uri, DocumentsContract.getTreeDocumentId(uri), "", 0, files,
                    new HashSet<>(), cancel, budget);
        else {
            budget.authority(root.toString());
            files.add(new Source(root, "", -1));
        }
        files.sort(Comparator.comparing(Source::path));
        if (files.isEmpty())
            throw new IOException("Empty source");
        MessageDigest digest = MessageDigest.getInstance("SHA-1");
        ByteArrayOutputStream hashes = new ByteArrayOutputStream();
        byte[] buffer = new byte[65536];
        byte[][] paths = new byte[files.size()][];
        long[] lengths = new long[files.size()];
        int remaining = pieceLength;
        long total = 0;
        long lastProgress = android.os.SystemClock.elapsedRealtime();
        for (int i = 0; i < files.size(); ++i) {
            Source source = files.get(i);
            paths[i] = source.path().getBytes(StandardCharsets.UTF_8);
            try (var descriptor = resolver.openAssetFileDescriptor(source.uri(), "r", cancel)) {
                if (descriptor == null)
                    throw new IOException("Source unavailable");
                // Never close the descriptor from the
                // CancellationSignal callback: that listener runs on
                // the thread requesting the cancel, while this thread
                // may be inside read(). The engine already refuses that
                // cross-thread descriptor close for import (fd reuse
                // could return unrelated bytes). Cancellation is
                // observed at the next throwIfCanceled()/read boundary.
                // This direct provider stream does not use the broker's
                // watchdog; a provider that ignores cancellation can
                // remain blocked after the caller's deadline.
                try (var input = descriptor.createInputStream()) {
                    for (;;) {
                        cancel.throwIfCanceled();
                        int count = input.read(buffer, 0, Math.min(buffer.length, remaining));
                        if (count == -1)
                            break;
                        if (count == 0)
                            throw new IOException("Provider stalled");
                        if (total > MAX_BYTES - count)
                            throw new IOException("Source too large");
                        total += count;
                        lengths[i] += count;
                        digest.update(buffer, 0, count);
                        remaining -= count;
                        if (remaining == 0) {
                            TorrentSafCreationBudget.hashAppend(hashes.size());
                            hashes.write(digest.digest());
                            remaining = pieceLength;
                            long now = android.os.SystemClock.elapsedRealtime();
                            if (now - lastProgress >= 250) {
                                progress.accept(total);
                                lastProgress = now;
                            }
                        }
                    }
                }
            }
            if (source.length() >= 0 && lengths[i] != source.length())
                throw new IOException("Source changed while hashing");
        }
        if (remaining != pieceLength) {
            TorrentSafCreationBudget.hashAppend(hashes.size());
            hashes.write(digest.digest());
        }
        cancel.throwIfCanceled();
        byte[][] trackerBytes = new byte[trackers.length][];
        for (int i = 0; i < trackers.length; ++i)
            trackerBytes[i] = trackers[i].getBytes(StandardCharsets.UTF_8);
        byte[] metadata = I2PD_JNI.buildTorrentMetadata(name.getBytes(StandardCharsets.UTF_8),
                paths, lengths, pieceLength, hashes.toByteArray(), trackerBytes, privateTorrent);
        if (metadata == null)
            throw new IOException("Metadata rejected");
        progress.accept(total);
        return metadata;
    }

    private static void enumerate(ContentResolver resolver, Uri tree, String id, String prefix,
            int depth, List<Source> files, HashSet<String> visited, CancellationSignal cancel,
            TorrentSafCreationBudget budget)
            throws IOException {
        cancel.throwIfCanceled();
        if (depth > 32 || !visited.add(id) || visited.size() > 20000)
            throw new IOException("Directory limit exceeded");
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        try (Cursor cursor = resolver.query(children,
                     new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                             DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                             DocumentsContract.Document.COLUMN_MIME_TYPE,
                             DocumentsContract.Document.COLUMN_SIZE},
                     null, null, null, cancel)) {
            if (cursor == null)
                throw new IOException("Directory unavailable");
            HashSet<String> names = new HashSet<>();
            while (cursor.moveToNext()) {
                cancel.throwIfCanceled();
                String name = safe(cursor.getString(1));
                String child = cursor.getString(0);
                String path = prefix + name;
                budget.document(child, path);
                if (!names.add(name))
                    throw new IOException("Duplicate source name");
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2)))
                    enumerate(resolver, tree, child, path + "/", depth + 1, files, visited,
                            cancel, budget);
                else {
                    if (files.size() >= MAX_FILES)
                        throw new IOException("Too many files");
                    Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, child);
                    budget.authority(document.toString());
                    files.add(new Source(document, path,
                            cursor.isNull(3) ? -1 : cursor.getLong(3)));
                }
            }
        }
    }

    private static String safe(String name) throws IOException {
        return TorrentSafCreationBudget.component(name);
    }
}
