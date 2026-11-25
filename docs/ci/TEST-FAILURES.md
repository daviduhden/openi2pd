<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# Test failure inventory

## 4e6aed8d: nine workflows pass; RT-087 remains

Exact SHA `4e6aed8d01d5a7a442fd9cceb7a04bbc6f4440e0`. Runs:
44749048926 maintenance, 44749053668 Android, 44749058101/44749062746
OpenSSL 3/4, 44749067556 components/Java, 44749072560 OpenBSD,
44749082196 focused instrumented TSan, 44749086732 packaging and
44749092184 validation succeed. Windows 44749077334 fails.

Linux jobs 136331870671/136331871090/136331887962/136331888562
and ASan+UBSan/TSan/UBSan jobs 136331994494/136331994466/136331994497
each pass 279/279. Focused native 10 torrent/3 client/9 profile groups and
15 fuzz smoke checks pass. OpenBSD job 136331922198: 271 pass/three
root-runner capability skips (274); CLANG64 job 136331940190: 262 pass/
one POSIX-only socket skip (263). MSVC job 136331939592: 35 pass/one
RT-087 failure (36). TF-112/113/114 and existing registration/dial controls
pass in their executed lanes. No actual sanitizer error signature was found
in complete current logs; this does not validate local new fault controls.

Android: 187 JVM tests/27 suites; API29 UI skipped. All five packaging
jobs succeed. Java interoperability remains 24 static/vector checks and
two known gaps, no differential binary execution. Perl quality skipped.

History: 585 accessible runs/85 SHAs (214 success/305 failure/66 cancelled),
582 full logs, one partial with three job logs and two unavailable cancelled
runs. Previous 575 entries reused only after exact ID/SHA agreement. All
five missing historical job logs were retried and remain HTTP 404; no
retention-expiry cause is inferred. Logs, metadata and downloaded exact
artifacts remain outside the tree under /var/tmp/openi2pd-ci-4e6a.


## dab02eb3: one checker family plus retained Windows failure

Exact SHA `dab02eb3f89340dd464af646702954b44795e002`. Runs:
44743735674 maintenance, 44743740590 Android, 44743755467 components/
Java, 44743772027 focused instrumented TSan and 44743777396 packaging
succeed. Runs 44743745432/44743749721 OpenSSL 3/4, 44743762052
OpenBSD, 44743766901 Windows and 44743781980 validation fail.

### TF-114: one process deadline masks another in a source checker

Affected jobs: Linux 136313631524/136313631910/136313648312/136313648752,
OpenBSD 136313690599, CLANG64 136313703616, sanitizers
136313755634/136313755786/136313755806. Original TAP 166/167:
child tracing requires bounded nonsecret probe / privacy boundary diagnosed.
Mutation witness 165 now succeeds, but checker exit is 0 instead of 1.

Root cause: the fixture changes GFlags WaitForExit while the checker still
finds the generic bounded-wait substring on the debugger. Unlike TF-113,
the mutation actually happened. Enforce the complete receiver/operand/line
grammar and exact inventory for GFlags, debugger and cleanup; independently
mutate each role. Global review includes router-loader and worker-heap
waits, missing/duplicate/unknown waits and newly inserted unbounded waits.
Implemented / static; Pending CI tests. The unchanged workflow's deadline
was bounded; this is checker enforcement, not an observed product hang.

Each Linux branch and sanitizer suite selects 279: 278 pass/one above
failure. OpenBSD: 270 pass/one failure/three root-runner skips (274).
CLANG64: 261 pass/one failure/one POSIX-only skip (263). MSVC job
136313704140: 35 pass/one RT-087 failure (36). The source-order contract
and published RT-120 admission regressions pass in executed suites.
No actual ASan/LSan/UBSan/TSan report signature was found in complete logs;
failed static checks remain real CI failures. Android: 187 JVM/27 suites
pass; fuzz smoke 15; all five packaging jobs succeed. Java: 24 static/
vector checks plus two known gaps, no differential binary execution.

Six metadata pages: 575 runs/84 SHAs, 205 success/304 failure/66 cancelled.
572 complete logs, one partial with three job logs, two cancelled runs
unavailable; all five historical missing job logs retried, HTTP 404 remains.
No retention-expiry cause is inferred. Exact metadata, logs and downloaded
artifacts are retained outside the tree in /var/tmp/openi2pd-ci-dab0.


## ad9514b5: fixture drift repeats across otherwise executed suites

Exact SHA `ad9514b5c155884b76c09d8c37198ad38482a3ac`. Latest runs:
44734723359 maintenance (success), 44734727435 Android (success),
44734732111/44734737265 OpenSSL 3/4 (failure), 44734742273 components/Java
(success), 44734747132 OpenBSD (failure), 44734750922 Windows (failure),
44734754669 focused TSan (success), 44734759132 packaging (success),
44734763926 validation (failure). Five success/five failure.

### TF-112: stale named-completion source contract

Linux branch jobs 136283154785/136283155166/136283173917/136283174350,
OpenBSD 136283204648, CLANG64 136283218765 and sanitizer jobs
136283272414/136283272728/136283272871 fail control-security-static.
Original diagnostic at check-ci-runtime-contracts.cmake:601 requires
`return [owner, generation, torrent, peer, origin, dial]`. Production now
constructs a named std::function before releasing rollback. Update the
contract to assert that transaction ordering within its function, retaining
capture/identity checks. Implemented / static; Pending CI tests.

### TF-113: a negative mutation no longer changes the workflow

The same jobs fail perl-maintenance-checkers TAP 165..167. The fixture
still mutates WaitForExit(60000), which was replaced by a shared remaining
deadline. Its mutation witness correctly fails; unchanged valid input
then returns success instead of the expected refusal. Mutate the actual
remaining-budget wait and add separate witnessed negatives for setup budget,
loader activation, saved registry kind, exact restore and absent-value cleanup.
Global search found no other maintained old wait operand. Implemented /
static; Pending CI tests. No retry, suppression or test removal.

Each Linux branch/sanitizer suite: 279 selected, 277 pass/two above failures.
OpenBSD: 269 pass/two failures/three root-runner capability skips (274).
CLANG64: 260 pass/two failures/one POSIX-only skip (263). MSVC: 35 pass/
one RT-087 failure (36). New Streaming registration and dial admission
regressions pass in executed lanes. Android: 187 JVM tests/27 suites pass;
15 fuzz smoke pass. Java: 24 static/vector checks/two known gaps, no
differential binary. All five packaging jobs pass. No actual sanitizer
error signature was found; the two failed static checks remain real failures.

History: six pages, 565 runs/83 SHAs (200 success/299 failure/66 cancelled),
562 complete logs, one partial with three job logs, two unavailable cancelled
runs. Five missing job logs were retried, still HTTP 404; no retention cause
is inferred. Evidence and downloaded exact artifacts stay outside the tree
under /var/tmp/openi2pd-ci-ad95.


## 4e524d06: nine successful workflows, one remaining Windows failure

Exact SHA: `4e524d06ef37a4031610c21a24213155e7a6ce67`. Runs:
44728025476 (maintenance), 44728029797 (Android), 44728034259/44728038369
(OpenSSL 3/4), 44728042168 (components/Java), 44728046818 (OpenBSD),
44728051011 (Windows), 44728054974 (focused instrumented TSan),
44728059591 (packaging), 44728063794 (validation).

GCC/Clang backend lanes and each sanitizer suite: 278 selected, zero
failures; fuzz smoke: 15. OpenBSD: 270 pass/3 capability skips; CLANG64:
261 pass/1 POSIX-only skip. MSVC: 34 pass/1 RT-087 failure. Android:
187 JVM tests across 27 suites; device UI skipped. Java comparison:
24 static/vector checks and two known gaps, no differential binary.
No sanitizer error signature was found in complete executed-suite logs.
Previous packaging apt blockers are absent in all five successful jobs.
Perl quality remains skipped. None of these results validates local edits.

Accessible history: 555 runs, 82 SHAs (195 success/294 failure/66
cancelled); 552 full logs, one partial with three job logs and two
unavailable cancelled runs. Five missing job logs were retried and return
HTTP 404; retention expiry is not inferred. Evidence files remain outside
the repository under /var/tmp/openi2pd-ci-4e52.



## 0b882952: previous allocation regressions pass; infrastructure blocks three jobs

Exact SHA `0b882952006e36755458a1876b6821108f51186e`. Paginated history
metadata covers 545 runs / 81 SHAs: 186 success, 293 failure, 66 cancelled.
All ten latest full/failed logs, job metadata and artifact indexes were
retrieved, including successful steps inside failed jobs. Broader historical
retrieval coverage is recorded separately; metadata alone is not a log.
Seven workflows succeed; Windows, Packaging and Validation fail.

Historical log coverage is now explicit: 542 complete run-log files (237
reused caches with exact expected SHA verified, 305 recovered this pass),
one partial run with three recovered job logs, and two canceled runs without
accessible job logs. Every one of the 545 unique metadata records was
accounted for. Full historical output was scanned for compiler, test and
sanitizer diagnostic families; occurrence counts include repeats and cascades
and are not independent findings or evidence of current source defects.

