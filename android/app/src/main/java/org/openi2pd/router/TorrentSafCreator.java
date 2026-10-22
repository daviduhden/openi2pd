/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentSafCreator.
 *
 * Brokers bounded SAF enumeration and incremental hashing. Native C++ owns
 * metadata serialization. Source documents are never converted into paths.
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

final class TorrentSafCreator {
    private record Source(Uri uri, String path, long length) {}
    private static final int MAX_FILES = 10000;

    private static final long MAX_BYTES = 64L * 1024 * 1024 * 1024;
    private TorrentSafCreator() {
    }

    static byte[] create(ContentResolver resolver, Uri uri, boolean tree, CancellationSignal cancel,
            LongConsumer progress, int pieceLength, String[] trackers, boolean privateTorrent)
            throws Exception {
        if (uri == null || !"content".equals(uri.getScheme()))
            throw new IOException("Invalid source");
        if (pieceLength < 16384 || pieceLength > 4 * 1024 * 1024
                || (pieceLength & (pieceLength - 1)) != 0 || trackers == null
                || trackers.length > 32)
            throw new IOException("Invalid creation options");
        List<Source> files = new ArrayList<>();
        Uri root = tree ? DocumentsContract.buildDocumentUriUsingTree(
                                  uri, DocumentsContract.getTreeDocumentId(uri))
                        : uri;
        String name;
        try (Cursor cursor = resolver.query(root,
                     new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null,
                     null, cancel)) {
            if (cursor == null || !cursor.moveToFirst())
                throw new IOException("Source unavailable");
            name = safe(cursor.getString(0));
        }
        if (tree)
            enumerate(resolver, uri, DocumentsContract.getTreeDocumentId(uri), "", 0, files,
                    new HashSet<>(), cancel);
        else
            files.add(new Source(root, "", -1));
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
                cancel.setOnCancelListener(() -> {
                    try {
                        descriptor.close();
                    } catch (IOException ignored) {
                    }
                });
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
                            if (hashes.size() >= 5 * 1024 * 1024)
                                throw new IOException(
                                        "Too many pieces; select a larger piece size");
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
            cancel.setOnCancelListener(null);
            if (source.length() >= 0 && lengths[i] != source.length())
                throw new IOException("Source changed while hashing");
        }
        if (remaining != pieceLength)
            hashes.write(digest.digest());
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
            int depth, List<Source> files, HashSet<String> visited, CancellationSignal cancel)
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
                if (!names.add(name))
                    throw new IOException("Duplicate source name");
                String child = cursor.getString(0);
                String path = prefix + name;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2)))
                    enumerate(resolver, tree, child, path + "/", depth + 1, files, visited, cancel);
                else {
                    if (files.size() >= MAX_FILES)
                        throw new IOException("Too many files");
                    files.add(new Source(DocumentsContract.buildDocumentUriUsingTree(tree, child),
                            path, cursor.isNull(3) ? -1 : cursor.getLong(3)));
                }
            }
        }
    }

    private static String safe(String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                || name.length() > 255 || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0)
            throw new IOException("Invalid source name");
        for (int i = 0; i < name.length(); ++i)
            if (Character.isISOControl(name.charAt(i)))
                throw new IOException("Invalid source name");
        return name;
    }
}
