# Architecture review and refactor plan

Reviewed: 2026-10-31.

## Scope and status

Initially approved: review the architecture, save a plan, and align folders
without changing runtime behavior. The user selected:
"Align folders now; plan architectural changes and animation (recommended)".

Follow-up approval: "Phases 1–3: contracts, ownership, lifecycle; clarify policies
first". Those phases are now implemented. Animation (phases 4-5), external channel
expansion, dependency upgrades, and broker/hardware execution remain excluded.
Existing staged changes are preserved; no staging or commit is requested.

Phase 0 is complete and verified. Phases 1-3 pass the expanded Docker regression
suite; final production-image verification is recorded below. Phases 4-6 remain
proposed only.

Historical phase 0 acceptance criteria:

- Production sources and internal headers live together under `src/`.
- Docker, Make, Ceedling, the compiler wrapper, and documentation use that layout.
- All seven moved files remain byte-for-byte unchanged.
- All 126 existing cases pass without weakening assertions or skipping tests.
- The production image compiles and links with real Paho/FTDI dependencies.
- The plan identifies risks, ownership boundaries, migration dependencies,
  animation contracts requiring decisions, and verifiable acceptance tests.

Phases 1-3 acceptance criteria:

- Pure immutable command decoding/formatting and light-state operations;
  self-contained headers, private adapter state, and compilation with `-fno-common`.
- Preserve `/set` validation, exact confirmation topics, canonical values,
  QoS 0/non-retained publication, all 513 frame bytes, timing delays, and logging.
- A synchronized 64-entry incoming queue; topic/payload limits 1024/64 bytes;
  at most 16 commands applied per frame. Overflow/oversize logs an error, releases
  rejected input, and fails with cleanup, without success confirmation.
- Single main-thread light-state ownership and stable snapshots during writes;
  controlled concurrent producer/consumer and stop-arrival tests.
- Application-owned error/exit policy and idempotent adapter close operations.
  Stop accepting input, discard queued commands, drain outstanding publications
  and disconnect for 5000 monotonic milliseconds, then close both adapters.
  Timeout or adapter error is unsuccessful. No blackout or final frame is sent.
- Paho owns automatic reconnect and the connected callback schedules resubscription.
  Initial connection, subscription, and publication failures remain fatal;
  established-connection reconnect failures remain retryable. The user selected
  Paho reconnect; the final initial-failure clarification was unavailable, so the
  recommended existing fail-fast policy was retained.
- Track publications until completion or destruction, including callbacks invoked
  during destroy. No project lock is held across Paho, FTDI, delays, or destroy.
- Full deterministic regressions and real production linkage remain mandatory.
  The 5-second deadline bounds draining, not Paho's additional teardown time.

## Historical architecture before phases 1-3

```text
main thread
  main: environment -> DMX_Initialize -> MQTT_Start -> signals
  loop: MQTT_Poll -> DMX_Loop [break, MAB, write, delay, change logging]
  stop: MQTT_Stop -> MQTT_Poll once -> return

Paho callback thread(s)
  onMessageArrived -> copy topic/payload -> application onReceived
    -> validate/mutate copied topic -> DMX_SetValueForChannel
    -> allocate/publish confirmation -> free copied incoming message
  send completion -> free outgoing message
  failure/disconnect -> atomic event flags -> main-thread MQTT_Poll
```

The deployed process is a small singleton executable, not a reusable library.
The application combines protocol policy and lifecycle; MQTT combines transport,
message allocation, callbacks, and connection state; DMX combines light state,
frame generation, transport timing, hardware lifecycle, and logging.
[Fatal cleanup](../src/shutdown.c) couples both adapters through global callbacks.

### What is already working well

- Small, understandable C modules; no framework or additional service is needed.
- Length-aware parsing and owned MQTT buffers reject malformed/binary inputs.
- Acquired FTDI resources are tracked for fatal cleanup; return codes are checked.
- Signal handlers only set stop intent. Callback errors use atomic flags rather
  than intentionally destroying Paho from within the failing callback.
- Unity/CMock test real project code with external dependencies mocked. Gateway
  tests exercise cross-module flow, and failure tests audit ownership and exits.
- Docker gates production compilation on a nonempty, entirely passing test suite.
  Keep these safeguards throughout the refactor.

## Original findings and priorities

These are architecture/correctness findings from source and test inspection,
not a penetration test or a claim of hardware or concurrent-runtime verification.