| Historical run / SHA | Remaining evidence gap |
|---|---|
| [39058841124](https://github.com/daviduhden/openi2pd/actions/runs/39058841124), `fe4b29012c83c113efd3d9ca1420c1fb19b4b370` | Canceled Linux/Android; job logs 117472519475 and 117472519699 return HTTP 404; no artifacts |
| [39058346321](https://github.com/daviduhden/openi2pd/actions/runs/39058346321), `a12f6ea9143df1f1cfb65e2a1e265448873c4f82` | Canceled OpenBSD UPnP OFF/ON; jobs 117470871030 and 117470871309 return HTTP 404; no artifacts |
| [39058430980](https://github.com/daviduhden/openi2pd/actions/runs/39058430980), `915d5424014bd48959675926186ea8d7b064968e` | Three musl job logs recovered (117471100986, 117471101202, 117471101278); package job 117472356821 returns HTTP 404. Artifact metadata 12110656110 names static aarch64; it supplies no packaging execution evidence |

An initial gh API refusal to print ANSI controls was corrected with
`--allow-escape-sequences` and three job logs were recovered. That client
policy failure was not retention expiry. The five remaining job 404s above
are recorded without inventing diagnostics or an expiry cause. Raw logs and
per-run coverage/SHA manifests remain outside the repository.


| Run | Outcome | Exact jobs |
|---|---|---|
| [44523997053](https://github.com/daviduhden/openi2pd/actions/runs/44523997053) | success | 135559647353 Agent and skills synchronization: success |
| [44524001311](https://github.com/daviduhden/openi2pd/actions/runs/44524001311) | success | 135559675228 Android arm64 build and unit tests: success; 135559678583 Android API 29 UI and accessibility: skipped |
| [44524006841](https://github.com/daviduhden/openi2pd/actions/runs/44524006841) | success | 135559680441 Linux (gcc, OpenSSL 3.x): success; 135559681032 Linux (clang, OpenSSL 3.x): success |
| [44524012153](https://github.com/daviduhden/openi2pd/actions/runs/44524012153) | success | 135559698041 Linux (clang, OpenSSL 4.x): success; 135559698762 Linux (gcc, OpenSSL 4.x): success |
| [44524017683](https://github.com/daviduhden/openi2pd/actions/runs/44524017683) | success | 135559716156 Install components and consumer package: success; 135559716582 I2P Java interoperability: success |
| [44524022145](https://github.com/daviduhden/openi2pd/actions/runs/44524022145) | success | 135559735746 OpenBSD 8.0 build and tests: success |
| [44524027363](https://github.com/daviduhden/openi2pd/actions/runs/44524027363) | failure | 135559751720 Windows MSVC build and tests: failure; 135559751762 Windows MSYS2 Clang64 build and tests: success |
| [44524032264](https://github.com/daviduhden/openi2pd/actions/runs/44524032264) | success | 135559771894 Instrumented OpenSSL TSan diagnostic: success |
| [44524036291](https://github.com/daviduhden/openi2pd/actions/runs/44524036291) | failure | 135559796085 Static musl build and package: success; 135559796413 Service component install matrix: failure; 135559796461 Container image: success; 135559796556 Debian packages: success; 135559796648 Service contracts: failure |
| [44524040625](https://github.com/daviduhden/openi2pd/actions/runs/44524040625) | failure | 135559803653 C++ source API documentation: failure; 135559803831 Bounded fuzz smoke checks: success; 135559804091 Sanitizers (ubsan): success; 135559804155 Agent Skills static contract: success; 135559804156 Sanitizers (tsan): success; 135559804240 Sanitizers (asan-ubsan): success; 135559806789 Perl quality (Test::More and Perl::Critic): skipped |

MSVC selects 33 tests: 32 pass; only `test-privsep-windows-supervisor`
fails with worker-before-authentication exit 3221225794 / c0000142 (RT-087).
TF-111's two typed-provenance fixtures and all three new RT-110/111/112
native targets pass in this executed revision. These results do not validate
subsequent local fixes, remaining strong callbacks or device-only behavior.

Executed scope is selected / passed / skipped / failed, not just a CTest
summary percentage. Linux GCC 16.2 / Clang 23.1.6 with OpenSSL 3.5.9 / 4.1.0:
276 / 276 / 0 / 0 each. ASan+UBSan, UBSan and TSan have the same counts;
fuzz smoke is 15 / 15 / 0 / 0. Instrumented OpenSSL TSan is only one focused
registry test. OpenBSD 8.0 / Clang 22.1.6 / LibreSSL 4.4.0 selects 271:
268 pass and three skip because the runner is root (`worker-identity`,
`credentials`, `torrent-storage-openbsd`). CLANG64 / Clang 23.1.5 /
OpenSSL 4.1.0 selects 260: 259 pass and the POSIX-only I2PControl AF_UNIX test
skips. These skips leave the skipped security boundaries unverified.
Android's exact 26 JUnit XML reports contain 179 tests, zero failures/errors
and zero skipped JVM tests; the separate API 29 UI job is skipped. Java
interoperability is 24 static/vector successes plus two gaps, with no
`--binary` and no live differential. Optional Perl quality did not execute.

### CI-NOV19-APT: runner downloads fail before the requested validators

Validation source-docs job 135559803653 fails installing Doxygen dependencies
(libfmt9, libxapian30, doxygen and ripgrep): mirror connection timeout/failure,
exit 100. Packaging service install job 135559796413 and service-contract job
135559796648 encounter Ubuntu mirror 503/download failures, also exit 100.
Neither Doxygen nor those service checks ran. Static-musl, Debian and container
siblings succeed. This is infrastructure evidence, not a compiler, test or
packaging-product diagnosis; no retries, source weakening or skipped tests
were added. A future successful manual lane remains necessary.



## 6fbe50c1: nine successful workflows, three MSVC test failures

Exact SHA `6fbe50c1d34ac151aecfce8be15c9ff960f074f1`. Paginated metadata
covers 535 runs / 80 SHAs: 179 success, 290 failure and 66 cancelled.
Complete latest-run logs, failed logs and artifact indexes were recovered for
all ten runs below. This does not claim retrieval of all 535 historical logs;
older evidence and retention gaps remain in their original entries.


- [43796719181](https://github.com/daviduhden/openi2pd/actions/runs/43796719181): Agent Knowledge Validation / success; 133067090195 Agent and skills synchronization=success
- [43796722505](https://github.com/daviduhden/openi2pd/actions/runs/43796722505): Android / success; 133067099029 Android arm64 build and unit tests=success; 133067101044 Android API 29 UI and accessibility=skipped
- [43796726343](https://github.com/daviduhden/openi2pd/actions/runs/43796726343): Linux OpenSSL 3.x / success; 133067110984 Linux (clang, OpenSSL 3.x)=success; 133067111435 Linux (gcc, OpenSSL 3.x)=success
- [43796730745](https://github.com/daviduhden/openi2pd/actions/runs/43796730745): Linux OpenSSL 4.x / success; 133067124292 Linux (clang, OpenSSL 4.x)=success; 133067124616 Linux (gcc, OpenSSL 4.x)=success
- [43796734463](https://github.com/daviduhden/openi2pd/actions/runs/43796734463): Linux / success; 133067135400 I2P Java interoperability=success; 133067135811 Install components and consumer package=success
- [43796738507](https://github.com/daviduhden/openi2pd/actions/runs/43796738507): OpenBSD / success; 133067148538 OpenBSD 8.0 build and tests=success
- [43796742021](https://github.com/daviduhden/openi2pd/actions/runs/43796742021): Windows / failure; 133067159459 Windows MSVC build and tests=failure; 133067159867 Windows MSYS2 Clang64 build and tests=success
- [43796745676](https://github.com/daviduhden/openi2pd/actions/runs/43796745676): Instrumented OpenSSL TSan diagnostic / success; 133067169234 Instrumented OpenSSL TSan diagnostic=success
- [43796749555](https://github.com/daviduhden/openi2pd/actions/runs/43796749555): Packaging and service validation / success; 133067181444 Static musl build and package=success; 133067181679 Service component install matrix=success; 133067181746 Debian packages=success; 133067181792 Service contracts=success; 133067181893 Container image=success
- [43796752971](https://github.com/daviduhden/openi2pd/actions/runs/43796752971): Validation / success; 133067190986 Bounded fuzz smoke checks=success; 133067191139 Sanitizers (ubsan)=success; 133067191275 Agent Skills static contract=success; 133067191287 C++ source API documentation=success; 133067191404 Sanitizers (tsan)=success; 133067191479 Sanitizers (asan-ubsan)=success; 133067192582 Perl quality (Test::More and Perl::Critic)=skipped


Executed suites: Linux GCC 16.2 / Clang 23.1.5 with OpenSSL 3.5.9 / 4.1.0,
273/273 each; OpenBSD 8.0 / Clang 22.1.6 / LibreSSL 4.4.0, 268/268;
CLANG64 / OpenSSL 3.6.5, 257/257; MSVC 19.44 / OpenSSL 3.6.5, 27/30.
ASan+UBSan, UBSan and TSan each pass 273/273; fuzz smoke passes 15/15.
The instrumented OpenSSL TSan job executes only one focused test.
Java interoperability reports 24 static/vector successes and two known gaps;
no differential ran because no `--binary` was supplied. Android UI and Perl
quality are skipped. None of these results validates subsequent local edits.

### TF-111: exception provenance fixtures assume exception_ptr identity

Windows run 43796742021, MSVC job 133067159459, reports assertions at
`test-streaming-sendstate.cpp:133` and `test-ssu2-receive-lifecycle.cpp:137`.
Both helpers rethrow an exception_ptr and compare current_exception() with
the original pointer. Static inspection demonstrates a nonportable fixture:
[C++ permits copies of the exception object](https://eel.is/c++draft/propagation#current_exception),
so object identity is not a portable propagation contract. This explains an
invalid assertion assumption; the logs alone do not expose MSVC's internal
copy operation. Global search found this comparison in these two helpers.

Both now throw a dedicated bad_alloc subtype carrying a shared provenance
token, catch that exact type and compare its payload. No-throw and unrelated
bad_alloc controls must fail injection verification. Existing first/later
allocation, unchanged-queue and retry controls remain. Implemented / static;
Pending CI compilation / Pending CI tests, especially MSVC. RT-087 is the
third MSVC failure and remains independent. TF-110 passes on this published
OpenBSD revision; its former missing diagnostic is not retroactively known.


## 35da66ad: runtime validation resumes; two platform failures remain

Refreshed accessible history: 525 runs / 79 SHAs, 170 success, 289 failure,
66 cancelled. All ten runs below executed SHA `35da66ad6bfdca2210f0b4e8cb582c6ef88e4368`.
Full and failed logs, successful sibling jobs and artifact metadata were
retrieved. The batch has eight successful workflows and two failures.

| Run | Outcome | Exact jobs |
|---|---|---|
| [43693762485](https://github.com/daviduhden/openi2pd/actions/runs/43693762485) | success | 132749710891 Agent and skills synchronization success |
| [43693764849](https://github.com/daviduhden/openi2pd/actions/runs/43693764849) | success | 132749717145 Android arm64 build and unit tests success; 132749718074 Android API 29 UI and accessibility skipped |
| [43693767409](https://github.com/daviduhden/openi2pd/actions/runs/43693767409) | success | 132749724640 Linux (clang, OpenSSL 3.x) success; 132749724825 Linux (gcc, OpenSSL 3.x) success |
| [43693770322](https://github.com/daviduhden/openi2pd/actions/runs/43693770322) | success | 132749733687 Linux (gcc, OpenSSL 4.x) success; 132749733865 Linux (clang, OpenSSL 4.x) success |
| [43693773158](https://github.com/daviduhden/openi2pd/actions/runs/43693773158) | success | 132749741166 I2P Java interoperability success; 132749741333 Install components and consumer package success |
| [43693775689](https://github.com/daviduhden/openi2pd/actions/runs/43693775689) | failure | 132749749314 OpenBSD 8.0 build and tests failure |
| [43693778358](https://github.com/daviduhden/openi2pd/actions/runs/43693778358) | failure | 132749757668 Windows MSYS2 Clang64 build and tests success; 132749757767 Windows MSVC build and tests failure |
| [43693780880](https://github.com/daviduhden/openi2pd/actions/runs/43693780880) | success | 132749765723 Instrumented OpenSSL TSan diagnostic success |
| [43693783386](https://github.com/daviduhden/openi2pd/actions/runs/43693783386) | success | 132749773679 Service component install matrix success; 132749773828 Container image success; 132749773850 Debian packages success; 132749773912 Static musl build and package success; 132749773962 Service contracts success |
| [43693785901](https://github.com/daviduhden/openi2pd/actions/runs/43693785901) | success | 132749782003 Agent Skills static contract success; 132749782139 Sanitizers (tsan) success; 132749782233 Bounded fuzz smoke checks success; 132749782244 Sanitizers (asan-ubsan) success; 132749782269 C++ source API documentation success; 132749782275 Sanitizers (ubsan) success; 132749783201 Perl quality (Test::More and Perl::Critic) skipped |


Linux OpenSSL 3/4 GCC and Clang each pass 273/273; the additional 10/3/9
counts are focused native groups, not Java differentials. ASan+UBSan, UBSan
and TSan each pass 273/273; fuzz smoke is 15/15. CLANG64 passes 257/257.
Independent timer checker/mutation controls execute successfully. These
results validate the published timer and retained-receive controls only;
they do not validate the new local transaction/capacity corrections.

Android JUnit has 179 tests in 26 suites with zero failures, including five
SAF exact-transfer cases. JNI arm64, APK, Javadoc and 46-declaration parity
pass. UI/device job is skipped; no provider/device claim is made. The Java
reference job reports 24 static/vector cases plus two known gaps and explicitly
no --binary execution. Instrumented OpenSSL TSan runs only the registry test
1/1, not a general provider-failure suite. Optional Perl quality is skipped.

OpenBSD passes 267/268 and reaches a later rollback assertion: TF-110 below.
MSVC passes 28/29 and retains RT-087. Exact downloaded artifacts are OpenBSD
14153463329, MSVC 14154521368, Linux3 GCC 14153753156 and Android 14153927534.
Sanitizer archives are listed and available but were not downloaded because
complete logs contain no runtime diagnostic stack requiring those images.

### TF-110 — UDP rollback fixture failed before observing its checkpoint

Run [43693775689](https://github.com/daviduhden/openi2pd/actions/runs/43693775689),
job 132749749314, OpenBSD 8.0 / Clang 22.1.6 / LibreSSL 4.4.0 Release:
`test-udp-ack-lifecycle.cpp:222 CheckFailedStartup: originalFailure`.
The earlier CheckReadCompletion TF-109 phase was passed; this assertion is a
new startup phase. The original log does not expose owner or exception code,
so the exact failure attribution remains open.

Static investigation found four consumers of an unconditional 4 MiB UDP
buffer request, including client startup before its injected checkpoint.
OpenBSD's default SB_MAX is 2 MiB; sbreserve rejects larger requests. The
shared request is now 2 MiB only on OpenBSD, preserving other platforms and
all real configuration errors. Sources: [socketvar.h](https://raw.githubusercontent.com/openbsd/src/master/sys/sys/socketvar.h)
(SB_MAX, revision 1.161) and [uipc_socket2.c](https://raw.githubusercontent.com/openbsd/src/master/sys/kern/uipc_socket2.c)
(sb_max/sbreserve, revision 1.186). This fixes a demonstrated default-platform
incompatibility, not a proven exact-run exception. The fixture now witnesses
the injection, emits only fixed owner/category labels and numeric codes on
failure, and exercises the other UDPSession consumer without network traffic.
Implemented / static; Pending CI compilation / Pending CI tests.

## 5fafdcf1: compile-blocked runtime validation

Latest accessible history: 515 runs / 78 SHAs, 162 success, 287 failure,
66 cancelled. The new batch has two successful workflows and eight failures;
see [CE-088's exact run/job table](COMPILATION-ERRORS.md#ce-088-recurrence-at-5fafdcf1-disposable-proxy-timers).
All use SHA `5fafdcf1f616c10077d480971a6946d15dd0d56f`.
Native CTest, sanitizers and fuzzers did not execute after the SSU2 blocker.
Windows startup probe/test never ran, so RT-087 remains unresolved. OpenBSD
never reached TF-109; the fixture correction is still Pending CI tests.
Android JVM/Lint/JNI/APK and API-29 UI validation likewise did not execute.

Successful Agent Knowledge job 132614358857, source API 132614427197 and
Skills 132614427249 establish only their own scopes. Java interop job
132614389872 reports 24 static/vector successes and two known gaps, explicitly
without --binary or differential execution. All five packaging jobs pass;
the container's older Boost version does not test the new native regressions.
Perl quality 132614428584 was skipped and does not close style debt.

Exact artifacts inspected: OpenBSD 14135437896 (configure/build only), Linux3
GCC 14135228336 (logs/cache/probe YAML), Windows MSYS2 14136015374 (no MSVC
artifact was produced), UBSan 14135388137 (logs/cache/revision and explicit
missing-images list). Missing runtime reports/binaries are phase gaps.
Larger TSan archives remain available but were not downloaded after targeted
artifacts established the common compile blocker; no stack is inferred.
New timer and receive-ownership regressions remain unpublished and unexecuted.

## e1d1b7a6: completed lanes and OpenBSD observation failure

Exact SHA `e1d1b7a63ce42cb2dbc2fd8331c95283742a705e`. Initial history
snapshot: 505 runs / 77 SHAs, 157 successes, 277 failures, 66 cancellations,
five runs in progress. Seventeen completed job logs were recovered directly,
including successful siblings while parent workflows were still active.

OpenSSL3/Clang job 132565599632, OpenSSL4/GCC 132565609027 and UBSan
132565663479 each pass 269/269; CLANG64 132565636198 passes 253/253.
Published RT-097/098/099/100 and TF-108 controls explicitly pass there.
Additional focused native groups pass 10/10, 3/3 and 9/9; fuzz 15/15,
documentation and agent contracts pass. These are scoped results, not a claim
that still-running jobs or subsequent local corrections pass.

Final snapshot: eight workflows succeed and two fail. OpenSSL3/GCC
132565599874 and OpenSSL4/Clang 132565608676 also complete successfully;
ASan+UBSan 132565663495 and TSan 132565663485 each pass 269/269. Validation
43633668033 succeeds, but Perl quality 132565664500 remains skipped. Windows
MSVC 132565636410 passes 24/25 and fails worker initialization (RT-087).
The full 505-run history therefore ends at 160 success, 279 failure and
66 cancelled for this batch; no running lane is counted as passed.

### TF-109 — A single poll is not a socket completion barrier

[OpenBSD run 43633655915](https://github.com/daviduhden/openi2pd/actions/runs/43633655915),
job 132565627097, finishes 263/264. The first real receive observation in
`test-udp-ack-lifecycle`, CheckReadCompletion:106, fails
`successor && successor != previous`. Its expected handler warning does not
appear before the assertion. The fixture sends/cancels then performs one
nonblocking poll, which does not prove that a reactor completion is ready.

Local correction dispatches until the actual operation identity changes under
one absolute five-second observation bound. It does not retry the send/read,
raise CTest's 120/300-second budgets or change product code. Ten analogous
poll-using fixtures were inspected: posted work, explicit-handler controls and
closed-socket cancellation drains did not demonstrate another equivalent
successful-send/single-poll assumption. The newly prepared SSU2 fragment fixture initially repeated the same poll
assumption; it now observes the first partial callback before sending the
remaining bytes, then the final callback under the same absolute bound.
Kernel scheduling details remain a hypothesis, not a product diagnosis. Implemented / static; Pending CI tests.


## Published a1a02a05 audit

Exact SHA `a1a02a053b08aa2ff15fcf8551492adcd476c1b2`; history now contains 495 runs / 76 SHAs:
152 success, 277 failure, 66 cancelled. The latest ten workflows split five
success / five failure. Full logs include successful sibling jobs and steps.
Linux native Release jobs use GCC 16.2.0 / Clang 23.1.5, CMake 4.5.0,
Boost 1.92.0_1, zlib 1.4 and OpenSSL 3.5.9 or 4.1.0. OpenBSD 8.0 uses
Clang 22.1.6, LibreSSL 4.4.0 and zlib 1.3.2; FindOpenSSL's synthetic 2.0.0
is not the selected LibreSSL library version.
The published corrections close TF-106's six prior contract families in
executed lanes; a later Android dialog needle is now exposed (TF-108).

| Run | Job ID | Job/platform | Outcome |
|---|---|---|---|
| [43606719436](https://github.com/daviduhden/openi2pd/actions/runs/43606719436) | 132482837476 | Agent Skills static contract | success |
| [43606719436](https://github.com/daviduhden/openi2pd/actions/runs/43606719436) | 132482837745 | Sanitizers (tsan) | failure |
| [43606719436](https://github.com/daviduhden/openi2pd/actions/runs/43606719436) | 132482837760 | C++ source API documentation | success |
| [43606719436](https://github.com/daviduhden/openi2pd/actions/runs/43606719436) | 132482837761 | Sanitizers (ubsan) | failure |
| [43606719436](https://github.com/daviduhden/openi2pd/actions/runs/43606719436) | 132482837769 | Bounded fuzz smoke checks | success |
| [43606719436](https://github.com/daviduhden/openi2pd/actions/runs/43606719436) | 132482837819 | Sanitizers (asan-ubsan) | failure |
| [43606719436](https://github.com/daviduhden/openi2pd/actions/runs/43606719436) | 132482838573 | Perl quality (Test::More and Perl::Critic) | skipped |
| [43606716217](https://github.com/daviduhden/openi2pd/actions/runs/43606716217) | 132482828254 | Debian packages | success |
| [43606716217](https://github.com/daviduhden/openi2pd/actions/runs/43606716217) | 132482828466 | Static musl build and package | success |
| [43606716217](https://github.com/daviduhden/openi2pd/actions/runs/43606716217) | 132482828526 | Service contracts | success |
| [43606716217](https://github.com/daviduhden/openi2pd/actions/runs/43606716217) | 132482828565 | Service component install matrix | success |
| [43606716217](https://github.com/daviduhden/openi2pd/actions/runs/43606716217) | 132482828738 | Container image | success |
| [43606713559](https://github.com/daviduhden/openi2pd/actions/runs/43606713559) | 132482819781 | Instrumented OpenSSL TSan diagnostic | success |
| [43606710842](https://github.com/daviduhden/openi2pd/actions/runs/43606710842) | 132482811718 | Windows MSYS2 Clang64 build and tests | failure |
| [43606710842](https://github.com/daviduhden/openi2pd/actions/runs/43606710842) | 132482811917 | Windows MSVC build and tests | failure |
| [43606708173](https://github.com/daviduhden/openi2pd/actions/runs/43606708173) | 132482803246 | OpenBSD 8.0 build and tests | failure |
| [43606705310](https://github.com/daviduhden/openi2pd/actions/runs/43606705310) | 132482794675 | Install components and consumer package | success |
| [43606705310](https://github.com/daviduhden/openi2pd/actions/runs/43606705310) | 132482794915 | I2P Java interoperability | success |
| [43606702116](https://github.com/daviduhden/openi2pd/actions/runs/43606702116) | 132482786073 | Linux (gcc, OpenSSL 4.x) | failure |
| [43606702116](https://github.com/daviduhden/openi2pd/actions/runs/43606702116) | 132482786277 | Linux (clang, OpenSSL 4.x) | failure |
| [43606699306](https://github.com/daviduhden/openi2pd/actions/runs/43606699306) | 132482777786 | Linux (clang, OpenSSL 3.x) | failure |
| [43606699306](https://github.com/daviduhden/openi2pd/actions/runs/43606699306) | 132482778025 | Linux (gcc, OpenSSL 3.x) | failure |
| [43606696497](https://github.com/daviduhden/openi2pd/actions/runs/43606696497) | 132482768786 | Android arm64 build and unit tests | success |
| [43606696497](https://github.com/daviduhden/openi2pd/actions/runs/43606696497) | 132482769839 | Android API 29 UI and accessibility | skipped |
| [43606693578](https://github.com/daviduhden/openi2pd/actions/runs/43606693578) | 132482761469 | Agent and skills synchronization | success |

### TF-108 — Wrapped confirmation call hides the next stale contract

Linux OpenSSL 3/4 GCC/Clang jobs each pass 268/269 tests; OpenBSD passes
263/264, CLANG64 252/253 and each sanitizer job 268/269. Their sole failure
is `control-security-static`, included check-android-dialog-authority.cmake:73:
`Activity confirmation lifecycle missing: confirmations.consume(ticket,`.
The Java formatter wrapped that call, while ticket and admission semantics
remain intact. Local correction permits only call-boundary whitespace and
retains the captured ticket plus complete readiness/admission predicate.
Two prepared negative mutations must change real source before rejection.
Implemented / static; Pending CI tests. Native/JVM test success cannot close
this independent source contract. Perl quality job 132482838573 is skipped,
not passing evidence for Perl::Critic.

TF-107's Unicode maintenance fixtures and RT-097's periodic UDP tests now pass
in the reached suites, including ASan/UBSan/TSan; broader network startup and
provider fault injection remain outside those controls. MSVC selects 25 tests,
passes 24, and still fails live worker initialization (RT-087).


## Published cfc15d644 audit

Accessible paginated metadata now contains **485 runs / 75 SHAs**: 147
success, 272 failure and 66 cancelled. The latest ten execute
`cfc15d644d6f9888c533c15dad6d35cecd6a2fa9`: three successful workflows and
seven failures. Complete latest logs include successful siblings; metadata
coverage does not establish availability of every historical log or artifact.

| Run | Job ID | Lane | Conclusion |
|---|---|---|---|
| [43595072942](https://github.com/daviduhden/openi2pd/actions/runs/43595072942) | 132447735042 | Agent and skills synchronization | success |
| [43595075812](https://github.com/daviduhden/openi2pd/actions/runs/43595075812) | 132447742474 | Android arm64 build and unit tests | failure |
| [43595075812](https://github.com/daviduhden/openi2pd/actions/runs/43595075812) | 132447744051 | Android API 29 UI and accessibility | skipped |
| [43595078584](https://github.com/daviduhden/openi2pd/actions/runs/43595078584) | 132447751525 | Linux (gcc, OpenSSL 3.x) | failure |
| [43595078584](https://github.com/daviduhden/openi2pd/actions/runs/43595078584) | 132447751792 | Linux (clang, OpenSSL 3.x) | failure |
| [43595081014](https://github.com/daviduhden/openi2pd/actions/runs/43595081014) | 132447762274 | Linux (clang, OpenSSL 4.x) | failure |
| [43595081014](https://github.com/daviduhden/openi2pd/actions/runs/43595081014) | 132447762475 | Linux (gcc, OpenSSL 4.x) | failure |
| [43595083617](https://github.com/daviduhden/openi2pd/actions/runs/43595083617) | 132447766913 | Install components and consumer package | success |
| [43595083617](https://github.com/daviduhden/openi2pd/actions/runs/43595083617) | 132447767111 | I2P Java interoperability | success |
| [43595085942](https://github.com/daviduhden/openi2pd/actions/runs/43595085942) | 132447774844 | OpenBSD 8.0 build and tests | failure |
| [43595088361](https://github.com/daviduhden/openi2pd/actions/runs/43595088361) | 132447783081 | Windows MSYS2 Clang64 build and tests | failure |
| [43595088361](https://github.com/daviduhden/openi2pd/actions/runs/43595088361) | 132447783285 | Windows MSVC build and tests | failure |
| [43595090827](https://github.com/daviduhden/openi2pd/actions/runs/43595090827) | 132447790354 | Instrumented OpenSSL TSan diagnostic | success |
| [43595093355](https://github.com/daviduhden/openi2pd/actions/runs/43595093355) | 132447799272 | Service component install matrix | success |
| [43595093355](https://github.com/daviduhden/openi2pd/actions/runs/43595093355) | 132447799424 | Static musl build and package | success |
| [43595093355](https://github.com/daviduhden/openi2pd/actions/runs/43595093355) | 132447799437 | Debian packages | success |
| [43595093355](https://github.com/daviduhden/openi2pd/actions/runs/43595093355) | 132447799497 | Container image | success |
| [43595093355](https://github.com/daviduhden/openi2pd/actions/runs/43595093355) | 132447799543 | Service contracts | failure |
| [43595095702](https://github.com/daviduhden/openi2pd/actions/runs/43595095702) | 132447805872 | Agent Skills static contract | failure |
| [43595095702](https://github.com/daviduhden/openi2pd/actions/runs/43595095702) | 132447806018 | Sanitizers (asan-ubsan) | failure |
| [43595095702](https://github.com/daviduhden/openi2pd/actions/runs/43595095702) | 132447806092 | C++ source API documentation | success |
| [43595095702](https://github.com/daviduhden/openi2pd/actions/runs/43595095702) | 132447806102 | Bounded fuzz smoke checks | success |
| [43595095702](https://github.com/daviduhden/openi2pd/actions/runs/43595095702) | 132447806130 | Sanitizers (ubsan) | failure |
| [43595095702](https://github.com/daviduhden/openi2pd/actions/runs/43595095702) | 132447806238 | Sanitizers (tsan) | failure |
| [43595095702](https://github.com/daviduhden/openi2pd/actions/runs/43595095702) | 132447807004 | Perl quality (Test::More and Perl::Critic) | skipped |

### TF-106 — Formatting invalidated six source/fixture contracts

Validation run 43595095702 reproduces six failed entries in each ASan+UBSan,
UBSan and TSan job: `perl-maintenance-checkers`, `build-system-static`,
`control-security-static`, `services-static`, `packaging-static` and
`i18n-static`. These are fixture/source-inspection failures, not observed
sanitizer reports or compiler errors. Linux OpenSSL 3/4, OpenBSD and CLANG64
repeat applicable contracts; packaging's service job fails its service guard.

| Contract | First diagnostic / root cause | Correction boundary |
|---|---|---|
| Perl maintenance fixture | Final supervisor operand mutation no longer matches the formatted PowerShell array; tests 153-155 fail | Match only final quoted worker with layout whitespace; retain effective-mutation assertion and exact other boundaries |
| Build-system | Selective MSVC CTest filter is missing | Inspect the joined PowerShell filter representation and preserve selected-test membership |
| SAF creation budget | Encoded URI must be charged before source retention | Adapt the exact wrapped Source-construction expression while preserving charge-before-retain ordering |
| Services | Missing `1|true|yes) I2PD_ENABLED=yes` | Accept shell formatter layout without changing enable-state tokens |
| Packaging | Missing `configure|reconfigure)` | Inspect formatted case alternatives without weakening maintainer-script operations |
| I18N | Missing `File::Spec->abs2rel($path, $root)` | Pin full actual `File::Spec->abs2rel( $path, $root )` expression |

Local contract corrections are Implemented / static; Pending CI tests.
Do not normalize whitespace inside quoted strings or drop security operands.
The new ECIES integrity/overlap controls and DHT timer lifetime controls pass
in reached native suites, including the sanitizer matrix. Provider/allocator
fault injection and complete DHT transport teardown remain separate gaps.
Android's classpath-report task fails before D8 execution (CE-091). Windows
MSVC retains RT-087's initialization-status failure. Its exact artifact now
contains an executed arming marker and worker termination stack through
`NtTerminateProcess` / `LdrInitializeThunk` / `LdrStandardizeSystemPath`, with
worker `1ff8` exit `c0000142` and separate conhost `1ffc` exit `c0000022`.
The parent later exits 1 and the debugger reports no runnable debuggees.
See [RT-087's image hash and evidence limits](RUNTIME-ERRORS.md#rt-087-continuation-armed-probe-reaches-loader-termination).
This is loader-context evidence, not causal attribution or a reason to relax
sandbox restrictions; root cause remains Pending.


### TF-107 — Unicode link validation mixed characters and bytes

A new local Markdown link exposed that check-doc-links read raw UTF-8 bytes
before Unicode slug/case folding. Punctuation could become false letters and
Cyrillic fragments could disagree. This is a local compile-free diagnostic,
not a failure attributed to a GitHub job. Both input paths now decode strictly;
URL escapes are decoded as UTF-8 octets before Unicode comparison. Filesystem
joins preserve their native byte-path boundary and diagnostics encode UTF-8.

Prepared maintenance fixtures cover em dash/Cyrillic headings, literal and
percent-encoded paths/fragments/images, non-ASCII parent directories, broken
Unicode anchors and malformed UTF-8/percent/NUL input. URL validation retains
exit 1; unreadable or malformed Markdown aborts explicitly. Direct checker
execution passes the current Markdown tree; the fixture suite is Pending CI
tests. No policy suppression or empty-anchor fallback is used.

New UDP periodic regressions (RT-097) and all TF-106/CE-091 corrections remain
Implemented / static; Pending CI compilation / Pending CI tests. The new
MSVC selection expands to 25 required entries without removing old coverage.

## Published b001b969 audit

Accessible paginated metadata contains **475 runs / 74 SHAs**: 144 success,
265 failure and 66 cancelled. The latest ten execute exactly
`b001b969b858a054375ee9b037896cf5eb94fda0`: eight successful workflows and
Android/Windows failures. Latest logs include successful siblings and steps;
this does not establish availability of all historical logs or artifacts.

| Run | Job ID | Lane | Conclusion |
|---|---|---|---|
| [43536044722](https://github.com/daviduhden/openi2pd/actions/runs/43536044722) | 132265178608 | Agent and skills synchronization | success |
| [43536049055](https://github.com/daviduhden/openi2pd/actions/runs/43536049055) | 132265190290 | Android arm64 build and unit tests | failure |
| [43536049055](https://github.com/daviduhden/openi2pd/actions/runs/43536049055) | 132265192521 | Android API 29 UI and accessibility | skipped |
| [43536053563](https://github.com/daviduhden/openi2pd/actions/runs/43536053563) | 132265205212 | Linux (gcc, OpenSSL 3.x) | success |
| [43536053563](https://github.com/daviduhden/openi2pd/actions/runs/43536053563) | 132265205296 | Linux (clang, OpenSSL 3.x) | success |
| [43536057521](https://github.com/daviduhden/openi2pd/actions/runs/43536057521) | 132265216701 | Linux (clang, OpenSSL 4.x) | success |
| [43536057521](https://github.com/daviduhden/openi2pd/actions/runs/43536057521) | 132265217014 | Linux (gcc, OpenSSL 4.x) | success |
| [43536061026](https://github.com/daviduhden/openi2pd/actions/runs/43536061026) | 132265228270 | Install components and consumer package | success |
| [43536061026](https://github.com/daviduhden/openi2pd/actions/runs/43536061026) | 132265228543 | I2P Java interoperability | success |
| [43536065149](https://github.com/daviduhden/openi2pd/actions/runs/43536065149) | 132265240305 | OpenBSD 8.0 build and tests | success |
| [43536069099](https://github.com/daviduhden/openi2pd/actions/runs/43536069099) | 132265254180 | Windows MSVC build and tests | failure |
| [43536069099](https://github.com/daviduhden/openi2pd/actions/runs/43536069099) | 132265254493 | Windows MSYS2 Clang64 build and tests | success |
| [43536072801](https://github.com/daviduhden/openi2pd/actions/runs/43536072801) | 132265266173 | Instrumented OpenSSL TSan diagnostic | success |
| [43536077028](https://github.com/daviduhden/openi2pd/actions/runs/43536077028) | 132265279457 | Container image | success |
| [43536077028](https://github.com/daviduhden/openi2pd/actions/runs/43536077028) | 132265279569 | Debian packages | success |
| [43536077028](https://github.com/daviduhden/openi2pd/actions/runs/43536077028) | 132265279607 | Service contracts | success |
| [43536077028](https://github.com/daviduhden/openi2pd/actions/runs/43536077028) | 132265279631 | Static musl build and package | success |
| [43536077028](https://github.com/daviduhden/openi2pd/actions/runs/43536077028) | 132265279863 | Service component install matrix | success |
| [43536080681](https://github.com/daviduhden/openi2pd/actions/runs/43536080681) | 132265291590 | Agent Skills static contract | success |
| [43536080681](https://github.com/daviduhden/openi2pd/actions/runs/43536080681) | 132265291884 | Sanitizers (asan-ubsan) | success |
| [43536080681](https://github.com/daviduhden/openi2pd/actions/runs/43536080681) | 132265291886 | C++ source API documentation | success |
| [43536080681](https://github.com/daviduhden/openi2pd/actions/runs/43536080681) | 132265292058 | Bounded fuzz smoke checks | success |
| [43536080681](https://github.com/daviduhden/openi2pd/actions/runs/43536080681) | 132265292136 | Sanitizers (tsan) | success |
| [43536080681](https://github.com/daviduhden/openi2pd/actions/runs/43536080681) | 132265293293 | Sanitizers (ubsan) | success |
| [43536080681](https://github.com/daviduhden/openi2pd/actions/runs/43536080681) | 132265293342 | Perl quality (Test::More and Perl::Critic) | skipped |

RT-092 preserved-offset copy controls (`test-signed-data-bounds`) and RT-093
ElGamal failure/overlap controls (`test-crypto-audit-bounds`) pass in the
executed native suites, including Linux GCC/Clang OpenSSL 3/4 and sanitizer
lanes. These results validate those bounded regressions, not allocator or
provider fault injection. New local ECIES digest gates and DHT timer ownership
changes are Pending CI compilation / Pending CI tests.

Android job 132265190290 builds its ordinary targets, then its separate D8
probe selects an R8 8.6.2-dev jar and rejects Java 25 major version 69
(CE-090). This is not evidence that the successfully built app used that jar.
Windows MSVC job 132265254180 still fails the live supervisor before
handshake with 3221225794 (`0xC0000142`), RT-087. Its debugger step fails and
the termination/exit markers are absent; script execution itself was not
proven. No child initializer cause is established.
Skipped Android device, optional Perl quality and OpenBSD worker-capability
paths remain unvalidated.


## Published 519c0386 audit

Accessible history now contains **465 completed runs / 73 SHAs**: 136 success,
263 failure and 66 cancelled (451 manual and 14 historical push runs).
The latest ten execute exactly
`519c038609f92309b60bd2986d0a4bb23756a077`: nine successful workflows and one
Windows failure. Full logs include successful steps and sibling jobs, not
only failed output. Historical missing logs/artifacts remain explicit gaps;
metadata coverage is not complete diagnostic coverage.

| Run | Job ID | Lane | Conclusion |
|---|---|---|---|
| [43523860539](https://github.com/daviduhden/openi2pd/actions/runs/43523860539) | 132224634881 | Agent and skills synchronization | success |
| [43523867631](https://github.com/daviduhden/openi2pd/actions/runs/43523867631) | 132224656840 | Android arm64 build and unit tests | success |
| [43523867631](https://github.com/daviduhden/openi2pd/actions/runs/43523867631) | 132224659023 | Android API 29 UI and accessibility | skipped |
| [43523872446](https://github.com/daviduhden/openi2pd/actions/runs/43523872446) | 132224671356 | Linux (gcc, OpenSSL 3.x) | success |
| [43523872446](https://github.com/daviduhden/openi2pd/actions/runs/43523872446) | 132224671853 | Linux (clang, OpenSSL 3.x) | success |
| [43523876608](https://github.com/daviduhden/openi2pd/actions/runs/43523876608) | 132224684825 | Linux (gcc, OpenSSL 4.x) | success |
| [43523876608](https://github.com/daviduhden/openi2pd/actions/runs/43523876608) | 132224685584 | Linux (clang, OpenSSL 4.x) | success |
| [43523880819](https://github.com/daviduhden/openi2pd/actions/runs/43523880819) | 132224700470 | Install components and consumer package | success |
| [43523880819](https://github.com/daviduhden/openi2pd/actions/runs/43523880819) | 132224700848 | I2P Java interoperability | success |
| [43523886074](https://github.com/daviduhden/openi2pd/actions/runs/43523886074) | 132224718963 | OpenBSD 8.0 build and tests | success |
| [43523891709](https://github.com/daviduhden/openi2pd/actions/runs/43523891709) | 132224734937 | Windows MSYS2 Clang64 build and tests | success |
| [43523891709](https://github.com/daviduhden/openi2pd/actions/runs/43523891709) | 132224735862 | Windows MSVC build and tests | failure |
| [43523896686](https://github.com/daviduhden/openi2pd/actions/runs/43523896686) | 132224752618 | Instrumented OpenSSL TSan diagnostic | success |
| [43523901724](https://github.com/daviduhden/openi2pd/actions/runs/43523901724) | 132224768754 | Static musl build and package | success |
| [43523901724](https://github.com/daviduhden/openi2pd/actions/runs/43523901724) | 132224769141 | Service contracts | success |
| [43523901724](https://github.com/daviduhden/openi2pd/actions/runs/43523901724) | 132224769217 | Debian packages | success |
| [43523901724](https://github.com/daviduhden/openi2pd/actions/runs/43523901724) | 132224769313 | Service component install matrix | success |
| [43523901724](https://github.com/daviduhden/openi2pd/actions/runs/43523901724) | 132224769332 | Container image | success |
| [43523906552](https://github.com/daviduhden/openi2pd/actions/runs/43523906552) | 132224783809 | C++ source API documentation | success |
| [43523906552](https://github.com/daviduhden/openi2pd/actions/runs/43523906552) | 132224783979 | Bounded fuzz smoke checks | success |
| [43523906552](https://github.com/daviduhden/openi2pd/actions/runs/43523906552) | 132224784136 | Agent Skills static contract | success |
| [43523906552](https://github.com/daviduhden/openi2pd/actions/runs/43523906552) | 132224784232 | Sanitizers (asan-ubsan) | success |
| [43523906552](https://github.com/daviduhden/openi2pd/actions/runs/43523906552) | 132224784315 | Sanitizers (ubsan) | success |
| [43523906552](https://github.com/daviduhden/openi2pd/actions/runs/43523906552) | 132224784350 | Sanitizers (tsan) | success |
| [43523906552](https://github.com/daviduhden/openi2pd/actions/runs/43523906552) | 132224785363 | Perl quality (Test::More and Perl::Critic) | skipped |

All four OpenSSL compiler lanes and ASan+UBSan, UBSan and TSan pass their
269 selected entries. TF-104's Perl inventory and TF-105's retained-child
ownership controls now pass in the executed Linux lanes; the Perl inventory
also passes on CLANG64 and OpenBSD. This validates those focused corrections,
not every concurrent shutdown or allocator/provider failure. RT-091's factory
tier/empty-input controls run through test-signed-data-bounds in these suites.
Fifteen fuzz smoke entries and the focused instrumented OpenSSL TSan test
pass. Source documentation, service/package and component-consumer jobs pass.

Windows CLANG64 passes 253 selected entries, including perl-portability.
MSVC builds and links, then passes 23 of 24 entries. The live supervisor
still fails before authentication with exit 3221225794 (`0xC0000142`), RT-087.
The contained startup diagnostic step also fails (`startup probe debugger
failed`) and does not establish a successful probe or a child initializer
stack. It cannot attribute the original failure to memory limits, tokens,
mitigations or application entry. Runtime installation is skipped afterward.

OpenBSD reports 264 selected entries with three skips: worker identity,
credentials and storage-supervisor controls. Their omitted paths are not
validated by the other tests. Android device/accessibility and optional Perl
quality remain skipped. Android's ordinary build, JVM/Lint/Javadoc/JNI and
APK steps pass; two ChromeOsAbiSupport Lint warnings remain independently
tracked. New local RT-092/093 changes are not part of any listed execution.


## Published fdfb1162 audit

History pagination reaches **455 completed runs / 72 SHAs**: 127 success,
262 failure and 66 cancelled (441 manual, 14 historical push). The latest ten
execute `fdfb116216058ddd1003981ebebc8901d7777621`: five success and five
failure. Full failed/successful sibling logs and artifact inventories were
retrieved. Historical unavailable evidence remains a gap, not a clean result.

| Run | Job ID | Lane | Conclusion |
|---|---|---|---|
| [43125810337](https://github.com/daviduhden/openi2pd/actions/runs/43125810337) | 130877469094 | Windows MSYS2 Clang64 build and tests | failure |
| [43125810337](https://github.com/daviduhden/openi2pd/actions/runs/43125810337) | 130877469531 | Windows MSVC build and tests | failure |
| [43125814527](https://github.com/daviduhden/openi2pd/actions/runs/43125814527) | 130877481081 | OpenBSD 8.0 build and tests | failure |
| [43125818597](https://github.com/daviduhden/openi2pd/actions/runs/43125818597) | 130877495203 | Container image | success |
| [43125818597](https://github.com/daviduhden/openi2pd/actions/runs/43125818597) | 130877495370 | Service contracts | success |
| [43125818597](https://github.com/daviduhden/openi2pd/actions/runs/43125818597) | 130877495396 | Static musl build and package | success |
| [43125818597](https://github.com/daviduhden/openi2pd/actions/runs/43125818597) | 130877495449 | Debian packages | success |
| [43125818597](https://github.com/daviduhden/openi2pd/actions/runs/43125818597) | 130877495521 | Service component install matrix | success |
| [43125823301](https://github.com/daviduhden/openi2pd/actions/runs/43125823301) | 130877511759 | Agent and skills synchronization | success |
| [43125827774](https://github.com/daviduhden/openi2pd/actions/runs/43125827774) | 130877525588 | Linux (clang, OpenSSL 3.x) | failure |
| [43125827774](https://github.com/daviduhden/openi2pd/actions/runs/43125827774) | 130877526082 | Linux (gcc, OpenSSL 3.x) | failure |
| [43125831805](https://github.com/daviduhden/openi2pd/actions/runs/43125831805) | 130877539211 | Linux (clang, OpenSSL 4.x) | failure |
| [43125831805](https://github.com/daviduhden/openi2pd/actions/runs/43125831805) | 130877539436 | Linux (gcc, OpenSSL 4.x) | failure |
| [43125835856](https://github.com/daviduhden/openi2pd/actions/runs/43125835856) | 130877553010 | Android arm64 build and unit tests | success |
| [43125835856](https://github.com/daviduhden/openi2pd/actions/runs/43125835856) | 130877555683 | Android API 29 UI and accessibility | skipped |
| [43125839804](https://github.com/daviduhden/openi2pd/actions/runs/43125839804) | 130877567644 | Install components and consumer package | success |
| [43125839804](https://github.com/daviduhden/openi2pd/actions/runs/43125839804) | 130877568014 | I2P Java interoperability | success |
| [43125843909](https://github.com/daviduhden/openi2pd/actions/runs/43125843909) | 130877581127 | Agent Skills static contract | success |
| [43125843909](https://github.com/daviduhden/openi2pd/actions/runs/43125843909) | 130877581219 | C++ source API documentation | success |
| [43125843909](https://github.com/daviduhden/openi2pd/actions/runs/43125843909) | 130877581284 | Sanitizers (tsan) | failure |
| [43125843909](https://github.com/daviduhden/openi2pd/actions/runs/43125843909) | 130877581289 | Sanitizers (asan-ubsan) | failure |
| [43125843909](https://github.com/daviduhden/openi2pd/actions/runs/43125843909) | 130877581291 | Sanitizers (ubsan) | failure |
| [43125843909](https://github.com/daviduhden/openi2pd/actions/runs/43125843909) | 130877581337 | Bounded fuzz smoke checks | success |
| [43125843909](https://github.com/daviduhden/openi2pd/actions/runs/43125843909) | 130877583010 | Perl quality (Test::More and Perl::Critic) | skipped |
| [43125848460](https://github.com/daviduhden/openi2pd/actions/runs/43125848460) | 130877595351 | Instrumented OpenSSL TSan diagnostic | success |

All four OpenSSL compiler lanes build/link and run 269 CTest entries. Their
only failing targets are perl-portability (TF-104) and test-datastorage-client
(TF-105). The three sanitizer lanes have the same two failures; no actual
ASan/UBSan/TSan report was found. OpenBSD fails one of 264 entries and CLANG64
one of 253, both only TF-104; their existing capability skips remain separate.
MSVC passes 23 of 24 selected tests; RT-087 still fails before authentication
with exit 3221225794 (0xC0000142). Job 130877469531 builds successfully.

### TF-104 — Producer added to CMake but omitted from expected Perl inventory

Original diagnosis in OpenSSL 3 Clang job 130877525588: portability test 3
reports got android/tools/compact-vector-paths.pl versus expected
 i18n/check-i18n-catalogs.pl at tools/tests/perl-portability.t:168. Syntax and
Critic inventory parity assertions 634/635 share that same missing expected
entry. OpenSSL 3/4 GCC/Clang, all three sanitizer lanes, OpenBSD and CLANG64
reproduce it. This is a maintenance contract failure, not compiler output.

The correction adds the producer to the expected list, retaining discovery
and both CMake registration assertions. A compile-free tools inventory guard
checks all Android Perl scripts across those three seams in both directions.
Existing source-only review confirms four matching scripts. Implemented /
static; Pending CI tests. The Test::More suite was not executed locally.

### TF-105 — Deferred child ownership contradicts eager-reap fixture

Original OpenSSL 3 Clang assertion at test-datastorage-client.cpp:132:
client.WorkerPid () <= 0. Four OpenSSL lanes and all three sanitizer lanes
reproduce it. The published close-only RPC retirement deliberately preserves
the ChildProcess for later reaping; WorkerPid already documents owned-child
identity, not readiness. Global consumer review finds only this fixture.

The fixture now pins the retained original PID during restart backoff and
asserts ECHILD after replacement and explicit Stop. It also observes SIGKILL
with Linux waitid/WNOWAIT before issuing exactly one failed RPC. The old
100-write loop could multiply five-second request budgets and mutate an
unexpectedly live worker. No production timeout increase or eager-reap
rollback was introduced. Implemented / static; Pending CI compilation /
Pending CI tests. Non-Linux code does not instantiate the waitid helper.

Successful siblings include fifteen fuzz smoke entries, source API docs,
Skills, packaging/services, component consumers and Java interop. Instrumented
OpenSSL TSan passes its focused test. Android's retained 25 JVM suites have
174 tests with no failures/errors/skips; debug/release Lint each contain two
ABI findings and zero VectorPath findings. The device/accessibility job and
optional Perl quality job are skipped, not validated.


## Published 6d0127b7 audit

Paginated history now contains **445 completed runs / 71 SHAs**: 122 success,
257 failure and 66 cancelled (431 manual and 14 historical push runs).
The latest ten runs execute exactly
`6d0127b78a4173403614b07120d2e27d21e6e793`: nine workflows succeed, Windows
fails only in its MSVC worker supervisor test. Full successful and failed
logs and artifact inventories were retrieved. Historical evidence gaps remain;
metadata availability does not imply every old log or artifact survives.

| Run | Job ID | Configuration | Result |
|---|---|---|---|
| [43115885487](https://github.com/daviduhden/openi2pd/actions/runs/43115885487) | 130843640410 | Windows MSYS2 Clang64 build and tests | success |
| [43115885487](https://github.com/daviduhden/openi2pd/actions/runs/43115885487) | 130843640621 | Windows MSVC build and tests | failure |
| [43115889983](https://github.com/daviduhden/openi2pd/actions/runs/43115889983) | 130843657335 | OpenBSD 8.0 build and tests | success |
| [43115894670](https://github.com/daviduhden/openi2pd/actions/runs/43115894670) | 130843672860 | Static musl build and package | success |
| [43115894670](https://github.com/daviduhden/openi2pd/actions/runs/43115894670) | 130843673204 | Debian packages | success |
| [43115894670](https://github.com/daviduhden/openi2pd/actions/runs/43115894670) | 130843673208 | Container image | success |
| [43115894670](https://github.com/daviduhden/openi2pd/actions/runs/43115894670) | 130843673224 | Service contracts | success |
| [43115894670](https://github.com/daviduhden/openi2pd/actions/runs/43115894670) | 130843673288 | Service component install matrix | success |
| [43115899316](https://github.com/daviduhden/openi2pd/actions/runs/43115899316) | 130843689149 | Agent and skills synchronization | success |
| [43115903969](https://github.com/daviduhden/openi2pd/actions/runs/43115903969) | 130843705212 | Linux (gcc, OpenSSL 3.x) | success |
| [43115903969](https://github.com/daviduhden/openi2pd/actions/runs/43115903969) | 130843705491 | Linux (clang, OpenSSL 3.x) | success |
| [43115908820](https://github.com/daviduhden/openi2pd/actions/runs/43115908820) | 130843719378 | Linux (clang, OpenSSL 4.x) | success |
| [43115908820](https://github.com/daviduhden/openi2pd/actions/runs/43115908820) | 130843719742 | Linux (gcc, OpenSSL 4.x) | success |
| [43115913175](https://github.com/daviduhden/openi2pd/actions/runs/43115913175) | 130843735271 | Android arm64 build and unit tests | success |
| [43115913175](https://github.com/daviduhden/openi2pd/actions/runs/43115913175) | 130843736970 | Android API 29 UI and accessibility | skipped |
| [43115917664](https://github.com/daviduhden/openi2pd/actions/runs/43115917664) | 130843749626 | I2P Java interoperability | success |
| [43115917664](https://github.com/daviduhden/openi2pd/actions/runs/43115917664) | 130843750074 | Install components and consumer package | success |
| [43115922661](https://github.com/daviduhden/openi2pd/actions/runs/43115922661) | 130843767974 | Agent Skills static contract | success |
| [43115922661](https://github.com/daviduhden/openi2pd/actions/runs/43115922661) | 130843768286 | Sanitizers (tsan) | success |
| [43115922661](https://github.com/daviduhden/openi2pd/actions/runs/43115922661) | 130843768311 | Sanitizers (ubsan) | success |
| [43115922661](https://github.com/daviduhden/openi2pd/actions/runs/43115922661) | 130843768330 | Bounded fuzz smoke checks | success |
| [43115922661](https://github.com/daviduhden/openi2pd/actions/runs/43115922661) | 130843768407 | Sanitizers (asan-ubsan) | success |
| [43115922661](https://github.com/daviduhden/openi2pd/actions/runs/43115922661) | 130843768489 | C++ source API documentation | success |
| [43115922661](https://github.com/daviduhden/openi2pd/actions/runs/43115922661) | 130843770002 | Perl quality (Test::More and Perl::Critic) | skipped |
| [43115927206](https://github.com/daviduhden/openi2pd/actions/runs/43115927206) | 130843783675 | Instrumented OpenSSL TSan diagnostic | success |

MSVC job 130843640621 builds successfully and passes 18 of 19 selected tests.
`test-privsep-windows-supervisor` fails before authentication: worker exit
3221225794 (`0xC0000142`), followed by the bounded accept failure. This repeats
the historical worker family; the CLANG64 informational modal is resolved in
job 130843640410, whose 250 selected tests have no failures (one skipped).
No child initializer stack identifies the MSVC failure yet.

ASan+UBSan, UBSan and TSan each pass 266 selected entries. Fifteen fuzz smoke
entries pass; instrumented OpenSSL TSan passes its focused regression. These
results validate only the executed published revision and selected inputs.
OpenBSD reports 261 entries with three capability skips. Its exact artifact
states: worker identity/credentials require a non-root runner, and storage
workers refuse a root runner. Retained Android artifacts contain 174 JVM entries with no failures/errors;
debug/release Lint XML each contain twelve VectorPath and two ABI findings.
Android device/accessibility and optional Perl quality jobs are skipped; no
device or Perl::Critic result is inferred.

TF-103 dependency acquisition no longer fails in these executed docs/service
lanes. Source API generation, package installation and consumer checks pass;
retain the historical DNS evidence rather than treating recurrence as fixed
by a product change. See runtime and warning inventories for remaining debt.


## 74d49559 — compile recurrence and exact historical diagnostic

Snapshot: 435 completed runs / 70 SHAs; 113 success, 256 failure, 66 cancelled.
The eleven additions are ten 74d49559 workflows (one success / nine failure)
and the earlier c25 Windows diagnostic. All latest logs, job metadata and
artifact inventories were retrieved; exact diagnostic and package artifacts
were inspected. Historical unavailable logs remain evidence gaps.

At `74d495596815075002f633e105affc62be14ad1c`, CE-088 blocks current-Boost
OpenSSL, OpenBSD, Android, Linux consumer, Windows, sanitizer and fuzzer native
execution. Exact affected jobs are grouped in the CE-088 compilation entry.
No new native CTest/sanitizer/fuzz success is inferred from those jobs.
Android has no new JVM/Lint/Javadoc/JNI/APK evidence; its device job
130657138002 skips, and artifact inventory is empty.

Successful siblings remain distinct:

- Packaging [43059250963](https://github.com/daviduhden/openi2pd/actions/runs/43059250963):
  container 130657001436, Debian 130657001503 and static musl 130657001682
  pass with older Boost versions. Exact regular/static package artifacts exist;
  package smoke/lifecycle checks are not the native ACK/storage CTest suites.
- Linux interop 43059296683 / 130657161810 passes reference/vector checks,
  not live Java handshakes or the failed consumer job.
- Agent gate 43059257292 / 130657025304 and Validation Skills 43059303577 /
  130657189219 pass. Optional Perl quality 130657192471 skips.

TF-103 recurs in source-docs job 130657188996 and service jobs
130657000939/130657001652: apt DNS failure before generator/contracts. Unlike
c25, this revision's Debian job succeeds; retain exact per-job evidence.
No retry, warning suppression or disabled validator was introduced.

Historical diagnostic [43055686363](https://github.com/daviduhden/openi2pd/actions/runs/43055686363)
on c25: CLANG64 130644394120 captures the actual modal caller (RT-083);
MSVC 130644393455 still reports worker initialization 0xC0000142 before auth.
These are separate runtime families. Current Windows 43059237413 does not
reach those tests because compilation fails.

New command-line native/default controls, isolated refusal/redaction children,
actual-router entry probes and timer-checker negative fixtures are registered
for existing manual CI. New implementation remains Pending CI compilation /
Pending CI tests. A rejection wrapper requires exact code 1, trusted outcome
and no sanitizer or sentinel; arbitrary nonzero/WILL_FAIL cannot prove refusal.

## c25a0b08 — TF-103 infrastructure acquisition failure

Snapshot before diagnostic dispatch: 424 completed runs / 69 SHAs;
112 success, 246 failure, 66 cancelled. Exactly ten new c25 runs give seven
successful workflows and three failures. Metadata was paginated; latest-ten
full successful/failed logs and artifact inventories were retrieved.
Historical unavailable logs remain gaps, not clean results.
Every result below executes `c25a0b081aa3d3b968e043d968b904a40b48e4f6`.

- [Validation 43037707365](https://github.com/daviduhden/openi2pd/actions/runs/43037707365):
  source-docs job 130580894102 fails apt with exit 100 after DNS resolution
  failure for azure.archive.ubuntu.com. Doxygen never starts. ASan+UBSan
  130580893127, UBSan 130580893120 and TSan 130580893264 each pass 261 tests;
  full logs contain no sanitizer diagnostic. Fuzz 130580892747 passes fifteen
  targets; Skills 130580893318 passes; Perl quality 130580895121 skips.
- [Packaging 43037666619](https://github.com/daviduhden/openi2pd/actions/runs/43037666619):
  service install 130580746855 and contracts 130580747343 fail the same apt
  DNS acquisition before configure/contracts. Debian 130580748977 is cancelled
  during dependency acquisition; no regular-package artifact or maintainer
  script validation exists. Container 130580749537 and static-musl
  130580747141 pass. Exact static-musl artifact contains the static PIE package;
  that separate artifact does not validate the unbuilt regular package.
- OpenSSL 3 [43037679443](https://github.com/daviduhden/openi2pd/actions/runs/43037679443)
  jobs 130580792543/130580793026 and OpenSSL 4
  [43037685671](https://github.com/daviduhden/openi2pd/actions/runs/43037685671)
  130580814605/130580815136 each pass 261 main and 10/3/9 focused tests.
- OpenBSD [43037659437](https://github.com/daviduhden/openi2pd/actions/runs/43037659437)
  job 130580718405: 256 registered, 253 pass and three non-root-required skips.
- Android [43037693624](https://github.com/daviduhden/openi2pd/actions/runs/43037693624)
  130580862404 succeeds; 174 JVM tests pass, JNI parity covers 46 declarations,
  Java major 69 / DEX 039 checks pass. Device 130580865228 skips; Lint remains.
- Windows [43037652402](https://github.com/daviduhden/openi2pd/actions/runs/43037652402)
  130580697443: 245 registered / 243 pass / one failure / one skip;
  130580697869: 19 registered / 18 pass / one failure. Startup causes remain open.
- Linux [43037699387](https://github.com/daviduhden/openi2pd/actions/runs/43037699387)
  consumer 130580864054 and reference/vector interop 130580864479 pass; not
  live Java handshake evidence. Agent gate 43037672749 / 130580768127 passes.
  Instrumented OpenSSL TSan 43037714920 / 130580926326 passes one registry test.

TF-103 is infrastructure, not a compiler or test assertion regression. Do not
add retries, broad package allowances or disable validators to hide it.
Skipped generator/service/package coverage remains Pending CI tests. New
RT-081/082 and root-metadata controls are prepared, not executed. Separate
manual diagnostic 43055686363 was launched after this historical snapshot;
no remote result can validate the new local implementation.

## Revision 830c65ab: latest ten and historical coverage

Paginated metadata now covers 414 completed runs on 68 SHAs: 105 success,
243 failure and 66 cancelled. Latest ten are nine success and Windows
failure. The earlier incomplete diagnostic 42745408718 is now failure.
Metadata spans back to run 39057879349; historical unavailable job logs
39058430980/117472356821 and runs 39058841124/39058346321 remain gaps.
All latest-ten full logs, job metadata and artifact inventories were retrieved;
Windows, OpenBSD and Android artifacts were inspected. No complete historical
artifact/log coverage is claimed.

Every row below executes `830c65ab1aaed6db9d6edeb7d0a91918ac446e21`.

| Run / workflow | Jobs | Observed scope |
|---|---|---|
| [42843157728 OpenSSL 3](https://github.com/daviduhden/openi2pd/actions/runs/42843157728) | 129923464005 / 129923464466 | GCC/Clang each 261 main tests plus 10/3/9 focused tests pass |
| [42843164455 OpenSSL 4](https://github.com/daviduhden/openi2pd/actions/runs/42843164455) | 129923487225 / 129923487479 | Same executed main/focused scopes pass |
| [42843184621 Validation](https://github.com/daviduhden/openi2pd/actions/runs/42843184621) | UBSan 129923558543, TSan 129923558600, ASan+UBSan 129923558655 | Each 261 pass; no sanitizer diagnostic found in reviewed full logs |
| Same Validation run | Fuzz 129923558436, docs 129923557845, Skills 129923558654 | Fifteen smoke targets, source docs and Skills pass; Perl quality 129923560250 skipped |
| [42843137520 OpenBSD](https://github.com/daviduhden/openi2pd/actions/runs/42843137520) | 129923394440 | JUnit: 256 registered, 253 pass, three non-root-required skips |
| [42843171562 Android](https://github.com/daviduhden/openi2pd/actions/runs/42843171562) | 129923514053 | JUnit: 25 suites / 174 JVM tests pass; device job 129923566413 skipped; Lint warnings persist |
| [42843132412 Windows](https://github.com/daviduhden/openi2pd/actions/runs/42843132412) | CLANG64 129923376820 / MSVC 129923377257 | JUnit: 243 pass / one failure / one skip out of 245; MSVC 18 pass / one failure out of 19 |
| [42843191661 Instrumented OpenSSL TSan](https://github.com/daviduhden/openi2pd/actions/runs/42843191661) | 129923581632 | One registry test passes, not the complete crypto suite |
| [42843178138 Linux](https://github.com/daviduhden/openi2pd/actions/runs/42843178138) | Consumer 129923534333 / interop 129923535177 | Install/consumer and reference/vector interop pass; not live Java handshake evidence |
| [42843143279 Packaging](https://github.com/daviduhden/openi2pd/actions/runs/42843143279) | Services 129923412035/129923412036; container 129923412052; Debian 129923412115; musl 129923412271 | Pass, with external tool/runner debt tracked separately |
| [42843150302 Agent Knowledge](https://github.com/daviduhden/openi2pd/actions/runs/42843150302) | 129923436556 | Pass |

Published CW-031/032 removal and prepared encoder regressions now have CI
execution evidence in the supported lanes that ran. Published RT-078 source
contracts and ordinary storage tests pass; allocator unwind remains untested
dynamically. TF-102 still exposes Windows startup failure. OpenBSD skips say
worker identity/credentials require non-root and storage workers refuse root;
MSYS2 skips the POSIX-only I2PControl socket test.

New RT-079/080, startup-root RAII and CDB caller diagnostics are local:
Pending CI compilation / Pending CI tests. Existing test-eddsa,
test-signature-identity-hardening, control-security-static and
perl-maintenance-checkers registrations cover the prepared increments.

## EA333 retrospective snapshot

The paginated inventory contains 404 runs on 67 SHAs: 96 success, 241 failure,
66 cancelled and one incomplete at collection. This is metadata coverage,
not a claim that all historical logs or artifacts remain available.

At `ea3335843fc9073fbbaa0c5d3530c9e9387b85b8`:

- OpenSSL 3 run 42739900479 and OpenSSL 4 run 42739905544 each pass 261
  main tests in both GCC and Clang jobs, plus their focused suites.
- Validation 42739919919 passes 261 tests in each ASan/UBSan, UBSan and TSan
  lane and fifteen fuzzer smoke targets. Reviewed logs contain no sanitizer
  report. Instrumented OpenSSL TSan 42739924109 exercises one registry test.
- OpenBSD 42739887270 registers 256 tests: 253 pass and three skip because
  worker/credential checks require non-root execution. Skips are evidence gaps.
- Android 42739910723 passes 174 JVM tests in 25 suites. Device execution is
  skipped; fourteen Lint warnings remain (twelve vector paths, two ChromeOS
  ABI warnings). APK construction does not validate device behavior.
- TF-101 source-root correction now passes in executed native lanes. TF-102
  classification correctly exposes a real Windows worker startup failure.
  Windows 42739881700 MSVC has 18 pass/one failure; CLANG64 has 243 pass,
  one failure/one skip out of 245. Diagnostic 42745408718 reproduces them:
  version timeout and worker 0xC0000142, respectively. See RT inventory for
  the newly captured modal marker; neither startup problem is resolved.

Current override, bounded-length, encoding and storage-unwind corrections
have source contracts/regressions prepared but remain Pending CI compilation
and Pending CI tests. Published results above apply only to their exact SHA.


## Published revision `01960093` (2026-11-05)

| ID | Exact evidence | Disposition |
|---|---|---|
| TF-046 / TF-062 | Windows 42056732400, MSVC `127347034968`: test-tui-protocol heap exception `0xc0000374`, empty LastTest output | Unresolved RT-032; constant phase markers prepared, published PageHeap run 42061501515 dispatched |
| TF-072 | Same Windows job: test-privsep-windows-supervisor skipped after `worker pipe accept timeout` at startup | Capability/worker-start evidence gap; no claim that handshake/restart/stop passed; child-liveness diagnostics prepared |
| TF-073 | Validation 42056754181 sanitizer jobs `127347117917`, `127347118420`, `127347118453` never reach CTest | CE-076 compile blocker; absence of new reports does not resolve old RT-027/031 |

Linux consumer/interop 42056751743, Android 42056748811, Packaging 42056737144
and Validation's fuzz/docs/static siblings pass at the full SHA recorded in
the compilation inventory. Optional Android device and Perl quality lanes
were skipped. These results validate only the operations actually executed,
not local fixes or the full supported matrix.

Prepared regressions: test-stream-creation-sync (both synchronous callback
paths), malformed Windows hello/re-arm in the existing IPC suite, extended
Tools manpage install checks and negative sanitizer-evidence fixtures.
Source-only checks pass separately; all behavioral execution remains pending
manual CI on a revision containing these local changes.

## Batch `63c42ca4` (run 41232676139, MSYS2) and 41232673150 (Linux)

This is the first batch that executed the newly added suites: the Linux lanes
had stopped at CE-055 and the Windows MSYS2 lane ran CTest. Twenty-five
distinct assertions, three crashes and three C++ assertion classes are
recorded here; each was root-caused and the correct side (product or test)
was fixed. All entries are **Pending CI compilation** and **Pending CI
tests**.

| ID | Test | First diagnostic | Root cause | Correction |
|---|---|---|---|---|
| TF-001 | `test-tunnel-build-records` | segfault, no output | `Tunnel::Build` posts through `Transports::SendMessages`; `m_Service` is created only by `Start()`, while `m_IsOnline` defaults true | product: null-`m_Service` guard in `SendMessage`/`SendMessages` (RT-004); tests call `transports.SetOnline(false)` |
| TF-002 | `test-tunnel-endpoint-fragments` | segfault | same pre-`Start()` send from the router-delivery path; additionally the checksum-IV suffix write was unguarded | product: capacity guard before the IV copy (RT-005); test offline switch and exact incomplete-message counts |
| TF-003 | `test-tunnelpool-build-backoff` | segfault | same pre-`Start()` send | test offline switch |
| TF-004 | `test-tunnelpool-zero-hop` | `zero-hop replacement tunnels were not published` | a zero-hop outbound path has no reply hop but creation/`Recreate` required an established inbound tunnel | product: shared `OutboundPathNeedsInboundTunnel()` predicate |
| TF-005 | `test-tunnelpool-reuse-policy` | `!old->IsHighBandwidth ()` | test built the "old, low-bandwidth" peer with caps `O` (128-256 KBps, high) | test uses `L`; no product flag needed |
| TF-006 | `test-routerinfo-java213-parsing` | `router.IsUnreachable ()` | test expected descriptor rejection for a signature-valid RI whose only address is unusable; DB-119 keeps it parseable/storable for 54 minutes | test asserts `!HasValidAddresses()`, `!IsPublished()`, `!IsReachableBy()` |
| TF-007 | `test-routerinfo-update-transaction` | `!router.Update (emptyBuffer, 0)` | the "no usable transport" body was signature-valid; `Update()` mirrors the parse level, and the address policy belongs to netDb | test expects acceptance, then pins a genuinely malformed body |
| TF-008 | `test-netdb-persistence` | `ClassifyRouterInfoFile ("/data/netDb/r/<hash>.dat") == Load` | the fixture omitted the canonical `routerInfo-` prefix | test uses the canonical name; a prefix-less name is now asserted `Foreign` |
| TF-009 | `test-artifact-storage` | `WriteArtifact (RouterInfoCache, ...)` | the direct fallback had no data directory, so the no-follow write failed before the fixture | test initialises `i2p::fs::DetectDataDir`; three delegated-write cases were previously vacuous |
| TF-010 | `test-ssu2-address-negotiation` | `router->IsUnreachable ()` | test expected whole-descriptor rejection for an unusable SSU2 address/MTU | test asserts the address-level contract; descriptor stays parseable |
| TF-011 | `test-ssu2-control-plane` | `!parsed.IsUnreachable ()` | the test log echo showed `Unexpected character , instead ';' after 4`: the SSU2 `pq` preference list was serialized as two length-prefixed values around a raw comma, so every reader rejected the RouterInfo | product: one `WriteString("4,3")` value; wire form pinned (TF-038) |
| TF-012 | `test-leaseset2-enc-types` | `!parsed.valid` for `"4,6"` | hybrid+X25519 (`6,4`/`4,6`) is the shipped default and valid; only two hybrids or hybrid+ElGamal are invalid | test moved those to the positive block |
| TF-013 | `test-leaseset-encrypted-freshness` | `olderOuter->IsValid ()` | the fixture appended the inner salt/plaintext outside the region `lenOuterCiphertext` declared, so the signature covered a longer prefix than the reader reconstructs (`LeaseSet2: Verification failed`) | test: reserve the layer-1 region; negative control (TF-039) |
| TF-014 | `test-i2cp-b33-lookup` | `addr` null | `AddressBook::GetAddress()` read the final label after the `.i2p` suffix only, so `<b32>.b33.i2p` yielded the literal `b33` | product: strip an explicit `.b33` before `GetFinalLabel` |
| TF-015 | `test-i2ptunnel-http` | `parser.Output () == expected` | (a) `pgrade` byte search missed mixed-case `uPgRaDe`; (b) the `Proxy-Connection`-only expectation omitted the synthesised `Connection: close` | product: `HasConnectionUpgradeToken()` in the tunnel parser and `HTTPProxy`; test pins both |
| TF-016 | `test-sam-control-framing` | `!ParseSAMParams ("ID=a ID=b").duplicate` | assertion was inverted; `Mapping::Insert` already reports the duplicate | test |
| TF-017 | `test-streaming-sendstate` | `result.called` | one `poll()` pass was assumed to deliver the posted completion | test drains the io_context within a bound |
| TF-018 | `test-proxy-auth` | wrong-opaque returned `BadNonce` | the same function had erased the nonce in an earlier expired-nonce case | test reorders and pins `StaleNonce` before the opaque comparison |
| TF-019 | `test-http-proxy-limits` | `origin.IsValid ()` | `ParseAuthority` returned `BadPort` for a port-less `Host` (origin form), rejecting ordinary proxy requests | product: port defaults to 80; CONNECT still requires one |
| TF-020 | `test-addressbook-actions` | `!fake->identities.contains (feedHash)` | the retracted destination is still referenced by two other names, whose identity file must be retained | test; new case pins deletion of an unshared destination |
| TF-021 | `test-torrents-security` | `have != false` | Boost tribool `!=`/`!` yield tribool, not bool | test uses `indeterminate()` + explicit conversion |
| TF-022 | `test-torrents-verify-parity` | `!boost::logic::indeterminate (have) && !have` | same tribool misuse | test |
| TF-023 | `test-torrents-tracker-schedule` | `ScheduledWait (...) <= 60 * 1000` | a `failure reason` response that also carried `interval` overwrote the 15 s-30 min ladder | product: schedule only in the success tail |
| TF-024 | `test-crypto-x25519-noise-elligator-mlkem-audit` | `GetPubicKey ()` mismatch | the test reused buffers that the fail-closed null-sibling branch had zeroed, so it built a decryptor from an all-zero scalar | test uses scratch buffers; product rejects an all-zero scalar on load |
| TF-025 | `test-eciesx25519-ratchet-session` | `offset + size <= innerLen` | the test parsed AEAD ciphertext as plaintext block headers | test mirrors the receiver key schedule and decrypts first |
| TF-026 | `test-routerinfo-firewall` | `!IsSafeIdentifier ("accept")` | `accept` is an nft verdict but was missing from the reserved list | product: reserved words extended; malformed vs non-public firewall endpoints separated |
| TF-027 | `test-privsep-authorization` | `IsReplyType (type) \|\| type == WorkerHello` | `Ping` was declared receivable by the core and `Error` only by three roles | product: registry rows and compile-time direction invariant |
| TF-028 | `test-privsep-supervisor` | `elapsed < STABLE_PERIOD * 1000` | the eight-wait ladder totals 61.75 s (capped eighth wait), not <60 s; the test copied an inaccurate class comment | product: `TotalLadderDelay()` + static_asserts, terminal `NoteStable`; test pins the real bound |
| TF-029 | `test-privsep-windows-ipc` (MSVC) | `channel.Send (WorkerHello).has_value ()` | the test writes the hello while the server is rejecting on PID/session and closing the pipe; MSVC loses that race, MSYS2 won it | test tolerates the racing write and keeps asserting the server rejection |
| TF-030 | `perl-portability` / `perl-doc-links` / `docs-nav-static` / `perl-maintenance-checkers` | path separators and empty diagnostics | CE-059 | checkers normalise and merge streams |
| TF-031 | `android-locale-policy` | `CRLF in ...` | CE-061 | `.gitattributes` LF pinning |
| TF-032 | `perl-install-layout` | missing `i2pdctl.1` | CE-060 | man-page guard widened to non-Android |
| TF-033 | `tools-cli` / `tools-smoke` / `tools-regressions` | CE-062, CE-063 | product CLI output and reserved-range filtering | tools and docs corrected |
| TF-034 | `i2pd-version` | `did not terminate within 12 s` again on 4e776a39, empty output | the GetCommandLineW rewrite did not fix the hang: no byte reached either pipe, so the block is before the handler (pre-main/loader or an unobservable console path) | product: raw-command-line answer at the first WinMain instruction, no AttachConsole, stderr fallback, no CRT argv fallback; driver requires the banner only when captured; Windows workflow adds objdump/ldd/probe diagnostics (RT-010, Pending CI) |
| TF-035 | Debian `Remove and preserve, then purge` | CE-064 | unprivileged `test -f` inside a 0700 state directory | workflow and static contract use `sudo test` |

Executed evidence for this batch: MSYS2 passed 172/210 tests, MSVC passed 6/7
(one skip), Fuzzing passed its bounded smoke run. Linux/OpenBSD/sanitizer
lanes stopped at CE-055, so their CTest results are unknown. The table is not
a claim that the corrected revision passes.


## Current batch (`00929cd1`, run 41229371203)

| Test | Diagnostic | Root cause | State |
|---|---|---|---|
| `test-privsep-windows-ipc` | assert `delivered.has_value() && delivered->has_value()`, exit `0xc0000409` | core could not send the typed `Error` for a refused worker frame; max-payload test sent the wrong direction | corrected locally, RT-001 |
| `test-privsep-windows-supervisor` | assert `HandleCount () == baseline`, exit `0xc0000409` | root check ran after AppContainer creation, leaving one-time handles | corrected locally, RT-002 |

The other five MSVC tests passed (`test-tui-protocol`, `test-tui-terminal`,
`test-tui-pipe-name`, `test-tui-win32-pipe`,
`test-privsep-windows-isolation`). Linux/OpenBSD/MSYS2 did not reach CTest
because `test-i2np-dispatch-policy` failed to compile (CE-048); Fuzzing passed
its bounded smoke run.

## Packaging and integration

The Debian lifecycle failure is recorded as CE-051/RT-003. It is a package
verification failure, not a C++ unit test, but it must be rerun with the same
artifact discipline: inspect the exact `.deb` control archive before changing
maintainer scripts.

## Historical evidence

Older test failures (Windows IPC restart assertions, Debian remove/purge,
shared test namespace errors) remain in `COMPILATION-ERRORS.md` and
`COMPILATION-WARNINGS.md` with their run IDs. They are not evidence for the
current local corrections and must not be counted as current passes.

## Batch `dc5540ae` / `c1125a75` (runs 4124877xxxx and 41249881405)

The code revision `dc5540ae` is the first with the per-test log echo. Fuzzing
and Packaging passed; Linux/OpenBSD/sanitizers stopped at CE-066 before CTest;
Windows MSYS2 ran 213 tests with 10 failures, and MSVC ran 7 with two heap
corruption exits. `c1125a75` changes only documentation and the static
checker, so its code behaviour is the same for these entries except where
noted. The echo produced the root cause of the two previously open tests.

| ID | Test | First diagnostic | Root cause | Correction |
|---|---|---|---|---|
| TF-036 | `test-sam-control-framing` | long `MIN=3.0.0...0.1` expected `NOVERSION` | Java `VersionComparator` reads that as 3.0, so `OK VERSION=3.3` is correct; the case was written for the retired digit-concatenation comparator, and a sibling `MAX=3.1.5` case wrongly expected 3.2 | test: pin `OK/3.3`, genuine above-range `NOVERSION`, `3.1`/`3.2` for trailing bounds |
| TF-037 | `test-proxy-auth` | `header("00000002") == Accepted` | an RFC 7616 response is bound to `nc`; the test reused the published `nc=1` vector for 2, 3 and 5, so `BadCredentials` was correct and the count was not consumed | test: per-`nc` vectors plus an older-`nc` `Replayed` case |
| TF-038 | `test-ssu2-control-plane` | `Unexpected character , instead ';' after 4` | product wrote the SSU2 PQ preference list as two value strings around a raw comma; Java and `ExtractParam` require one length-prefixed value `"4,3"` | product: one `WriteString`; wire-form assertions |
| TF-039 | `test-leaseset-encrypted-freshness` | `LeaseSet2: Verification failed` | the hand-sealed fixture appended 32+lenInner bytes after the region its `lenOuterCiphertext` declared, so the signature prefix was longer than the reader's reconstruction | test: index inside the reserved region; negative control flips a ciphertext byte |
| TF-040 | `test-tunnel-endpoint-fragments` | `GetNumIncompleteMessages () == 2`; log `Buffer too small ... dropped` | the new endpoint guard compared the *declared* length to a full cell, but `NewI2NPTunnelMessage(true)` reserves the gateway header and declares 6 bytes less; every transit cell was dropped. Capacity is the memory-safety bound | product: capacity first, then a declared-length floor that allows the reserved header; pool-cell regression |
| TF-041 | `test-torrents-verify-parity` | `GetNumPeers (1) == 0` | `ParsePeers` mutated the live tracker slot before the whole bencode document validated, so a truncated response committed a partial list | product: parse into a scratch set and commit only on success; test asserts retained last-good list |
| TF-042 | `tools-cli`/`tools-smoke`/`tools-regressions` | `i2pbase64 ... after padding`; `routerinfo usage ... stderr`; `offlinekeys ... Can't create file` | `i2pbase64` decoded all complete quartets at once so `=` in the same chunk failed with the generic message; the routerinfo test searched for `argv[0]`-bearing text; two regression needles were stale after the stream/atomic-write changes | tools/tests: quartet-by-quartet decode with `data after padding`; path-independent usage assert; stderr/exit-code asserts for every writer; latent famtool cases corrected |
| TF-043 | MSVC `test-privsep-windows-isolation`, `test-privsep-windows-supervisor`, `test-privsep-windows-ipc` | isolation now passes; supervisor/IPC SEGFAULT; TUI `0xc0000374` | live-worker test never sent `WorkerHello`; spawn reuse/containment/stale-pipe holes; the remaining access violations are not explained by the audited ownership chains | product: production worker entry, per-attempt command line, unconditional containment, listener/pipe cleanup; crash needs PageHeap/minidump (RT-011, RT-012) |

Status: Implemented / static; Pending CI compilation; Pending CI tests.

## Current published revision (`56f17c0f`, runs 4136485xxxx)

These runs describe published SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`.
They do not validate the later local worktree changes. See linked run IDs in
the project tracking entry and runtime inventory for job-level details.

| ID | Test/job | First diagnostic | Root cause | Correction/status |
|---|---|---|---|---|
| TF-048 | Linux GCC/Clang, OpenSSL 3.x, run [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256), SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`; jobs 125022735576/125022735731 | `tools/test-tools-regressions.cmake:743`: `data after padding` | fixture repeated `qg==` (already padded) 1023 times, then appended another value; decoder correctly rejected concatenated padded values | Fixture now uses 1023 valid `qqqq` quartets and one terminal `AA==`, preserving the 4096-byte cross-read boundary; expected output is 3069 `0xaa` bytes plus `0x00`; Implemented / static, Pending CI |
| TF-049 | Linux GCC/Clang, OpenSSL 3.x, run [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256), SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`; jobs 125022735576/125022735731 | `test-streaming-sendstate.cpp:99`: second `Write()` never completes; log `Can't send packets, missing remote LeaseSet` | the fixture reused an `io_context` after `poll()` drained it and stopped it; queued work on a stopped context does not run until `restart()` | Test now restarts a stopped context before subsequent posts and replaces fixed poll-count loops with one ready-queue drain; read cancellation phases use the same guarded lifecycle; Implemented / static, Pending CI |
| TF-050 | Linux/OpenSSL 3/4/OpenBSD `test-blocklist-parse` | reason assertion mismatch | test fixture passed a temporary string to a parser returning borrowed views; view outlived backing storage | test fixture now names backing string; pending CI |
| TF-051 | Linux/OpenSSL 3/4/OpenBSD/sanitizers `test-i2pcontrol-socket` | three socket identity/replacement assertions | fixture closed the old bound listener before unlink/rebind; the filesystem could reuse its device/inode, falsely matching the replacement | fixture keeps original listener open through replacement cleanup; pending CI |
| TF-052 | Linux/OpenSSL 3/4 `test-datastorage-protocol` | expected relative-path rejection | encoder checked only framing bounds while decoder enforced absolute-root semantics | encoder now rejects empty/relative roots and fixture covers both; pending CI |
| TF-053 | Linux/OpenSSL 3 `test-datastorage-client` | restart retry expected to remain Busy after 250 ms, but write succeeded | `Configure()` reset the shared backoff on every successful worker handshake, so consecutive fast worker deaths never doubled their delay | removed reset from data/router worker configuration; Start and the quiet window still reset the policy; existing 250→500 ms regression pins it; pending CI |
| TF-054 | Linux `test-privsep-linux-ipc` | test expected empty record carrying SCM_RIGHTS to be accepted | Channel correctly closes the unauthorized descriptor and reports a protocol error for descriptor-bearing empty records; the fixture expected tolerance | regression now expects the error, checks descriptor count and channel reuse; pending CI |
| TF-055 | Linux `test-torrents-dht-persistence` | saved state differs from one peer record | the tunnel base constructor creates a local destination when passed null, so `BuildStateSnapshot()` persists its self record before the peer; malformed reload correctly retains both | test now validates the self record, peer bytes and retained snapshot; pending CI |
| TF-056 | Linux Clang/OpenSSL 4 run [41364858202](https://github.com/daviduhden/openi2pd/actions/runs/41364858202), job 125022744749, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-i2cp-session-lifecycle.cpp:106`: `destination.use_count() == 1` timed out after 30.1447s; sibling OpenSSL 3 and GCC/OpenSSL 4 lanes passed | Destination and I2CP member timers, posts and stored request callbacks could retain an owner through its stopped context/request map | Timers, owner posts, stored lookup completions, dropped-message retries, I2CP lookups, Streaming lookup callbacks, BOB lookup/ping/inbound-receiver callbacks and SAM b33 datagram continuations now use weak ownership. The test checks expiry after timer self-stop and a queued LeaseSet callback stranded in a stopped destination context; `check-control-security.cmake` pins the native callback seams. A raw-`this` session snapshot future was also changed to weak ownership. Request maps stay executor-owned; external Stop does not mutate them, and final callbacks complete in destruction. Implemented / static, Pending CI; there is no focused live SAM/BOB/Streaming lifetime regression, and I2CPServer lifecycle posts remain open |
| TF-057 | OpenBSD VM/LibreSSL 4.4.0 run [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438), job 125022789000, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-gost-sig.cpp:49`: BN context setup assertion | The test called `BN_CTX_get()` without the required `BN_CTX_start()` frame; other native GOST functions already bracket context temporaries | Added a balanced `BN_CTX_start()`/`BN_CTX_end()` pair and searched all direct test-side `BN_CTX_get()` call sites; Implemented / static, Pending CI |
| TF-058 | OpenBSD VM/LibreSSL 4.4.0 run [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438), job 125022789000, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-clientcontext-registry.cpp:97`: `snapshots.load() > 0` | readers were launched, then deletion/shutdown happened before the scheduler guaranteed any reader ran; the assertion was scheduling-dependent | Test now barriers after each reader has checked a complete snapshot and before mutation/shutdown; Implemented / static, Pending CI |
| TF-059 | OpenBSD VM/LibreSSL 4.4.0 run [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438), job 125022789000, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-tui-ipc-regression.cpp:472`: 16 immediate connections did not produce `queueFull` | The fixture assumed `listen(fd, 4)` yields a fixed effective backlog; backlog is advisory and kernel-capped, while the client retries nonblocking connects | Replaced queue saturation with `RetryUnixConnectUntil` using fake clock/connect/sleep inputs. Regression pins deadline expiry, EINTR retry and immediate nonretryable errors; the real test retains successful connect/request/reconnect coverage. Implemented / static, Pending CI; no attempt count or timeout was increased |
| TF-060 | OpenBSD VM/LibreSSL 4.4.0 run [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438), job 125022789000, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-privsep-protocol.cpp:427`: maximum `RouterStorageRequest` transport send failed; exact send error was not emitted and run has no diagnostic artifact | The fixture used a 32,785-byte AF_UNIX `SOCK_SEQPACKET` record despite testing only that post-refusal receive capacity exceeds the 16-byte header. Kernel atomic-record limits vary; this is a portability hypothesis, not confirmed `EMSGSIZE` | Recovery fixture now sends a 17-byte frame, which detects accidental header-only capacity; max-payload framing remains independently tested by the codec boundary cases. Implemented / static, Pending OpenBSD CI; exact original errno remains unknown |
| TF-067 | OpenBSD `test-datastorage-protocol` | relative-path rejection assertion | same producer/consumer semantic mismatch as TF-052 | encoder fix applies; pending OpenBSD CI |
| TF-068 | Linux/OpenSSL 3/4 `test-datastorage-client`, OpenSSL 3 run [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256) job `125022735731`, OpenSSL 4 run [41364858202](https://github.com/daviduhden/openi2pd/actions/runs/41364858202) job `125022744749`, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-datastorage-client.cpp:169`: a request unexpectedly succeeded 250 ms after the second worker death, where the fixture expects the doubled 500 ms backoff | Exact-SHA source still resets the budget in DataStorage Configure; removing that reset exists only in the local diff. The published failure therefore matches TF-053 rather than contradicting the corrected local flow | Implemented / static: preserve the local reset removal, add stable-generation reply authority and deterministic policy regressions; Pending CI compilation; Pending CI tests |
| TF-069 | Linux GCC/OpenSSL 3/4 `test-eddsa`, runs [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256) job `125022735576` and [41364858202](https://github.com/daviduhden/openi2pd/actions/runs/41364858202) job `125022744601`; OpenBSD [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438) job `125022789000`; SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | Positive custom Ed25519 verification assertion at `tests/test-eddsa.cpp:170` fails on GCC and OpenBSD; Linux Clang OpenSSL 3/4 passes the same vector | Exact-SHA fixture supplied 32 bytes to a 64-byte SHA-512 writer and verifier reader; corrected locally to return an owning 64-byte array. Primitive code remains unchanged; association with historical failures is pending CI | Implemented / static; Pending CI compilation; Pending CI tests. Rerun the corrected fixture on all affected lanes and retain symbolized artifacts if the failure persists |
| TF-061 | Windows MSVC and MSYS2 `test-tui-win32-pipe`, run 41364870388, SHA 56f17c0f | Both jobs fail the first-instance ERROR_ACCESS_DENIED assertion; original numeric error absent | Instance quota can confound exclusivity, but this is a static hypothesis | Fixture now leaves capacity for a control instance and captures/prints the immediate error; exact denial assertion preserved. Implemented / static; Pending CI tests |
| TF-062 | Windows MSVC `test-privsep-windows-isolation`, `test-tui-protocol`, `test-tui-pipe-name` | heap corruption / fast-fail (`0xc0000374`, `0xc0000409`) | intermittent or deterministic allocator corruption is not attributable from ordinary CTest output | Open; PageHeap/minidump required |
| TF-063 | Windows MSYS2 `tools-cli`, `tools-regressions`, `test-tui-win32-pipe` | failures; `i2pd-version` timeout | tools-cli exact output 414141410d0a contradicted LF-only 414141410a; other failures remain independent | NUL fixture now uses newline-free 41414141; pipe follow-up TF-061. Implemented / static; Pending CI tests; version timeout and remaining failures stay open |
| TF-064 | Sanitizer `test-blocklist-parse`, `test-torrents-dht-protocol` | ASan/TSan borrowed-view use-after-lifetime in test fixtures | temporary concatenated strings backed parser results containing string views | fixtures now retain backing strings; pending sanitizer CI |
| TF-065 | Validation `C++ source API documentation` | Doxygen: explicit link request to `bind` at `libi2pd/util.h:91` could not be resolved; warnings are fatal | plain `std::bind` was interpreted as an unresolved explicit link | changed to `\c std::bind`; pending documentation CI |
| TF-066 | Validation `Perl quality` | Perl::Critic exits 2 with repeated `RequireExtendedFormatting`, `ProhibitComplexRegexes`, `RequireFinalReturn`, I/O and local initialization findings | first-party findings across tracked Perl scripts/tests; published output omitted filenames, so exact source attribution is unavailable | `.perlcriticrc` now prints file/line/column/policy; pending CI to map and correct findings |

The table intentionally preserves unresolved causes as unresolved. Runs are
historical evidence only; none exercised the local corrections.

## Batch `4e776a39` (runs 41320539612/45934/42948, 2026-11-01)

| ID | Test | Diagnostic | Root cause | Correction |
|---|---|---|---|---|
| TF-044 | `tools-regressions` | `i2pbase64 decodes NUL: expected hex '00', got ''` | the fixture names `nul.b64`/`nul.bin` are the Win32 NUL device in every directory, so the tool read EOF and wrote nothing; the same trap made the `b33address` NUL case pass vacuously | test: rename to `nul-byte.*`/`nul-input.b64`, pin the fixture bytes, add a reserved-device-name guard, split padding across reads |
| TF-045 | `test-tunnel-endpoint-fragments` | `GetNumIncompleteMessages () == 0`, line 331 | the new pool-cell case crafts a fragmented router-delivery first fragment, which legitimately stays in the incomplete table until a follow-on arrives | test: expect 1 |
| TF-046 | MSVC `test-tui-protocol` | `Exit code 0xc0000374` | unresolved; sources and parser are unchanged from the passing revision, the binary links only `Log.o` | test: guard the unguarded `expected` dereference and pin both bounded-container seams; crash needs PageHeap/minidump (RT-012) |
| TF-047 | `test-datastorage-protocol` | compile error CE-069 | `std::string` passed to a byte span | test: borrowing `Bytes()` helper |

The per-test log echo again identified causes without a debugger. The Windows
version hang remains open with explicit diagnostics. The MSVC access
violations did **not** reproduce on the `41325980409` rerun of the same SHA
(the whole MSVC job passed), so they are recorded as intermittent and still
need PageHeap/minidump evidence if they return. No local build, CTest,
sanitizer or workflow dispatch exists.

TF-069 static follow-up (2026-11-02): exact-SHA source inspection found
that the test supplied a 32-byte digest array to a 64-byte SHA-512 writer
and verifier reader. The fixture now obtains an owning 64-byte result.
This is a demonstrated fixture defect, not evidence of a primitive failure.
The assertion and RT-027 remain open pending manual CI on the correction;
see the RT-027 static follow-up for the equivalent-pattern review.

TF-070 (2026-11-04 static): TSan `test-log-lifecycle` lost
`local-after-sink` during `SetSink(nullptr)` → `SendTo(stream)` →
`Start/Stop`; no product defect was demonstrated by inspection. The remaining
suspects are the 1 s `Queue::Wait` wakeup and the sticky `m_StartRequested`
flag. TSan `test-runnableservice-lifecycle` and
`test-i2cp-session-lifecycle` also hit 2–5 s join/`use_count` budgets; the I2CP
implementation changed after the published run, so they require a rerun before
classification. No test was executed for this note.

TF-071 (2026-11-04 static): TF-065's `\c std::bind` did not reliably remove
the Doxygen autolink token. The source comment now says "type-erased deleter"
with no `std::` token; documentation CI remains the evidence gate.

TF-070 executor follow-up (2026-11-04): source comparison against
56f17c0f demonstrates an unguarded empty-run race in RunnableService and the
same launch-before-post ordering in NTP. Per-run work ownership, normal-return
exit and NTP queue retirement are Implemented / static. Existing registered
lifecycle targets now prepare idle-gap/restart/drain/exception and deterministic
empty-run regressions. No timing budget was increased. This does not resolve
the separate logger assertion, I2CP ownership or historical leaks; see RT-031.

TF-070 logger publication follow-up (2026-11-04): the exact-SHA Log::Start
never resets m_StartRequested between joined writer generations. A new Run
can therefore pass its condition wait on the old true bit, observe the still
false running flag, drain an empty queue and exit before Start publishes true
and its caller appends local-after-sink. Start now re-arms the signal before
launch under lifecycle/start locks. Repeated destination-change regressions
and a source-order contract are prepared. This demonstrated source
interleaving explains the observed symptom; runtime confirmation is Pending
CI. The queue wakeup and other lifecycle hypotheses are not declared resolved
by this static correction.

## Published revision `14f224e6` (2026-11-07)

Exact run/job links and SHA are recorded in COMPILATION-ERRORS.md. Local
corrections are Implemented / static, Pending CI compilation and Pending CI
tests; no local behavioral suite was executed.

| ID | Observed evidence | Correction / remaining validation |
|---|---|---|
| TF-074 | OpenBSD 42402987408 / MinGW 42402984632: EdDSA assertion at line 145 | RT-038 full nonce reduction; strict comparison retained |
| TF-075 | Same platforms: capability fixture expects a grant for forbidden Exec | Assert zero Exec grants and complete withholding; production capability masks unchanged |
| TF-076 | Windows 42402984632: POSIX WIFEXITED/WIFSIGNALED unimplemented in native Perl | Decode Perl wait words portably in three suites; check full pipe status in vector/branding helpers; signal-with-valid-JSON fixtures prepared |
| TF-077 | OpenBSD 42402987408: validator fixture cannot execute git init -b main | Install Git in VM; declare Git 2.28 test dependency and put its directory in that CTest environment |
| TF-078 | OpenBSD failing action provides no usable CTest artifact | Pinned action exits before copyback; separate prepare action and documented remote custom shell preserve copyback and original failure status |

The custom shell behavior was checked against the pinned
[vmactions implementation](https://github.com/vmactions/openbsd-vm/blob/f1ca8bc8ecc5b6ef7218e5b914836e067e1546d3/index.js)
and its documented custom-shell interface. Negative workflow fixtures pin
failure-preserving upload structure and prohibit workflow_call triggers.
TF-073 recurs in all three sanitizer jobs of 42403004301 through CE-078.
No new sanitizer runtime result is available. Optional Perl quality and API 29
UI lanes are skipped; those capabilities remain unvalidated. Existing MSVC
heap failures and MinGW startup timeout remain open under RT-032.

## Follow-up on published `68c16e10`

| ID | Exact observed failure | Correction / status |
|---|---|---|
| TF-079 | Perl portability test 210 across SSL3/4, OpenBSD and all sanitizer jobs | Scanner matched `open` inside quoted `openbsd`; require the complete keyword, retain unsafe pipe fixtures and add a safe literal control. Implemented / static; Pending CI tests |
| TF-080 | Linux IPC TestPeerExpectation line 299 in SSL3/4 and ASan | Fixture set configured=false before asking explicit UID rejection. Use designated configured fields; preserve UID/GID negatives and add the legacy kernel-authenticated round trip. Production checks unchanged. Implemented / static; Pending CI tests |
| TF-081 | PageHeap 42407544860 emits syntax error for CDB `q 1` after the useful stack | Use supported `q`; independently fail on OPENI2PD_CRASH or missing debugger log, with negative source fixtures. Implemented / static; Pending CI tests |

Validation 42414235478 ASan/UBSan has six failing tests: TF-079, TF-080,
i2ptunnel-stop, i2ptunnel-targets, streaming-sendstate and torrents-security
(RT-042/043). TSan has TF-079 plus RT-044; UBSan has TF-079 only. These are
runtime/test failures after successful compilation, not compile blockers.

Earlier TF-074/075/076/077 now pass the corresponding executed crypto,
capability and Perl/Git fixture paths. OpenBSD TF-078 artifact copyback is
confirmed: exact-run artifact `13591246747` contains configure.log, build.log,
JUnit and Testing data despite CTest failure. Three tests are skipped in JUnit:
worker identity and credentials under root, plus torrent storage pending an
exec-free broker. LastTest's summary wording must not override JUnit notrun.
Skipped worker startup gives no later handshake/shutdown evidence.

### TF-082 — Native Win32 fixture output changed LF into CRLF

Windows 42414215883 MinGW job `128528682471`, perl-maintenance-checkers
assertions 243/244: generated i18n catalogs fail escaped/newline-key controls.
The shared fixture writer used text output; native Windows inserted CRLF.
Use raw writes to preserve intended bytes, retaining explicit CRLF-negative
fixtures. Implemented / static; Pending CI tests.

### TF-083 — Native Win32 path and symlink capability boundaries

Same run/job, perl-agent-knowledge-contract: Unicode worktree-root subtest 36
fails resolution and JSON PASS assertions; subtest 39 runs no assertions when
core symlink throws an unimplemented-operation exception. Trap only symlink
creation and report an exact capability skip, preserving checks on capable
hosts. Implemented / static; Pending CI tests for that capability handling.

Unicode worktree paths remain **Blocked**. Git UTF-8 output, decoded Perl
strings and native Win32 filesystem/process paths require a lossless bridge.
Core GetANSIPathName can substitute question marks when short aliases are
unavailable; silently applying it would jeopardize path-boundary checks.
The test stays enabled and adds bounded failure-only diagnostics for its
private fixture paths. No CLI coercion, CPAN dependency or blanket skip was
introduced. Required follow-up: inspect those exact byte/path diagnostics and
implement a verified round-trip native-path/process bridge before claiming
Unicode portability. Reference: [Perl Win32 filename API](https://perldoc.perl.org/5.40.3/Win32#Win32::GetANSIPathName(FILENAME)).

## 2026-11-08 — Published `8ffd1ad8` and focused `9f5320b2`

### TF-084 — Unknown-type fixture reused a now-registered IPC value

OpenSSL 3/4 jobs and all three sanitizer jobs fail
`test-privsep-linux-ipc.cpp:465`, TestMalformedFrames. Type 7 is
ConfigureTorrentStorage, so a raw framing channel correctly accepts it.
Derive the first unknown value from MAX_MESSAGE_TYPE and check the registry;
retain reserved-zero and 0xffff rejection plus channel-reuse controls. Add
explicit acceptance of known framing, without changing endpoint authorization
or production decoding. Implemented / static; Pending CI tests.

The earlier configured-peer expectation case runs before this assertion and
passes; this does not mark the whole IPC binary validated. Ordinary TSan also
fails the separate provider-cache case, while ASan/UBSan and UBSan have only
TF-084 in the new batch. OpenBSD completes all 246 CTest entries without a
failure, but worker/broker capability skips remain unexecuted paths.

### TF-085 — New failure regressions prepared, not executed

Existing bencode and verification suites now cover recognized field types,
transactional handshake/tracker failures and peer-ID provider failure. The
RPC metadata fixture additionally checks internal error 4 maps to existing
wire error 3 plus a string, and control snapshots retain that string.
All are local prepared regressions, not newly observed CI failures.

### TF-086 — Seed uniqueness assertion admitted legitimate random failures

Static probability review found the old 512-sample test required all 32-bit
seeds to differ; a real birthday collision could fail it without a defect.
Replace that requirement with deterministic source/boundary/failure/cleansing
regressions and a provider non-constant control that permits collisions.
No historical failure is attributed to this hypothesis. Implemented / static;
Pending CI tests. No retry or timeout hides it.

TF-079 portability scanning, TF-082 raw catalog fixtures and narrow symlink
capability handling pass their executed new paths. TF-083 Unicode resolution
still fails in Windows 42419357644: the retained diagnostics show intended
UTF-8 `c3a9` versus Git's `c383c2a9` representation at the failing root.
That identifies a boundary mismatch, not a safe license to coerce paths.
The Unicode test remains enabled; a lossless native path/process bridge is
still Blocked. MinGW's other failure is the GUI informational timeout.

Focused TSan 42422405960 on `9f5320b2` passes 1/1; it does not validate the
local full-matrix dependency refactor, provider failure changes or parser fixes.

## 2026-11-08 — Tests after the c115d386 continuation

### TF-087 — Scheduling fixture expected malformed known types to succeed

The tracker-schedule assertion at line 167 fails in OpenBSD, MinGW, all four
OpenSSL jobs and all three sanitizer jobs (run/job table in the compilation
inventory). Its empty byte string interval contradicts the now-executed typed
parser contract. The fixture now asserts malformed response, retained prior
interval and bounded failure scheduling. Negative integer intervals still
exercise valid typed clamping. The parser is not weakened to satisfy the old
expectation. Equivalent known-type fixtures, DHT, PEX and metadata handlers
were inspected. Implemented / static; Pending CI tests.

### TF-088 — Release assertion evaluation needs a direct regression

A successful MSVC suite still emitted CW-026. A shared source-level assertion
setup removes the command override while retaining evaluation. The new
common/selected test fails if NDEBUG remains defined or an assert operand
stops executing. Normal tests, tool tests and fuzzers use the same helper.
Prepared / not executed; Pending CI compilation and Pending CI tests.

RT-051 and RT-052 remain real product/CLI failure-path defects rather than
fixture expectations. Their regression extensions stay enabled. The older
unknown-opcode case now passes far enough to expose the independent EOF case.
MinGW Unicode and GUI version timeout remain unresolved; MSVC still skips
supervisor authentication after worker status 0xC0000142. API 29 remains
skipped. No passing sibling closes those coverage gaps.

## 2026-11-08 — Failures exposed after prior fixture corrections

### TF-089 — EOF regressions violated the Ping payload contract

Validation 42431903303 jobs 128582436615, 128582436820 and 128582436891,
and both OpenSSL compiler
matrices (run/job links in COMPILATION-ERRORS.md), SHA c3f5df53, fail
`test-privsep-linux-ipc.cpp:649`: sending Ping with payload "last" is rejected.
The registry permits zero payload bytes for Ping, Pong and Shutdown. A valid
empty-payload frame still has a nonempty transport record containing its
header. The test failed before reaching buffered EOF authentication.

Both buffered-close and wrong-identity-close fixtures now send a valid Ping.
The first explicitly rejects the old illegal payload, then sends a valid
frame on the same channel and checks type, request ID and empty payload.
Global searches found exactly these two nonempty control-message fixtures.
Production encoding, authentication, descriptor and EOF checks are unchanged.
Implemented / static; Pending CI tests. RT-052/RT-054 negative paths must
still execute on the revised source before closing their validation tasks.

### TF-090 — Compact destination fixture used the torrent info-hash width

The same sanitizer/OpenSSL runs, OpenBSD 42431852556 job 128582289517 and
MinGW 42431789062 job 128582095295 fail tracker-schedule.cpp:197 on c3f5df53.
The fixture allocated 20 bytes from infoHash.size(), but compact I2P peers
carry 32-byte destination hashes. RT-055 correctly rejects that malformed
range before committing the expected interval.

Use a typed IdentHash fixture. Added explicit 20-byte rejection checks retain
prior interval/counters and bound retry scheduling; a subsequent valid reply
clears the failure and restores normal scheduling. Valid minimum/maximum
interval cases remain. Equivalent HTTP/datagram compact-peer fixtures were
inspected, including existing tail-length rejection coverage. No production
parser tolerance is widened. Implemented / static; Pending CI tests.

The latest Perl lane passes 1230 assertions but fails its separate analyzer
step (CW-028). Windows Unicode conversion, GUI version timeout and worker
initialization remain unresolved; API 29 UI results are incomplete. Existing
negative tests and diagnostic failures remain enabled.

## 2026-11-08 — 0b7f0ab4 fixture and packaging outcomes

### TF-091 — Documentation growth changed debhelper compression

Packaging run [42440572453](https://github.com/daviduhden/openi2pd/actions/runs/42440572453),
Debian job 128608728103, SHA 0b7f0ab4 builds and installs both packages, then
DEP-8 fails `missing file: /usr/share/doc/i2pd/docs/i18n.md`. The exact uploaded
Debian data archive contains docs/i18n.md.gz and not the plain form. Its control
archive was inspected; this is not a maintainer-script failure or missing
CMake install payload.

Smoke now accepts plain or .gz representations for compressible Markdown
policy/README/palette documentation. Missing both forms still fails. License
paths and canonical changelog.gz remain exact. Analogous fixed compressed
expectations were reviewed, and the packaging source contract pins the
paired existence check. No compression policy or install manifest is weakened.
Implemented / static; Pending CI tests on the corrected package smoke path.

### TF-092 — Compilation and cancellation hide runtime verification

The latest sanitizer/OpenSSL lanes stop at CE-086; Android/OpenBSD/MSYS2 stop
at CE-085/CE-087. Therefore TF-089/TF-090 and RT-058/RT-059 runtime paths are
not validated by this batch. New atomic ownership, signed bit/encoding bounds,
Javadoc corpus, Android guard fixtures and Windows deadline controls are
Prepared / not executed locally and Pending CI compilation / Pending CI tests.

Android UI emulator/instrumentation never starts after its failed triplet
configure. Cancellation happens later; its cause is not inferred. MSVC is
canceled while compiling, and its always-run heap step finds no diagnostic
binary. The 294-byte artifact contains only debugger commands, not an executed
heap trace. Earlier 0xC0000142 and MinGW Unicode/version failures stay open.

The source-only static job and documentation/fuzz sibling jobs pass on the
remote SHA; Perl's seven executed suites pass 1270 assertions. Those results
do not run the new local eight-assertion corpus or resolve the failing Critic
step and cannot be attributed to later edits.

## 2026-11-08 — Executed `6390fdea` test inventory

Exact run/job/SHA references are recorded in
[the compilation continuation](COMPILATION-ERRORS.md).

### TF-093 — Standalone Perl contract omitted from independent discovery

OpenSSL 3/4, OpenBSD and all three sanitizer lanes fail the portability
inventory equality assertions at tools/tests/perl-portability.t:329/339
(assertions 609/610 on the sanitizer runners). MSYS2 reports the same cause
at its platform-dependent assertion numbers. CMake includes
`tests/android-javadoc-regex.t`, but both the explicit maintained inventory
and independent source discovery omit top-level `tests/*.t`. The first
mismatch is that path versus `tests/interop/check-vectors.pl`.

The explicit inventory now includes the Javadoc contract and independent
discovery scans top-level `.t` files. Exact bidirectional equality remains;
no test is removed or relaxed. Analogous script/module/subdirectory
inventories and both syntax/Critic consumers were reviewed. Existing CTest
and manual sanitizer/platform lanes exercise the correction. Implemented /
static; Pending CI tests. No local Test::More suite was executed.

### TF-094 — Native Windows paths bypass trusted source harvesting

MSYS2 42492792058, job 128768581923, fails eight assertions in
`perl-maintenance-checkers`: four missing-label fixtures return success
instead of `missing source literal`. `harvest()` in the native catalog
checker matches `/tui/main.cpp` and terminal suffixes directly against
File::Spec native paths. Backslash separators bypass tab/help/option/error
collection while ordinary catalog parity still passes.

The checker now derives a repository-relative slash identity with
abs2rel/splitdir solely for source selection. Native read paths and rendered
user operands remain unchanged. Both terminal adapters and an explicit
untranslated rendered-data control are covered in the existing fixture
suite. Related path-sensitive checkers were searched; no additional
same-form defect was demonstrated. Implemented / static; Pending CI tests.

The same MSYS2 job still fails the historical Unicode validator fixture and
an empty-output GUI version probe after 12 seconds. These remain separate,
unresolved findings; increasing budgets or skipping fixtures is not a fix.
Optional Perl quality job 128768654939 and Android UI job 128768637533 skip,
so no new Critic or device-validation result is available.

## 70f66977 blocked regression execution

CE-088 blocks the newly registered readiness and downstream native tests;
CE-089 independently blocks both TSan builds. Android unit/Lint/device steps
were not reached. Historical runtime families and optional-lane gaps remain
open. Packaging 42512607518 passes its five distinct jobs, and documentation
and agent-knowledge siblings pass at the executed SHA. Those results do not
validate local source changes or the later ab021821 launcher-only commit.

## TF-095: core Carp omitted after expanded Perl discovery

Observed at `9f2934f81edd16c6bcb9b3ebbac91474c7b318a1` in
[Validation 42545638623](https://github.com/daviduhden/openi2pd/actions/runs/42545638623),
Linux OpenSSL 3/4, OpenBSD 42545614356 and MSYS2 42545610680.
The perl-portability import assertion rejects `Carp`, imported by the
previously uncovered tests/android-javadoc-regex.t. Carp is a core Perl
module; the checker allowlist was incomplete, not the dependency policy.
The local allowlist correction retains the existing per-import regression.
Global inspection found one owning module allowlist. Implemented / static;
Pending CI tests. No dependency was added or interpreter requirement changed.

## TF-096: slow log-peer fixture assumes per-record readiness

At the same SHA, OpenSSL 3/4 GCC/Clang and Validation ASan+UBSan, TSan and
UBSan fail `test-privsep-log.cpp:953`, assertion `sink.IsOpen ()`.
Validation jobs are 128929923766, 128929923845 and 128929923871.
A peer draining one record per 400 ms does not guarantee POLLOUT before the
production one-second timeout: several records can be required by the
kernel readiness threshold. LogSink correctly detaches on timeout; this
assertion alone is not evidence of a production mutex or memory defect.

The prepared fixture fills the queue via actual zero-budget send failure,
parks the peer behind a control pipe, checks concurrent counter access and
then drains all queued records. Futures are joined and the child reaped
before final outcome assertions. The production timeout is unchanged.
Implemented / static; Pending CI tests. Scheduling-sensitive test behavior
must still be assessed by the same runner/toolchain lanes.

## 9f2934f8 positive evidence and remaining platform gaps

Readiness, deadline-mutex and torrent verification regressions pass all
three executed sanitizer configurations and OpenBSD. MSVC's selected suite
passes. Android job 128929903685 has DisplayTextTest 3/3 and confirmation
13/13, no failures; device job 128929904898 skips. OpenBSD's three worker
capability skips provide no later handshake evidence and emit no reason.
Windows still has Unicode fixture and GUI-version failures: the Git root
round-trip turns UTF-8 resume-with-accents into double-encoded mojibake,
then native abs_path fails; the console control reports its version while
the GUI probe times out with empty output. These remain separate open
findings, not resolved by passing MSVC sibling tests.

## TF-095/096: validated in the executed 80a487d5 lanes

[Validation 42553097403](https://github.com/daviduhden/openi2pd/actions/runs/42553097403)
passes all three sanitizer jobs. Perl portability and the controlled logger
fixture execute successfully; OpenSSL 3/4 and OpenBSD workflows also pass.
Child-stop budget, torrent availability and rate-scheduler controls pass in
the executed suites. TF-095/096 are Validated by CI at this SHA; pure budget
arithmetic is not evidence for every kernel shutdown path.

## TF-097: MSYS2 lane discovers an unrelated native Perl

[Windows 42553068552](https://github.com/daviduhden/openi2pd/actions/runs/42553068552),
job 128952711384, at the same SHA fails perl-agent-knowledge-contract's
Unicode Git-root round trip. The configure log finds
C:/Strawberry/perl/bin/perl.exe 5.32.1; the MSYS package inventory does not
contain Perl. Native Perl's CP_ACP filesystem/process boundary does not
match the UTF-8 contract, and the fixture also includes Chinese names that
cannot be repaired by a CP1252-only adapter. Decoding Git output twice or
skipping these cases would hide the mismatch.

The local workflow declares the MSYS perl package and passes its cygpath
interpreter path to CMake. Every i2pdctl TAP invocation now uses the selected
Perl directly, removing incidental prove-wrapper interpreter selection while
retaining the same Test::More assertions. Source guards pin the interpreter
boundary. Implemented / static; Pending CI tests. Native MSWin32 Unicode
support remains a separate limitation; an MSYS pass will not validate it.

The GUI i2pd-version timeout remains open. The TUI control does not link the
router libraries and cannot exclude router loader/static-initializer issues.
The exact artifact lacked the GUI image and matching DLLs. The prepared
workflow now retains them with PE/import/runtime resolution and CMake cache
before probing. This improves attribution; it is not a GUI startup fix.

## TF-098: YAML package metadata misclassified as interpreter stdin

Observed at `d63b6b9075e510d469cff1aab2dcdd6e664ea4d5` in
[knowledge 42667583150](https://github.com/daviduhden/openi2pd/actions/runs/42667583150),
job 129335273970, and OpenBSD, OpenSSL 3/4, Windows MSYS2 and all three
Validation sanitizer jobs. Diagnostic: build-windows.yml:49, interpreter
source from standard input is forbidden. The line is the literal package
name perl inside setup-msys2's install scalar, not executed code.

The local scanner masks only literal package lists in direct action inputs,
preserving line numbers. It continues checking executable run blocks,
unknown actions, nested maps and executable-looking package operands.
Accepted/rejected YAML fixtures pin these boundaries and secret-free
source diagnostics. No workflow workaround or warning suppression.
Implemented / static; Pending CI tests.

## TF-099: Cygwin executable suffix hides an earlier POSIX TLS shim

Windows MSYS2 job 129335208335 in
[42667563721](https://github.com/daviduhden/openi2pd/actions/runs/42667563721)
now uses MSYS Perl 5.42.3, exposing the i2pdctl TLS fallback fixture. The
runtime reports Config::_exe as .exe, while the intended fake OpenSSL is
an extensionless POSIX executable. The resolver skipped it and selected
real system OpenSSL, causing connection errors rather than parser results.

The local resolver tries the native image first within each absolute PATH
directory, then the extensionless POSIX wrapper. Native MSWin32 retains its
image-only behavior. Existing forty fallback controls remain enabled and
pin the intentionally extensionless shim. No shell or dependency added.
Implemented / static; Pending CI tests.

## TF-097 continuation: matching MSYS Git and genuine symlinks

The original Unicode repository case now passes, but newline/metacharacter
path and symlink cases expose mismatched host tools. Exact package inventory
has MSYS Perl but no MSYS Git. Native Git sees physical filename mappings
where Cygwin/MSYS exposes logical POSIX names. The local workflow explicitly
installs MSYS Git, pins its native CMake path and records bare-git resolution.
This is host-tool closure, not a native MSWin32 Unicode fix. See the primary
[MSYS Git guidance](https://www.msys2.org/docs/git/) and
[Cygwin filename mapping](https://cygwin.org/cygwin-ug-net/using-specialnames.html).

Default MSYS symlink deepcopy can report creation success without a link.
The local job requests strict native symlinks; the fixture verifies -l and
readlink before relying on it, retaining its prior platform failure/skip
boundary. No successful copy is treated as containment coverage. See
[MSYS symlink modes](https://www.msys2.org/docs/symlinks/).
Implemented / static; Pending CI tests.

## TF-100: missing-target doc links use an unsafe lexical projection

The same MSYS2 maintenance-checkers output rejects an outside missing link
but calls it merely missing. check-doc-links fell back from abs_path failure
to canonpath, which deliberately preserves parent components. The local
resolver walks the longest existing physical prefix, then normalizes missing
suffixes, following symlinks before parents and bounding link expansion.
Original-path existence remains mandatory: missing/../existing and regular
file/.. paths cannot become valid links through normalization. Added nested
missing and real-symlink escape fixtures. Global Perl search found no other
abs_path-to-canonpath failure fallback. Implemented / static; Pending CI tests.

The GUI version timeout remains unattributed. The exact GUI image and six
CLANG64 DLLs are now retained; imports resolve and main invokes global
constructors before RunI2PD. This narrows hypotheses but is not stack/entry
proof. Do not change RNG or startup behavior without that evidence.

## TF-100 continuation: fixture grammar bypasses containment

All latest runs execute `176aec32c6ac9deeae36f5142d05a9c284f5e94a`.
[Validation 42690770393](https://github.com/daviduhden/openi2pd/actions/runs/42690770393)
jobs 129409600455, 129409600562 and 129409600759 fail assertions 413–418
in `perl-maintenance-checkers`. Four OpenSSL compiler lanes, OpenBSD and
CLANG64 repeat the same failure, nine lanes total. Original diagnostic:
`physical symlink prefix cannot escape through a missing target`.

The fixture uses reference-style Markdown, outside the documented grammar
of `check-doc-links.pl`. No target reached the resolver; this is a fixture
defect, not evidence against physical-prefix resolution. Global search
found this fixture form only in that symlink loop. The local correction
uses inline links, absolute symlink authority and an existing outside-file
control before all three missing/parent-component cases. Existing six
assertions remain; two controls are added. Implemented / static; Pending CI
tests. Production grammar is unchanged.

At this SHA, MSYS Git/Perl closure, extensionless TLS wrappers and YAML
package scanner regressions pass. CLANG64 still separately fails
`i2pd-version` after 12 seconds without output. MSVC reports 17 tests with
one supervisor skip: worker exit 3221225794 (0xC0000142) before authentication.
OpenBSD reports 255 tests, one fixture failure and three capability skips.
Skipped handshake/shutdown paths remain unvalidated.

### Latest-ten evidence inventory

All rows use the exact SHA above. Full logs, job metadata and artifact
inventories were retrieved; failed logs and relevant artifacts supplement
the review. Historical run metadata totals 382; unavailable historical
diagnostics remain evidence gaps as previously recorded.

| Run | Workflow | Observed result |
|---|---|---|
| [42690778943](https://github.com/daviduhden/openi2pd/actions/runs/42690778943) | Instrumented OpenSSL TSan | Success |
| [42690770393](https://github.com/daviduhden/openi2pd/actions/runs/42690770393) | Validation | Three sanitizer lanes fail TF-100; fuzz/docs/static pass; Perl quality skipped |
| [42690762752](https://github.com/daviduhden/openi2pd/actions/runs/42690762752) | Linux | Install/consumer and I2P Java interoperability pass |
| [42690756912](https://github.com/daviduhden/openi2pd/actions/runs/42690756912) | Android | Arm64 build and 174 JVM tests pass; API 29 UI skipped |
| [42690751293](https://github.com/daviduhden/openi2pd/actions/runs/42690751293) | OpenSSL 4 | GCC/Clang each fail TF-100 |
| [42690742396](https://github.com/daviduhden/openi2pd/actions/runs/42690742396) | OpenSSL 3 | GCC/Clang each fail TF-100 |
| [42690736472](https://github.com/daviduhden/openi2pd/actions/runs/42690736472) | Agent knowledge | Success |
| [42690730284](https://github.com/daviduhden/openi2pd/actions/runs/42690730284) | Packaging/services | Success |
| [42690723606](https://github.com/daviduhden/openi2pd/actions/runs/42690723606) | OpenBSD 8.0 | TF-100 plus three capability skips |
| [42690715945](https://github.com/daviduhden/openi2pd/actions/runs/42690715945) | Windows | CLANG64 TF-100/startup timeout; MSVC success with worker startup skip |

## TF-101: registered source root points to the CMake entry

All rows below execute `029011279d1b7a454571fb600621f11d62f8a142`.
The first failure is `check-addressbook-rearm-lifetime.cmake:10 (file)`:
`file failed to open for reading (No such file or directory)` at
`build/libi2pd_client/AddressBook.cpp`. The script receives
`-DSOURCE_DIR=${PROJECT_SOURCE_DIR}` from `tests/CMakeLists.txt`; that variable
names build/, whereas PROJECT_ROOT names the repository. Full logs and
LastTest/JUnit artifacts confirm the same root cause in nine lanes.

| Run | Jobs | Result and configuration |
|---|---|---|
| [42699000692](https://github.com/daviduhden/openi2pd/actions/runs/42699000692) | 129438426292 / 129438426514 / 129438426539 | ASan+UBSan / UBSan / TSan: 260 of 261 pass; only root inspection fails |
| [42698967451](https://github.com/daviduhden/openi2pd/actions/runs/42698967451) | 129438302355 / 129438302720 | OpenSSL 3 GCC/Clang: same sole failure |
| [42698975123](https://github.com/daviduhden/openi2pd/actions/runs/42698975123) | 129438329139 / 129438329828 | OpenSSL 4 GCC/Clang: same sole failure |
| [42698942155](https://github.com/daviduhden/openi2pd/actions/runs/42698942155) | 129438207819 | OpenBSD 8.0: same root failure; worker identity/credentials/storage skip on root runner |
| [42698933379](https://github.com/daviduhden/openi2pd/actions/runs/42698933379) | 129438186341 | CLANG64: root failure plus independent GUI version timeout |

The previous TF-100 link fixtures pass. The direct workflow invocation
correctly passes $PWD and passes, which did not cover the registered argv.
Global search finds no other wrong root-variable registration. The local fix
quotes SOURCE_DIR=${PROJECT_ROOT}, pins that add_test command in the existing
build-system contract and adds an explicit root precondition in the script.
The registered CTest remains enabled everywhere; no test or coverage removed.
Implemented / static; Pending CI tests. This is not a sanitizer report.

Successful siblings include fifteen fuzz smoke targets, source documentation,
Skill checks, packaging/services, Linux installed consumers and reference
interoperability checks. Instrumented OpenSSL TSan run 42699008722 executes
only test-clientcontext-registry; it does not establish complete crypto
coverage. Linux interop run 42698992642 verifies Java reference/provenance and
vectors, not a live Java/C++ handshake. Optional Perl-quality/device jobs skip.

## TF-102: unexpected Windows worker exit was reported as capability skip

In run 42698933379, successful MSVC job 129438185880 reports
`worker pipe accept timeout (worker exited before authentication; code
3221225794)` then returns 77. This is 0xC0000142, an unattributed loader
failure. The same nested UI reduction occurs in the CLANG64 sibling before
its successful worker restart, so that reduction does not establish cause.

The local fixture classifies live startup/Ping/restart failures as failures.
Only directly observed nested-job assignment access denial can grant a
backstop capability skip. Independent live work still runs; failure dominates
skip. Checked outcomes preserve destruction and final handle verification.
Classification controls include the loader code and genuine denial cases;
source guards forbid skip/abort exits from the live cleanup scope.
Implemented / static; Pending CI tests. No production isolation is weakened.

Published diagnostic run [42719195489](https://github.com/daviduhden/openi2pd/actions/runs/42719195489)
was dispatched with heap_diagnostics=true on 02901127. It can provide missing
loader/entry evidence; it cannot validate any local correction described here.


### Retrospective refresh and unpublished follow-up

A fresh paginated GitHub query confirms the same 505 runs / 77 SHAs:
160 success, 279 failure, 66 cancelled. No new remote run validates the local
increment. Re-reviewed exact OpenBSD and Windows evidence; RT-087 remains
unresolved. The prepared nonce-free Windows probe now retains predecessor
instruction disassembly at the failing termination, within its existing
deadline. It does not log operands, stack data or credentials and does not
establish a cause until that diagnostic executes on a published revision.

The local MSVC selection now contains 28 targets (25 at published e1d1b7a6),
including proxy, socket-lifecycle and request-ledger controls. CE-093 and
CW-033 are preventive local findings, not failures assigned to old runs.

## c9e81108 readiness regression (TF-115)

On `c9e8110858eb1d0a3ac1e1807e665e49b64a0b61`, five workflows
passed and five failed. The same readiness fixture failed across the
[OpenSSL 3](https://github.com/daviduhden/openi2pd/actions/runs/44757377506),
[OpenSSL 4](https://github.com/daviduhden/openi2pd/actions/runs/44757382951),
[OpenBSD](https://github.com/daviduhden/openi2pd/actions/runs/44757393726),
[Windows](https://github.com/daviduhden/openi2pd/actions/runs/44757399249)
and [sanitizer](https://github.com/daviduhden/openi2pd/actions/runs/44757416055)
jobs. Affected jobs: 136360561437, 136360562057, 136360582466,
136360582924, 136360625745, 136360638345, 136360638614, 136360697317,
136360697424 and 136360697512.

`TestReadinessSelectionAndRearmFailure` expected `timed_out` at line 374.
The fixture set the deadline to zero, which is `NEVER_TIMES_OUT`.
Both callbacks consequently remained pending when rearm failed.
The regression now exercises an actual finite expiry and checks that
a never-expiring sibling receives the rearm failure. Pending CI.

Four Linux suites and three sanitizer suites each passed 278/279.
OpenBSD passed 270 with one failure and three skips. CLANG64 passed 261
with one failure and one skip. MSVC passed 34/36, with RT-087 also failing.
Android passed 187 JVM tests. No sanitizer report accompanied the assertion.

The inventory covers 595 runs at 86 SHAs, with 592 complete logs, one partial
and two unavailable runs. Five retried historical job logs still return 404.

## a5ebd51d batch outcomes

Nine of ten workflows succeeded on `a5ebd51de0aedb04c0b3335a196a1af3502d987b`.
MSVC passed 35 of 36 and retained RT-087; MSYS2 had one skip among its
263 selections. TF-115 readiness and torrent admission passed on both
Windows jobs, OpenBSD, the four Linux compiler/provider lanes and the
three sanitizer lanes. Those sanitizer lanes reported 279 selections each;
bounded fuzz selected 15 and the instrumented-OpenSSL TSan run selected
one. No sanitizer finding was observed in those logs.
[Validation run](https://github.com/daviduhden/openi2pd/actions/runs/44918645700)

[OpenBSD run 44918642754](https://github.com/daviduhden/openi2pd/actions/runs/44918642754)
reported three skips among 274 selections. Its JUnit artifact gives the
reasons: worker identity and credentials require a non-root runner, and
storage refuses root. A non-root storage run still skips until exec-free
provisioning exists. Android arm64 passed JVM/Lint and D8/R8 checks;
API 29 UI was skipped. The signing job and package ID change were added
on later revisions with no runs in the retrieved history.

## 7f563d3a manual batch

Exact SHA `7f563d3a3c11a250c394a4a22a26cb972f403db7`. Seven workflows
passed. Windows and Validation failed. Android was cancelled during
collection of diagnostics. Android arm64 passed 187 JVM
tests, API 29 instrumentation was skipped after RT-7F-01, and signing was
not requested. MSVC passed 35/36 and retained RT-087. The four Linux
OpenSSL jobs and three sanitizer jobs passed 279 selections each.
OpenBSD completed 274 selections with three skips. All 15 bounded fuzz
cases and the focused instrumented OpenSSL TSan case passed. The provider
versions were OpenSSL 3.5.9, 4.1.0 and LibreSSL 4.4.0.

[Validation Perl job](https://github.com/daviduhden/openi2pd/actions/runs/44923772633/job/136905276549)
passed Test::More and failed Perl::Critic with 207 findings across seven
files (TF-066). The profile now delimits records with a newline and
retains enforcement. Existing formatting and complexity findings remain
open. See CE-7F-01 for the independent documentation dependency failure.

The paginated Actions inventory contains 615 accessible runs across 88
SHAs (235 successful, 313 failed, 67 cancelled). These totals describe
metadata coverage. Complete log review is recorded by exact run.

TF-066 follow-up: the Android Javadoc reader now checks open/read/close
separately and treats an empty regular source as EOF. Its empty-input
regression is prepared in the maintenance suite. Pending CI tests; this
addresses one diagnostic from the published batch.

## 36ae883e manual batch

The metadata inventory contains 625 runs and 89 SHAs, with 242 successful,
316 failed and 67 cancelled. The ten exact runs on
`36ae883ea40059ed55fc653e23845df0a080a2e9` produced seven workflow
successes and three failures (Android API 29 RT-36-01, MSVC RT-087 and
optional Perl quality TF-066). MSVC passed 35/36. Four Linux
compiler/provider jobs and three sanitizer jobs passed 279 selections
each. The instrumented TSan probe and 15 bounded fuzz cases passed.
OpenBSD had three skips among 274 selections. Android arm64 passed and
production signing was skipped.

[Perl quality job 136916729552](https://github.com/daviduhden/openi2pd/actions/runs/44927502209/job/136916729552)
passed Test::More and failed Perl::Critic with 953 newline-delimited records.
Earlier concatenated output counts are not comparable. The catalog checker
now checks source I/O and uses extended regex formatting for singular and
plural pattern families, preserving captured literals. Existing maintenance
fixtures cover escaped newlines and duplicate plurals. Pending CI tests.

## 38a3debd manual batch

Seven of ten workflows succeeded. Android API 29 stopped at RT-38-01,
MSVC passed 35/36 with RT-087, and Validation's only failed job was optional
Perl quality (TF-066). Four Linux compiler/provider jobs and the three
sanitizer variants passed 279 selections each. OpenBSD had three known
skips among 274 selections. The 15 fuzz selections and instrumented
OpenSSL TSan probe passed. Android arm64 passed and signing was skipped.

[Perl quality job 136925560524](https://github.com/daviduhden/openi2pd/actions/runs/44930403992/job/136925560524)
reported 949 policy records, down from 953 in the preceding batch.
The native catalog checker accounted for 47, down from 53. The local
printf-pattern refactor preserves operand order and capture boundaries;
the existing maintenance fixtures cover these semantics. Pending CI tests.

Paginated Actions metadata covers 635 executions across 90 SHAs. Complete
job-log evidence in this increment covers the ten executions above.

## 076ddcb7 manual batch

Paginated metadata covers 655 runs at 92 SHAs. For the ten exact
076ddcb76e7c3318922e49abbaaf143216e2aa8e runs, two workflows succeeded,
seven failed and Android was cancelled. OpenSSL instrumented TSan,
component interop/install, Doxygen, bounded fuzz (15/15), Android arm64
and generated JNI header comparison succeeded in their individual lanes.
The sanitizer variants had one Perl portability failure each and no
sanitizer report in the recovered logs. Windows retains RT-087. The new
OpenBSD non-root phase did not run because the root suite failed first.

TF-076-01. The module inventory regex read a POD sentence beginning
`require explicit` in the JNI checker as an import. It failed across
native OpenSSL 3/4, GCC 13, MSYS2, OpenBSD, sanitizer and optional Perl
jobs. Inspect documentation boundaries before classifying a module
as required. The scanner now ignores POD and has a fixture covering
both ignored prose and a real require after =cut. Pending CI tests.
[Validation Perl job](https://github.com/daviduhden/openi2pd/actions/runs/44949202546/job/136983194003)

TF-076-02. The GCC 13 distribution lane ran tools/test-tools-inventory.cmake
under older script-mode CMake, where unset CMP0057 rejected IN_LIST.
Use list(FIND), matching the service checker. The existing inventory
target validates this on the same lane. Pending CI tests.
[Distribution job](https://github.com/daviduhden/openi2pd/actions/runs/44949202640/job/136983194711)

TF-076-03. The static musl job built its Debian package but failed to
write SHA256SUMS because the root packaging container created the
host-mounted static-out directory. The runner now creates that directory
before the container writes packages. This keeps checksums under runner
authority. Pending CI tests.
[Static package job](https://github.com/daviduhden/openi2pd/actions/runs/44949202814/job/136983194867)

## 48680786 manual batch

Paginated Actions metadata returned 665 runs across 93 SHAs (265 successful,
331 failed and 69 cancelled). The ten complete run logs below cover
48680786664c71045cbbee002f549332eec4d1f9. Conclusions describe that revision.

| Run | Observed outcome |
|---|---|
| [Linux](https://github.com/daviduhden/openi2pd/actions/runs/44999485705) | GCC 13.3 and Clang 17.0.6 with libstdc++ 13 and system OpenSSL 3.0.13 each passed 279 tests. Component install passed. The interop job checked pinned refs and historical vectors, with two known gaps and no differential binary supplied. |
| [OpenSSL 3](https://github.com/daviduhden/openi2pd/actions/runs/44999485734) | GCC 16.2 and Clang 23.1.6 with 3.5.9 each passed 279 tests and the focused selections. |
| [OpenSSL 4](https://github.com/daviduhden/openi2pd/actions/runs/44999485630) | GCC 16.2 and Clang 23.1.6 with 4.1.0 each passed 279 tests and the focused selections. |
| [OpenBSD](https://github.com/daviduhden/openi2pd/actions/runs/44999485867) | Clang 22.1.6 and LibreSSL 4.4.0 selected 274 tests, with three root-phase skips. Two non-root credential tests passed separately. |
| [Windows](https://github.com/daviduhden/openi2pd/actions/runs/44999485726) | MSYS2 Clang64 selected 263 tests with one expected POSIX-only skip. MSVC passed 35 of 36 in the focused selection. The supervisor failed before authentication (RT-087). |
| [Android](https://github.com/daviduhden/openi2pd/actions/runs/44999485666) | Arm64 build and JVM checks passed. API 29 ran 64 instrumentation cases with seven failures. Production signing was skipped. |
| [Validation](https://github.com/daviduhden/openi2pd/actions/runs/44999485763) | 279 selected in each of ASan+UBSan, UBSan and TSan with no failures. Fuzz smoke passed 15/15. Source documentation and agent checks passed. Optional Perl quality failed at Perl::Critic. |
| [Instrumented OpenSSL TSan](https://github.com/daviduhden/openi2pd/actions/runs/44999485742) | The focused provider test passed. |
| [Packaging](https://github.com/daviduhden/openi2pd/actions/runs/44999485787) | Debian, static musl, container image, service matrix and service contracts passed. |
| [Agent knowledge](https://github.com/daviduhden/openi2pd/actions/runs/44999485631) | Synchronization gate passed. |

**TF-486-01.** Perl Test::More completed before Perl::Critic exited 2.
The report contains 962 findings, including seven
`ProhibitCaptureWithoutTest` records in the TOML parser, doc link and nav
checkers, workflow checker and portability fixture. Those sites now hold
captures in lexical variables. The existing fixtures cover TOML parse/refusal,
documentation inputs, workflow parsing and the two-way Perl inventory.
Formatting and other policy findings remain open. Pending CI tests.
[Perl quality job](https://github.com/daviduhden/openi2pd/actions/runs/44999485763/job/137137652576)

**TF-486-02.** The API 29 x86_64 job ran the built debug APK and
recorded seven failures in its downloaded instrumentation XML. One
`LocaleResourceTest` assertion found the zh-CN catalog missing. Three
`UiContractTest` fixtures inflated animated Material views off the main
thread. `UiDialogLifecycleTest` clicked the address book action while its
initial asynchronous refresh could still reject it. The local revision
selects the packaged locale by its resource qualifier, checks the real
debug and release APKs and synchronizes the affected fixtures with their
UI prerequisites. Pending CI tests. Two separate broker-contract failures
remain under RT-486-01.
[API 29 job](https://github.com/daviduhden/openi2pd/actions/runs/44999485666/job/137137652481)

**TF-486-03.** OpenBSD's non-root credential XML counted two passes,
yet the credential test printed `supplementary groups present, skipping
the drop check` and returned success. A separate root-started CTest
fixture now seeds a supplementary group in a forked child and verifies
the production drop to the provisioned non-root account. The ordinary
non-root fixture still checks that an unprivileged caller cannot clear
supplementary groups. The root-started binary runs from a root-owned
directory and has a separate required JUnit result. Pending CI tests.
[OpenBSD job](https://github.com/daviduhden/openi2pd/actions/runs/44999485867/job/137137661029)

## 029ffd46 manual batch

The paginated inventory contains 675 runs across 94 SHAs, including 268
successes, 338 failures and 69 cancellations. Full logs for the ten runs
below belong to 029ffd4641be7f40e5c616615cd2231f97007f48.

| Run | Exact batch outcome |
|---|---|
| [Agent knowledge](https://github.com/daviduhden/openi2pd/actions/runs/45487509537) | Passed. |
| [Android](https://github.com/daviduhden/openi2pd/actions/runs/45487509934) | Arm64 passed, including packaged-locale check. API 29 Android instrumentation blocked by HTTP 429 during Gradle dependency resolution. Signing skipped by input. |
| [Linux](https://github.com/daviduhden/openi2pd/actions/runs/45487510099) | Component install and Java reference lane passed. The GCC 13 and Clang 17 floor jobs each failed only the Perl maintenance fixture among 279 tests. |
| [OpenSSL 3](https://github.com/daviduhden/openi2pd/actions/runs/45487510059) | GCC and Clang each failed only the Perl maintenance fixture among 279 tests. |
| [OpenSSL 4](https://github.com/daviduhden/openi2pd/actions/runs/45487509920) | GCC and Clang each failed only the Perl maintenance fixture among 279 tests. |
| [OpenBSD](https://github.com/daviduhden/openi2pd/actions/runs/45487509975) | Broad suite failed the same fixture among 274 selections, blocking the later credential probes and POSIX front-end build. |
| [Windows](https://github.com/daviduhden/openi2pd/actions/runs/45487510107) | MSYS2 failed the same fixture among 263 selections. MSVC failed the supervisor case among 36 selections. |
| [Provider TSan](https://github.com/daviduhden/openi2pd/actions/runs/45487509958) | Focused instrumented OpenSSL test passed. |
| [Packaging](https://github.com/daviduhden/openi2pd/actions/runs/45487509869) | Five jobs passed. |
| [Validation](https://github.com/daviduhden/openi2pd/actions/runs/45487510039) | Three sanitizer jobs failed the Perl fixture among 279 selections each. Perl quality stopped in Test::More. Fuzz smoke passed 15/15. |

**TF-029-01.** Test::More cases 448 and 503 failed their positive baselines
for Chinese plurals and LF endings. Both synthetic Gradle configurations
retained the obsolete bare `zh` filter while the checker now requires
`zh-rCN`. The production Gradle config and its static contract use the
new spelling. A repository search found exactly two stale executable
fixture copies. Both now use the supported qualifier, with a witnessed
bare-filter mutation and diagnostic assertion before the plural variants.
The existing CTest and optional Perl lanes run these cases. Pending CI tests.
[Perl quality job](https://github.com/daviduhden/openi2pd/actions/runs/45487510039/job/138776712391)

**TF-029-02.** The x86_64 API 29 run stopped on Gradle plugin-classpath
dependency resolution after Maven Central returned HTTP 429. That job
did not execute any instrumentation assertions and supplies no SAF
retirement evidence. Arm64 still supplies evidence for the APK locale
checks. Keep the API 29 lane enabled.
[API 29 job](https://github.com/daviduhden/openi2pd/actions/runs/45487509934/job/138776710614)

**TF-029-03.** The OpenBSD artifact contained the broad CTest report
and lacked both dedicated credential reports. A failed broad CTest
previously stopped the remaining guest phases. Separate steps now
allow the probes and the POSIX build after their specific prerequisites.
The broad suite retains its own status and the guest copies back each
report independently. The workflow checker pins these prerequisites.
Pending CI tests.
[OpenBSD job](https://github.com/daviduhden/openi2pd/actions/runs/45487509975/job/138776714780)

The Perl quality job now retains the Test::More transcript and runs
Perl::Critic after successful tool installation even if a preceding
test fails. Both failures still propagate to the job.

Both Android jobs missed their Gradle caches for this batch. The UI job
now tries the arm64 Gradle cache for the same build-input hash before
downloading dependencies with an empty cache. The x86_64 native cache
remains triplet-specific. A cold batch or a dependency missing from
both caches may still hit the external rate limit. Pending CI tests.
[Maven Central's HTTP 429 documentation](https://central.sonatype.org/faq/429-error/)

The Linux OpenSSL composite now collects focused torrent/client/profile
results and standalone Perl evidence after successful compilation even
if the broad CTest step fails. The existing output retains all JUnit
files and each phase preserves its failure. Pending CI tests.

## 652024d2 manual batch

Paginated metadata covers 685 runs at 95 SHAs, with 276 successes,
340 failures and 69 cancellations. The ten full logs below belong to
652024d2d6081a15187a63d9094a160619dece07.

| Run | Observed outcome |
|---|---|
| [Agent knowledge](https://github.com/daviduhden/openi2pd/actions/runs/45654900944) | Passed. |
| [Validation](https://github.com/daviduhden/openi2pd/actions/runs/45654900993) | ASan+UBSan, UBSan and TSan each passed 279; fuzz smoke passed 15/15. Doxygen and agent checks passed. Test::More passed 1578 across eight files; Perl::Critic failed. |
| [Provider TSan](https://github.com/daviduhden/openi2pd/actions/runs/45654901837) | Instrumented provider test passed. |
| [Linux](https://github.com/daviduhden/openi2pd/actions/runs/45654901941) | GCC 13 and Clang 17 floor jobs each passed 279, and component install passed. Java interoperability checked the reference and historical vectors without a differential binary. |
| [OpenBSD](https://github.com/daviduhden/openi2pd/actions/runs/45654902364) | 274 selected, three expected root-suite skips. Two dedicated non-root cases and the independent root-started credential drop passed. POSIX front-end build passed. |
| [Windows](https://github.com/daviduhden/openi2pd/actions/runs/45654902545) | MSYS2 passed 262 with one expected AF_UNIX skip. MSVC passed 35/36, with RT-087 still failing. |
| [OpenSSL 3](https://github.com/daviduhden/openi2pd/actions/runs/45654902946) | Both GCC and Clang passed 279 and the separate 10/3/9 focused selections. |
| [OpenSSL 4](https://github.com/daviduhden/openi2pd/actions/runs/45654903617) | Both GCC and Clang passed 279 and the separate 10/3/9 focused selections. |
| [Packaging](https://github.com/daviduhden/openi2pd/actions/runs/45654904135) | All five jobs passed. Debian debugedit diagnostics persisted. |
| [Android](https://github.com/daviduhden/openi2pd/actions/runs/45654906083) | Arm64 build and JVM tests passed. API 29 JUnit recorded 65 passes, including stale-endpoint refusal and retirement/death/rebind. Signing skipped by input. |

The posted revision validates TF-029-01 and the dedicated probes that
were blocked in TF-029-03. The Android 429 did not recur. Positive device
cancellation of a provider call across Binder remains separate evidence.

**TF-652-01.** The repository severity-3, core-theme Perl::Critic report
contains 830 findings and exited 2. Test::More had already passed.
Local changes address a slice of the findings in the vector producer,
documentation navigation, hex conversion and two command fixtures.
The existing CI lanes cover the parser, packaging fixture and interop
converter contracts. The remaining profile findings stay open.
[Perl quality job](https://github.com/daviduhden/openi2pd/actions/runs/45654900993/job/139348838633)

Android API 29 recorded a matching Gradle cache restore from arm64. The
separate x86_64 native dependency cache missed. The build used Temurin
25.0.5+8, Gradle 9.8.1 and AGP 9.4.1.
