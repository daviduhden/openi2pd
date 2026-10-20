<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: fixed router persistence capability.
Documents the implemented worker and the remaining global separation boundary.
-->
# Fixed router persistence worker

Status: implemented by static analysis; pending CI compilation and CI tests.

`privsep.routerstorage=true` requests a worker before `RouterContext::Init`, so
initial identity generation also uses the delegated writer. Requested backend
failure aborts initialization. The core depends on a transport-independent
callback, avoiding a library dependency on the process supervisor. Writer failure
never falls back to local filesystem IO. Key-save failure aborts creation or
rotation instead of silently running with an unpersisted identity.

The authority is four fixed identifiers: `router.keys`, `router.info`,
`ntcp2.keys` and `ssu2.keys`. Existing serialization and locations are preserved.
Messages contain an enum and at most 32 KiB; peer-controlled paths are not part
of this interface. Atomic replacement uses owned regular files, no symlinks or
hardlinks, exclusive private staging, file sync, rename and parent sync. Fixed
staging names bound crash leftovers. Identity serialization buffers are cleansed.

Linux delegates the datadir descriptor during bootstrap, then installs the
network-denied storage sandbox. OpenBSD starts a detached worker before runtime
pledge, inheriting the root at fd4 and IPC at fd3; no directory SCM_RIGHTS or
execpromises is used. Each worker applies its own policy before accepting state
writes. Configured OpenBSD storage workers retain only their preopened channel,
without new UNIX-connection or descriptor-reception promises. EOF closes the
worker without requiring runtime proc/exec in the parent. Startup rollback and
normal shutdown close the capability after router shutdown.

The service identity must be unprivileged and own the root, which cannot be
group/world writable. Unsupported requested platforms fail explicitly. The
option remains separate from isolated torrent storage and does not establish
global separation. NetDb/profile/configuration, client state, runtime logging
and other persistent authorities still need integration, as do the genuine
network/router/verifier process boundaries. Do not label these obligations as
merely pending tests. The Linux worker regression is prepared, not executed.