| Priority | Evidence | Finding and consequence | Planned response |
| --- | --- | --- | --- |
| High | [DMX state/write](../src/dmx.c#L72), [setter](../src/dmx.c#L108), [delivery](../src/mqtt.c#L146) | Paho calls the application inline; the callback writes `packet` while the main thread reads it for FTDI, comparison, and copying. There is no synchronization. This is a C data race, not merely possible visual tearing. Adding an animation writer would compound it. | Main-thread ownership of light state and immutable-per-write frames; bounded synchronized handoff from callbacks. |
| High | [normal exit](../src/mqtt2dmx.c#L117), [DMX cleanup](../src/dmx.c#L18) | One poll after asynchronous disconnect does not wait for completion. Normal exit never explicitly closes/deinitializes FTDI. Tests currently require the request, not completed cleanup. | Explicit stopping/draining/closed lifecycle, deadline, idempotent close operations, one application-owned exit path. |
| High before animation | [DMX loop](../src/dmx.c#L72) | Frame transport, sleeps, state comparison, and logging share one blocking operation. There is no elapsed-time clock or scheduler. A fade implemented as sleeps or increments per loop would block commands or change duration with load. | Separate state evaluation and transport. Use monotonic elapsed time; preserve FTDI timing until separately measured and approved. |
| Medium | [application globals](../src/mqtt2dmx.c#L11), [MQTT globals](../src/mqtt.c#L14), [test flags](../project.yml) | `mqtt_topic` has two tentative definitions and tests require `-fcommon`. Adapter globals and externally visible internal functions obscure ownership; tests reach into them. | One owner per state item, module-private implementation details, self-contained internal headers; then remove `-fcommon`. |
| Medium | [DMX API](../src/dmx.h#L13), [parser](../src/mqtt2dmx.c#L36) | The 513-byte frame represents 512 slots, but the byte-sized channel API and parser allow only 1-255. Slot 0 can be changed by direct internal API use. | Distinct channel/value types and explicit slot validation; keep the existing external range until separately approved. |
| Medium | [command handler](../src/mqtt2dmx.c#L53), [publication](../src/mqtt.c#L282) | Parsing, state mutation, confirmation formatting, and memory release are coupled. Confirmation is published after a buffer update, not after physical delivery; QoS 0/non-retained messages are not delivery guarantees. Animation would make "confirmed" ambiguous. | Pure command decoder, explicit command application, owned publication lifetime, separate acceptance/completion semantics for a future transition API. |
| Medium | [reconnect](../src/mqtt.c#L74), [connection loss](../src/mqtt.c#L133), [poll](../src/mqtt.c#L222) | Manual reconnect and Paho automatic reconnect are both configured, with a clean session. Tests simulate callbacks sequentially. Atomics cover event flags, not all client/message lifecycle state. | Choose one reconnect owner and verify resubscription against real pinned Paho. Audit concurrent callback teardown and outstanding sends; do not assume flag deferral proves safe shutdown. |
| Medium | [fatal shutdown](../src/shutdown.c#L7), [DMX check](../src/dmx.c#L35) | Adapters can terminate the process through global cleanup hooks. This makes lifecycle extension, recovery decisions, and ordinary unit testing difficult. | Adapters report errors; application coordinates cleanup and exit, preserving current fail-fast policy until explicitly changed. |
| Medium | [test isolation](../tests/README.md#isolation-and-test-seams), [Dockerfile](../Dockerfile), [Makefile](../Makefile) | Strong deterministic regressions do not cover concurrency, actual reconnect, serial timing, or disconnect completion. Test and production toolchains differ; production uses an old Alpine base. `-Wall` is currently on the link command, and `make run` references an undefined `build` prerequisite. | Keep production linkage gating; separately plan warning/dependency/build hygiene and targeted integration checks rather than silently upgrading during relocation. |

### Timing and observability caveats

At 250 kbaud, 513 slots at 11 bits each take approximately 22.57 ms on the wire.
The configured delays add 120 us break, 16 us mark-after-break, and 12 ms pause.
If serialized, that totals approximately 34.71 ms before other overhead; it is
not evidence of a measured refresh rate. USB buffering, FTDI write completion,
OS scheduling, and synchronous per-channel logging affect the actual cadence.
Do not interpret the 12 ms pause as a 12 ms frame period or promise a fade update
rate from mock delay assertions.

## Folder alignment

Implemented layout:

```text
src/
  mqtt2dmx.c
  mqtt.c       mqtt.h
  dmx.c        dmx.h
  shutdown.c   shutdown.h
tests/
  test_*.c
  compile.rb   run.rb   README.md
  support/
build/                       ignored, generated tests/mocks/reports
.agent-memory/               plans and handoffs
.github/                     instructions and skills
Dockerfile  Makefile  project.yml  Gemfile  Gemfile.lock
```

Headers stay with implementations because there is no public library interface.
Keep the flat source layout until actual module growth justifies subdirectories;
do not create empty `domain/`, `services/`, `interfaces/`, or `animations/` trees.
Test runners stay in `tests/`, and build configuration stays at the root.
The production binary location inside the image is unchanged.

As responsibilities are extracted, add concrete pairs such as `command.c/.h`,
`light_state.c/.h`, and `transition.c/.h` under `src/`, with matching
`test_command.c`, `test_light_state.c`, and `test_transition.c` under `tests/`.
Extract a separate clock or command-queue module only when its implementation
merits one. Retain `mqtt.c` and `dmx.c` as adapter names; renaming them is not
necessary to create good boundaries.

## Proposed target architecture

```text
Paho adapter -> bounded synchronized incoming queue
                                |
                     main-thread application owner
                     decode -> apply command
                                |
                       light state + transitions
                                |
                       evaluate at monotonic now
                                |
                     513-byte stable frame -> FTDI adapter

application -> acceptance/completion publication -> Paho adapter
application -> lifecycle/error policy -> close adapters
```

- Pure decoding operates on immutable byte spans, returns a typed command or a
  validation error, and never talks to MQTT, FTDI, the clock, or process exit.
  Preserve arbitrary configured topic prefixes and exact confirmation topics.
- Paho callbacks enqueue owned input and record lifecycle events; they never
  mutate light state or invoke frame writes. The queue has finite memory, explicit
  ownership on rejection/shutdown, and synchronization appropriate for Paho's
  actual callback concurrency. Do not assume a single producer without evidence.
- The application drains a bounded amount of work per frame so sustained MQTT
  traffic cannot starve output. No queue/state lock is held across Paho calls,
  FTDI I/O, sleeping, or client destruction.
- One main-thread owner holds state and constructs each stable frame. The FTDI
  adapter only transmits it and owns its device resource; it does not interpolate.
- Begin with module-private state and explicit lifecycle methods. Introduce a
  context struct where reset/ownership needs it, not a generic dependency
  injection framework or a multi-universe abstraction.
- Keep repeated frame output even when values are unchanged. Snapshot/state
  equality is not permission to stop the DMX stream.

## Transition design proposal (not implemented)

Start with independent linear transitions per channel, backed by a fixed-size
array rather than a thread, allocation, or timer per channel. Each active entry
needs a start value, target value, monotonic start time, and duration.

Evaluation at time `now`:

1. Clamp elapsed time to `[0, duration]`.
2. Interpolate using signed arithmetic for descending fades and sufficiently
   wide intermediates. Define rounding and maximum duration before implementation.
3. Emit an integer value in `[0, 255]`; at/after the deadline emit the exact target.
4. Mark a completed entry inactive once; do not publish completion every frame.

Proposal: zero duration is an immediate set. A new command for an active channel
replaces its transition from the value evaluated at that same timestamp, not the
old target. Other channels continue independently. Late frames evaluate the
current elapsed time instead of replaying missed steps or accumulating drift.
Equal start/target values and completion followed by retargeting must be defined.
Sample one timestamp for all channels in a frame.

The current `/set` decimal payload and confirmation remain unchanged. Add an
explicit opt-in command shape/topic for fades only after the wire contract is
agreed; do not silently reinterpret existing payloads. Keep acceptance distinct
from completion, and do not claim either proves a physical fixture reached its
target. Avoid publishing each intermediate value unless separately requested.

### Decisions required before implementing behavior changes

- Transition payload/topic and units; bounded maximum duration; initial linear
  easing only versus additional curves. No JSON dependency is assumed.
- Replace/retarget versus queue commands; same-channel ordering; whether time
  starts on receipt or application. Recommended: start when the main loop applies
  the command, retarget from its current evaluated value.
- Future changes to the approved queue limits/overflow policy require a new
  decision; the current limits and fail-fast policy are recorded above.
- Acceptance/completion/cancellation topics and QoS/retain behavior; handling
  publication failures or disconnects while transitions continue.
- Deliberately exposing channels 256-512 remains a separate decision.
  Internal storage should support all slots; external expansion needs new tests
  and documentation rather than an incidental integer-type change.
- Define whether future transitions continue during shutdown drain. Current
  shutdown has a 5-second drain and no blackout/final frame; closing output does
  not itself establish fixture state.
- Desired frame cadence and acceptable end-to-end timing tolerance, validated on
  approved hardware. Existing mock timings cannot establish these thresholds.

## Phased implementation and dependency gates

| Phase | Work | Depends on | Acceptance gate |
| --- | --- | --- | --- |
| 0: Layout only | Move seven unchanged files into `src/`; update all build/test paths and directly related docs. | Existing regression baseline | Identical source contents; 126/126 tests; real production link and runtime image. |
| 1: Isolate contracts | Extract pure command decoding/formatting and light-state operations; make headers self-contained; eliminate duplicate topic ownership and hide internals. Keep current APIs as thin compatibility seams where needed. | 0 | Existing topics, validation, confirmations, byte ownership, frame bytes, and error outcomes unchanged; new pure unit tests; compile without `-fcommon` once duplicate definitions are removed. |
| 2: Establish single ownership | Add bounded synchronized handoff; move command application/state mutation to the main loop; keep frames stable through transport; cap command processing per frame. | 1; queue policy approved | Controlled concurrent producer/consumer tests, saturation and cleanup tests, frame snapshot tests, no output starvation under bounded test load. Update callback-order tests intentionally rather than weakening ownership assertions. |
| 3: Complete lifecycle | Explicit adapter close/error contracts; application-owned stopping/draining/closed state; reconcile reconnect ownership and subscription recovery; track queued/in-flight message lifetimes. | 2; shutdown/reconnect policies approved | Delayed/failed/never-completing disconnect, pending publications, callback during stop, partial initialization, repeated close, and normal/fatal cleanup tested. Destroy outside callbacks with a documented quiescence strategy; no locks held while awaiting Paho. |
| 4: Pure transition engine | Fixed-capacity channel state and linear elapsed-time evaluation, independent of networking/hardware. | 1; transition semantics approved | Fake-time tests for start, midpoint, endpoint, descending fade, rounding, zero duration, late tick, retarget, cancellation, independent channels, max duration, and protected slot 0. |
| 5: Wire transitions into output | Opt-in protocol, monotonic scheduler, evaluate-before-write, bounded command drain, agreed acceptance/completion events. | 2, 3, 4; wire protocol/cadence approved | Mock-clock gateway tests through MQTT input to exact frame bytes and events; unchanged `/set`; ongoing DMX frames during idle/active fades; no per-fade sleeps or unbounded queues. |
| 6: Operational verification/hygiene | Real Paho integration, timing measurement, warning flags, reproducible supported production dependencies, and observability suitable for continuous fades. | 5 for animation verification; build hygiene can follow 0 separately | Isolated broker reconnect/resubscribe and shutdown tests; approved FTDI timing checks against agreed thresholds; full suite and production link on each dependency change. |

Phase 4 can proceed independently of phases 2-3 after phase 1, but must not be
connected to runtime output before ownership and lifecycle gates pass.
Keep each phase separately reviewable and reversible. Establish concurrency and
shutdown correctness before committing to additional animation features such as
groups, scenes, color-space interpolation, or scheduled sequences.

## Verification strategy

For phase 0, use a green-before/green-after refactor: no behavior change means no
artificial failing behavioral test. Check byte identity against the starting
staged sources and rerun the entire suite because paths affect every module.

For behavioral phases, use Docker-only red-green-refactor. Add a focused test,
observe the intended assertion failure (not a build failure), implement, rerun,
then exercise affected gateway regressions. Every cross-module change requires
the full suite and a production image build. Maintain the strict runner.

Existing commands:

```sh
docker build --target test-results .
docker build --target test -t mqtt2dmx-test:local .
docker run --rm --network none mqtt2dmx-test:local test:gateway
docker build -t mqtt2dmx:local .
```

Rebuild the test image after changes or use the documented mounted-checkout
workflow. New pure modules can use Ceedling's `test:<module>` selectors once added.
Use fake monotonic timestamps, not sleeps, for interpolation tests. Add separate
Docker concurrency tests when synchronization is introduced; sequential mocked
callbacks cannot prove absence of races. Any sanitizer/toolchain addition belongs
to an explicitly scoped verification change, not this folder migration.
Real broker/hardware tests require approval and must not target existing devices
or broker topics by default.

## Execution record

- Initial worktree contained staged user changes, including production fixes and
  the test harness. Those staged contents were retained; no commit was made.
- Baseline: `docker version` succeeded; `docker build --target test-results .`
  freshly executed 126 tests, 126 passed, 0 failed, 0 ignored (exit 0).
  Dependency layers were cached; test execution was not.
- Phase 0: seven production files relocated with no byte changes, verified using
  `git show :<original-path> | cmp - src/<file>` for each file.
  Root source copies are absent. `git diff --check` passed.
- Post-move: `docker build -t mqtt2dmx:local .` succeeded (exit 0).
  Its test gate freshly compiled and executed all 126 cases: 126 passed,
  0 failed, 0 ignored. Production freshly compiled all four `src/*.c` files
  and linked with real Paho/FTDI libraries; the runtime image was produced.
  Dependency layers and unchanged runtime copy layers reused cache, not the
  regression execution or production compile/link.
- Application 49, MQTT 30, DMX 23, gateway 18, and harness 6 cases remain in
  the regression scope. No tests were added, removed, or weakened for relocation.
- Local Markdown link targets in changed documentation resolve; stale root-source
  paths were checked in the affected build/test/instruction surfaces.
  `git diff --check` passed. No new test infrastructure was needed for phase 0;
  concurrency, real-broker, and timing verification remain future gaps.
- Runtime verification: not performed; no broker or FTDI approval was requested.
- The preceding entries are the completed phase 0 record, not the current source
  identity or architecture.

### Phases 1-3 implementation (2026-10-31)

- Added pure `command` and `lights` contracts, with 512-slot internal storage and
  preserved 1-255 MQTT addressing. Decoding never mutates incoming buffers.
- Application owns state and writes stable frame snapshots. Adapter handles,
  callback pointers, packet history, and lifecycle state are private; removed
  duplicate topic ownership and `-fcommon` from tests and production.
- MQTT callbacks enqueue owned input or record atomic events. The bounded
  synchronized queue and 16-message drain budget prevent command starvation of
  frames. CMock tests check limits; a separate thread-safe Paho double exercises
  two producers, owner consumption, ordering, and stop races.
- Added closed/running/draining/disconnecting MQTT lifecycle, tracked publication
  ownership, Paho-only reconnect/resubscription, FTDI close/deinit, and an
  application-owned shutdown coordinator with injectable monotonic time.
- Existing regression scenarios were migrated from callback-inline application
  and adapter-owned exits to the approved owner-thread/error-return contracts.
  Internal frame-state assertions were replaced with exact output, pure-state
  snapshot, and change-only logging checks, not removed as runtime guarantees.
- Red evidence: the original MQTT delivery occurred before Paho buffer release
  (`Expected 0 Was 1`). Focused shutdown tests subsequently exposed success despite
  a destroy-time failure (`Expected 1 Was 0`, 8/9 passing). Reconnect tests exposed
  fatal treatment of retryable failures (42/44 passing). Application confirmation
  errors exposed missing status propagation (51/53 passing). Each intended
  assertion was observed in Docker before its corresponding fix.
- Docker environment preparation succeeded:
  `docker build --target test -t mqtt2dmx-test:local .`.
  Mounted-checkout regressions executed freshly with
  `docker run --rm --network none -v "$PWD:/workspace" mqtt2dmx-test:local test:all`.
  Final suite and production-image results are recorded below.
- Controlled concurrency suite repeated successfully 100 times (300 cases) using
  its Docker-built executable, with no broker or device access.
- Pinned Paho 1.3.1 source was inspected inside Docker: connected events occur on
  initial/automatic connections; receive dispatch is synchronized with destroy;
  destroy removes pending commands/responses and can invoke failure callbacks.
  Project cleanup relies on this singleton/pinned-library quiescence contract,
  not callback-local depth or a sleep. Real reconnect and hardware timing remain
  unverified; source inspection and mocks are not an integration run.
- No commit or staging was performed. Runtime verification was not performed:
  broker and FTDI execution were not approved.
- Final stop-intent regression also observed queued command application after a
  signal (`Expected 0 Was 1`, 53/54 application cases passing); the handler now
  releases such messages without application or confirmation.
- Final `docker build -t mqtt2dmx:local .` succeeded (exit 0). Its gate freshly
  compiled/executed 186 cases: 186 passed, 0 failed, 0 ignored. Production freshly
  compiled all seven C modules with `-fno-common`, linked real Paho/FTDI and pthread
  support, and produced the runtime image. Dependency layers reused cache; tests
  and application compilation/linking did not. No dependency upgrades occurred.
- Final counts: application 54, MQTT 46, DMX 26, gateway 21, commands 15, lights 6,
  shutdown 9, concurrent handoff 3, harness 6. Verification details and reproducible
  commands are in [test documentation](../tests/README.md).
- Phases 1-3 are implemented and deterministically verified. Runtime reconnect,
  Paho teardown timing, and FTDI cadence still require separately approved
  integration checks. Animation remains unimplemented and requires its own
  decisions and approval; no hardware state guarantee is inferred from shutdown.
