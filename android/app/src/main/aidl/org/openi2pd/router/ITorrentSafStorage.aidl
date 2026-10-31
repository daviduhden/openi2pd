// See the LICENSE file at the top of the project tree for copyright and
// license details. openi2pd: bounded, app-private SAF payload
// capabilities (protocol v2).
//
// Authorization model
// -------------------
// The broker runs in its own process (":torrent_storage") under the
// application UID. The UID is the hard boundary: a foreign UID is
// rejected with SecurityException. On top of that boundary every
// capability below is bound to the exact (uid, pid) that created it, so
// a different component of the same application cannot read, write,
// finish, delete, close, revoke, cancel or retire another component's
// root, handle, operation or token. A caller's pid (not only its uid)
// is recorded by handshake(), by every operation registration and by
// every created root/handle/token.
//
// Admission
// ---------
// One broker admits at most 8 concurrent operations, 4096 open handles,
// 128 tree roots and 8 prepared export tokens. A busy, duplicate,
// exhausted, expired or wrong-owner request returns its typed failure
// (0, false, null or a no-op); no method throws because of admission.
// Only a foreign UID raises SecurityException, which Binder marshals
// for the caller.
//
// Deadlines
// ---------
// Every data transaction is cancellable. The broker cancels the
// provider call at 15 s and retires its own process at 30 s only when
// the same operation is still registered, so one slow provider read
// cannot drop every torrent handle.
//
// No descriptor is ever exported: ParcelFileDescriptor stays
// broker-internal.
package org.openi2pd.router;
interface ITorrentSafStorage {
    // Wire contract version. Version 2 adds the generation fence
    // (handshake and retire(long)), the one-shot export token
    // (prepareExport/openExport) and the (uid, pid) owner binding on
    // every capability.
    const int API_VERSION = 2;

    // Returns this broker process generation: a non-zero CSPRNG
    // identity chosen when the service was created. The client keeps it
    // separate from its own binding fence and must present it to
    // retire(long). The calling (uid, pid) becomes the retirement
    // authority for this generation. No admission bound applies; it
    // never touches provider state.
    long handshake();

    // Returns API_VERSION so a client can refuse a broker from another
    // build. No bound applies; it never touches provider state.
    int apiVersion();

    // Registers a one-shot export capability for exactly one document.
    // uri: content URI with a persisted or temporary write grant, at
    // most 8192 characters. length: exact final metadata size, 1..8
    // MiB. operation: caller-unique token, 1..Long.MAX_VALUE. Bounds: 8
    // live tokens, 30 s lifetime each, one admission slot. Returns a
    // non-zero token bound to the caller, the URI and the document
    // identity observed now; 0 when the grant, identity, argument or
    // admission is invalid.
    long prepareExport(String uri, long length, long operation);

    // Consumes a prepared token exactly once and truncates only that
    // document. The token must belong to the calling (uid, pid), must
    // be unexpired, and the grant, the document identity and the
    // declared length must still match; otherwise nothing is truncated
    // and 0 is returned. Once admitted, a presented token is consumed
    // even when the descriptor open fails, so one capability can never
    // truncate twice; a refused admission leaves it live. Bounds: 4096
    // open handles, one admission slot. Returns a non-zero non-export
    // handle for write()/finishExport()/closeFile().
    long openExport(long token, long operation);

    // Commits a successful export (exact declared length) or truncates
    // a failed one to zero bytes. Only the handle owner may finish it.
    // Bounds: one admission slot. Returns false for a foreign, unknown
    // or non-export handle, or a size mismatch. A failed export leaves
    // an empty document that the broker will not delete on its own (the
    // user picked it); it stays removable through deleteByRelative()
    // when it lives below a tree root, or through the system file
    // picker otherwise.
    boolean finishExport(long handle, boolean success, long operation);

    // Registers a tree root capability from an already persisted grant.
    // uri: content tree URI, at most 8192 characters.
    // Bounds: 128 roots, one admission slot.
    // Returns a non-zero root owned by the caller, or 0.
    long authorizeTree(String uri, long operation);

    // Resolves (and optionally creates) a bounded relative path below a
    // tree root and opens a random-access regular document. relative:
    // 1..8192 characters, at most 32 components, no empty, "." or ".."
    // component, at most 255 characters each, no control characters.
    // length: 0..2^40; a read must match it exactly, a write may extend
    // a shorter existing document. Bounds: 4096 open handles, one
    // admission slot. Returns a non-zero handle owned by the caller, or
    // 0.
    long openFile(long root, String relative, long length, boolean create, long operation);

    // Reads exactly count bytes at offset from a handle the caller
    // owns. Bounds: count 0..16384, offset+count within length, one
    // admission slot. Returns null on any failure, foreign handle or
    // cancellation.
    byte[] read(long handle, long offset, int count, long operation);

    // Writes bytes at offset through a handle the caller owns and
    // fsyncs. Bounds: 0..16384 bytes, offset+count within length, one
    // admission slot. Returns false on any failure, foreign handle or
    // cancellation.
    boolean write(long handle, long offset, in byte[] bytes, long operation);

    // Deletes only the exact regular tree document behind an open
    // handle the caller owns, after re-opening it and comparing
    // device/inode identity. Bounds: one admission slot. Returns false
    // for an export handle, a foreign handle, a directory or a replaced
    // document.
    boolean deleteFile(long handle, long operation);

    // Deletes one regular document by relative path below a writable
    // tree root the caller owns. Deletion is size-independent: a
    // partial or failed write changes the length, so no declared size
    // is required. Identity is checked by opening the resolved document
    // twice and comparing device/inode; a directory is refused (use
    // removeEmptyDirectory). Bounds: path as openFile, one admission
    // slot.
    boolean deleteByRelative(long root, String relative, long operation);

    // Removes one empty directory by relative path below a writable
    // tree root the caller owns. The child listing must be empty,
    // because a provider is allowed to delete recursively. Bounds: path
    // as openFile, one admission slot.
    boolean removeEmptyDirectory(long root, String relative, long operation);

    // Releases one handle the caller owns. Closing never waits for
    // provider IO, so it cannot wedge the control plane. A foreign or
    // unknown handle is a no-op.
    void closeFile(long handle);

    // Revokes one tree root the caller owns and closes the handles
    // created through it. A foreign or unknown root is a no-op.
    void revokeTree(long root);

    // Out-of-band: cancels exactly the operation registered by this
    // caller's (uid, pid) and never waits for the data-operation lock.
    // A foreign or unknown operation is a no-op.
    oneway void cancel(long operation);

    // Retires the broker process exactly once for the generation
    // returned by handshake(). A stale, wrong-caller or zero generation
    // is a no-op. Death, rather than closing a descriptor from another
    // thread, is what fences every old writer.
    oneway void retire(long generation);
}
