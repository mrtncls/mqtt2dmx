# Test coverage and limitations

## Regression contracts

The original 126-case baseline has been migrated to the phases 1-3 architecture.
Protocol, frame, ownership, signal, and unsuccessful-exit contracts remain;
callback ordering and cleanup ownership are intentionally changed. Additional
pure, queue, lifecycle, and concurrent-handoff tests extend that baseline.
Do not skip cases or weaken assertions to obtain a green build.

The approved contracts are:

- Commands require a final `/set` suffix, a digits-only channel from 1 through
  255, and a digits-only value from 0 through 255. Empty fields, signs, whitespace,
  trailing junk, binary payloads, and numeric overflow are rejected without a DMX
  update or confirmation. Leading zeros are valid and confirmations use canonical
  decimal values. Prefixes may contain `/set` as part of another segment.
- Dependency and allocation failures report a diagnostic and exit unsuccessfully
  after releasing acquired resources. This includes partially constructed
  messages, received Paho buffers, MQTT clients, and initialized FTDI resources.
  Asynchronous connect failure is now required to terminate rather than only log.
- Paho callbacks release Paho-owned buffers after copying into a synchronized
  64-entry queue. Topic/payload limits are 1024/512 bytes; overflow or oversize
  is a visible fatal error, never a successful command. Only the main thread
  applies commands, at most 16 per frame. Frame snapshots remain stable.
- Adapters return errors. The application owns cleanup and exit status. MQTT
  destruction occurs outside callbacks and handles callbacks invoked by destroy.
  Tests audit exactly-once releases and partial initialization.
- SIGINT/SIGTERM stop frames and discard queued input without confirmations.
  Shutdown drains publications/disconnect for 5000 monotonic milliseconds, then
  closes both adapters. Delayed, failed, and never-completing operations, exact
  deadline boundaries, and repeated close are tested. No blackout is introduced.
- Paho alone owns automatic reconnect; initial failure remains fatal, later
  reconnect failures remain retryable, and connected events request resubscription.
- Paho's zero `topicLen` means a NUL-terminated topic, not an empty topic.
- Deterministic unit tests and cross-module tests mock external libraries.
  The concurrent suite uses a thread-safe Paho double rather than CMock.
  Live-broker, hardware, and wall-clock timing verification remain excluded.

## Running

From the repository root, run the full suite during the Docker build:

```sh
docker build --target test-results .
```

No separate `docker run` is needed. This target runs `test:all` without building
the production application or runtime image. Normal production builds already
require this stage. Unchanged inputs may reuse cached passing results; force
fresh execution with:

```sh
docker build --no-cache --target test-results .
```

`--no-cache` also rebuilds the test environment's dependency layers. The default
suite discovers all `test_*.c` files; parameterized cases are counted and
reported individually. None are skipped.
For focused suites or mounted-checkout testing, use the standalone `test` image
and `docker run` commands in the [project README](../README.md). Focused selectors
are `test:mqtt2dmx`, `test:mqtt`, `test:dmx`, `test:gateway`, `test:harness`,
`test:command`, `test:lights`, `test:shutdown`, `test:mqtt_concurrency`,
`test:config`, `test:transition`, `test:ha_protocol`, `test:ha_topics`, `test:ha`, and `test:home_gateway`.
Pass one selector per container invocation; use `test:all` for the complete suite.

The [runner](run.rb) cleans generated output, propagates Ceedling failures, and
checks the JSON summary for a nonempty, all-passing suite. With a checkout mounted,
the report is `build/artifacts/test/tests_report.json`; mocks and binaries also
stay under ignored `build/`. Mounted output is owned by the container's user.
Building `test-results` retains reports inside that build stage, not in the host
checkout. For standalone `docker run --rm` invocations without a mount, output
is discarded with the container.

The [Dockerfile's test stage](../Dockerfile) pins Ruby 3.3.6 by image digest, libftdi1
development headers 1.5-6+b2, Jansson development headers 2.14-2, and Paho 1.3.1
by tag plus commit verification.
The [bundle lockfile](../Gemfile.lock) pins Ceedling 1.0.1 and its Ruby dependencies.
That Ceedling release bundles CMock 2.6.0 and Unity 2.6.1. Build requires access
to Docker Hub, Debian package repositories, GitHub, and RubyGems; test execution
does not require external services. The `test-results` stage runs the full suite
and creates a success marker consumed by the production `build` stage. This
dependency prevents Docker from skipping tests when building the runtime image.
Unchanged inputs can reuse cached passing results. The standalone `test` target
only prepares the environment and retains the runner entrypoint for focused or
mounted-checkout testing. The existing Alpine production base and Paho dependency
are unchanged; Jansson development/runtime packages are added for strict JSON.
No Ruby tooling or test artifacts are copied into the runtime image.

## Home Assistant and transition coverage

| Suite | Contracts |
| --- | --- |
| [Configuration](unit/config/test_config.c) | Stable IDs, names, exact channel mapping, duplicate IDs/channels/keys, unsupported fields, type/range/bound checks, 255-light limit, file loading, custom prefixes, self-subscription rejection, explicit migration manifests, field/index diagnostics and byte-for-byte preservation of rejected destination configurations |
| [Transitions](unit/lighting/test_transition.c) | All 256 template brightness values, fractional off threshold, inverse calibration, both curves, ascending/descending, default versus explicit zero, elapsed time/late endpoints, retarget precision, maximum duration, equal targets, remembered brightness, raw overrides, submission-gated completion |
| [HA protocol](unit/protocol/test_ha_protocol.c) | Actual HA JSON examples, optional-field rules, reordering/whitespace, invalid types/values, malformed/duplicate/NUL JSON, 512-byte input boundary, duration rounding, exact ID lookup, custom topics, escaped discovery names, full-length identity and worst-case 4096-byte output bounds |
| [HA topics](unit/protocol/test_ha_topics.c) | Exact command/state/discovery topics, command filter and availability, custom prefixes and maximum-length gateway topics; shared by validation and publication |
| [HA owner/scheduler](unit/app/test_ha.c) | Independent fades, calibrated frames, protected start code, raw/state reconciliation, startup ordering, single in-flight publication, coalescing, offline continuation, reconnect/birth refresh without restarting fades, explicit exact-topic cleanup with discovery disabled, offline removal retry, retained command rejection, fatal send status |
| [Home gateway](integration/test_home_gateway.c) | Real application/MQTT/decoder/engine/scheduler with mocked Paho/DMX/clock: application timestamps, calibrated endpoints, shorter transitions replacing active fades for both curves without resuming the old fade, legacy confirmation bytes and retention, logical reconciliation, queue drain budget between frames, offline fading, retained replay rejection, send/clock/frame failures and stop intent |
| [MQTT](unit/adapters/test_mqtt.c) | Opt-in three-filter reconnect, retained offline will, retention flag forwarding, completion accounting, graceful offline draining, connection-loss-before-subscribe and disconnected shutdown |
| [DMX](unit/adapters/test_dmx.c) | Unchanged transport sequencing and idle frames; changed-frame logging bounded to one summary per 256 changed frames, including a 257-frame deterministic run |

Behavioral increments observed assertion failures before implementation, then
passed their focused Docker suites. The saved
[implementation plan](../.agent-memory/transitions-animation-plan.md) records
red/green and final production-build evidence. Logging tests use constant-memory
mock callbacks for the long run; other transport tests retain strict ordered
expectations. No sleeps, real broker, USB device or privileged container is used.

The [targeted refactor plan](../.agent-memory/targeted-refactors-plan.md) records
the later 317-test baseline, 23 failing diagnostic assertions followed by 73/73
passing configuration cases, and a fresh 353/353 full-suite production build.
The MQTT concurrency and lifecycle suites continue to cover the extracted
private helpers. Production Make checks in Docker also verified clean/parallel,
no-op, transitive-header and single-source incremental builds.

Final validation on 2026-10-31: `docker build -t mqtt2dmx:local .` freshly executed
315 tests, all passing with none ignored. The production image built successfully;
unchanged native compile/runtime layers reused the preceding successful real-library
build. A network-disabled runtime `ldd` check resolved all shared libraries without
starting the gateway. Documentation links and patch whitespace also passed checks.

Mocks do not demonstrate HA entity registration, actual broker retention/restart
behavior, USB frame cadence, physical endpoint timing or perceived smoothness.
Those operational checks require explicitly approved broker/HA/device access.
The deterministic timing gate is exact endpoint output on the first successful
frame at/after the deadline, at most 16 input deliveries and one scheduled HA
publication per frame. Physical timing/visual thresholds remain to be agreed.

## Test structure

Module-focused suites live in `unit/`, mirroring the `app/`, `adapters/`,
`config/`, `lighting/`, and `protocol/` source groups. Cross-module gateway and
concurrent MQTT handoff suites live in `integration/`; they still use deterministic
test doubles, not a live broker or hardware. Harness self-tests live in `harness/`.
Shared helpers and mock declarations stay in `support/`, and the thread-safe Paho
double stays in `fakes/`. The runner and compiler wrapper remain at this directory's
root. Ceedling recursively discovers the three suite directories and `src/`.
Keep basenames unique so existing `test:<module>` selectors and mock/source
resolution remain unambiguous. Generated files belong only in ignored `build/`.

Use `test_given_<context>_when_<action>_then_<outcome>` names and three
clearly separated sections:

- `// Given (Arrange)`: initial state, inputs, and mock expectations.
- `// When (Act)`: explicit calls exercising the behavior under test.
- `// Then (Assert)`: observable outcomes and mock verification.

[DMX tests](unit/adapters/test_dmx.c) demonstrate this convention. Keep independent scenarios
in separate tests. A setup helper such as `expect_frame()` only configures mocks;
the test calls `DMX_WriteFrame()` explicitly. Calls establishing prior state belong
in Arrange. CMock expectations are configured before Act and verified in Assert;
frame callbacks also check transient write arguments at invocation time.
For fatal paths, `run_child()` executes the action in Act and returns captured
status/output for `assert_child_failure()` in Assert. The existing combined
`expect_child_failure()` helper remains available for unmigrated tests.

## Coverage map

This table describes the passing assertions, not exhaustive input-space or
hardware coverage.

| Suite | Observable behavior |
| --- | --- |
| [Application](unit/app/test_mqtt2dmx.c) | Channel 16/value 199 command, update-before-confirmation ordering, exact confirmation topic/payload lengths, channel 1/255 and value 0/255 combinations, topic restoration and single incoming release |
| Application | Channel 0/256 and value -1/256 rejection, topic with `/set` but no channel separator; no DMX update or publication |
| Application | Missing required address, optional client/topic defaults and overrides, initialization order, repeated frames until stop, SIGINT/SIGTERM handler registration and stop intent |
| Application | Missing/nonterminal/repeated `/set`, empty/textual fields, signed/whitespace/fractional/trailing input, overflow, prefix containing `/set`, leading zeros, confirmation digit lengths |
| Application | Independent optional overrides, real SIGINT/SIGTERM delivery in isolated children, disconnect on normal signal shutdown |
| [MQTT](unit/adapters/test_mqtt.c) | Explicit-length copying, independent terminated buffers, empty payload, embedded NUL bytes during copying, exactly-once owned-buffer release |
| MQTT | Client creation/registration, connect options, connect-success subscription at QoS 0, reconnect and resubscribe after loss |
| MQTT | Explicit nonzero and zero-length topic conventions, release of Paho buffers before main-thread delivery, acknowledgement, unchanged input bytes |
| MQTT | Publish bytes, length, QoS and retained flag; ownership retained until completion; release on success and asynchronous failure |
| MQTT | Empty-payload publishing; deferred disconnect-success destruction; create/registration errors and immediate/asynchronous connect, subscribe, send and disconnect failures, including NULL failure responses |
| MQTT | Failure at each of the three message allocations, partial-message cleanup, receive-buffer cleanup without consumer delivery on allocation failure, message/client cleanup before fatal exits |
| [DMX](unit/adapters/test_dmx.c) | Ordered FTDI setup and exact device/serial parameters, complete zero frame, channel byte offsets and unaffected slots/start code |
| DMX | Break on/off and 120/16/12000 microsecond delay sequence, persistent repeated 513-byte frames, value clearing, change-only logging |
| DMX | Every FTDI initialization step, break-on/off, each delay, short/zero/negative writes: require controlled failure and acquired-resource cleanup, not an assertion abort |
| DMX | EINTR during each of the break, mark-after-break, and frame delays retries the interrupted delay and completes the frame rather than treating a signal as an I/O failure |
| [Gateway](integration/test_gateway.c) | Real application/MQTT/DMX modules together: initialization, subscription, Paho delivery, confirmation, frame bytes, ownership, disconnect and destruction |
| Gateway | Explicit/zero topic lengths, boundaries and invalid commands, binary payload rejection, failure at each of the six incoming/confirmation allocations and cleanup across modules |
| [Harness](harness/test_harness.c) | Child assertion isolation, exit-time verification, allocation injection at each position, detection of missing frees before exit |
| [Commands](unit/protocol/test_command.c) | Immutable explicit byte spans, typed channel/value, unchanged output on errors, exact parent-topic length, canonical formatting for all 256 byte values |
| [Lights](unit/lighting/test_lights.c) | All 512 slots, protected start code, invalid slots leave state unchanged, independent stable snapshots |
| [Shutdown](unit/app/test_shutdown.c) | Fake monotonic time, delayed completion, exact deadline, failures during drain/destruction, both adapters closed |
| [Concurrency](integration/test_mqtt_concurrency.c) | Two producer threads and a polling owner, exactly-once delivery, per-producer ordering, 16-message budget, arrivals during stop, synchronous send completion |

## Isolation and test seams

- Unit executables link the module under test and, where needed, the real shared
  [application cleanup helper](../src/app/shutdown.c). Application tests mock the
  project MQTT/DMX/light-state/shutdown interfaces; wrapper tests execute real wrapper logic and mock
  only Paho or FTDI. The gateway suite links application, commands, lights, both
  adapters, and cleanup; it mocks only Paho, FTDI, delays, and monotonic time.
- [Paho](support/paho_api.h) and [FTDI](support/ftdi_api.h) mock-input headers
  contain only used declarations and include the real library headers. Conflicting
  signatures therefore fail compilation. CMock generates implementations; these
  headers are not alternate production libraries.
- [Delay mocking](support/delay.h) replaces `usleep`, so timing checks assert the
  requested durations and order rather than waiting or measuring wall-clock time.
- Asynchronous callbacks are captured and invoked synchronously by tests, followed
  by `MQTT_Poll()` for deferred shutdown. The pinned Paho library can retain its
  mutex and client references across a callback; destruction inside that callback
  could wait for the current thread or invalidate references Paho still uses.
  Callback event flags are atomic; the queue and publication list are synchronized.
  Paho 1.3.1 source inspection confirms receive callbacks run under its client
  synchronization and destroy removes responses/commands while invoking failure
  callbacks. No project lock is held across Paho calls or destruction.
  The singleton relies on this pinned library quiescence contract, not a sleep
  or a callback-depth guess.
- [Thread-safe Paho double](fakes/paho_fake.c) is linked only into the concurrent
  suite. CMock's generated bookkeeping is not used concurrently. Real MQTT adapter
  code runs with two pthread producers and a main-thread consumer; no broker is
  contacted. This is controlled concurrency evidence, not a race-detector proof.
- `MQTTMessage` carries explicit topic/payload byte lengths. Application fixtures
  initialize these lengths, and MQTT copy tests assert them. This lets validation
  reject an entire binary payload rather than accept a prefix before a NUL byte.
- FTDI failure-loop fixtures initialize the device in Arrange before exercising
  the failing operation, so cleanup tests do not depend on earlier test cases.
- Adapter state is private; tests capture dependency handles and inspect output
  rather than writing adapter globals. Environment variables and signal
  dispositions are reset/restored between cases. Stop requests are tested both directly and by
  raising real process signals; crash-prone cases run in isolated children.
- The [test compiler wrapper](compile.rb), selected in [project.yml](../project.yml),
  adds `-Dmain=mqtt2dmx_main` only when compiling `src/app/mqtt2dmx.c`. Ceedling's regular
  flags apply to entire test executables, so setting this flag there would also
  rename Unity's entry point. The wrapper leaves the runner's `main` unchanged.
  Production sources need no test-only conditional directives; production builds
  use the original `main` and do not invoke the wrapper.
- Test and production compilation use `-fno-common`; the duplicate topic owner
  is gone. Public internal headers do not require transitive Paho/FTDI includes.
- Unity parameterized tests are enabled. Ceedling backtrace runners are disabled
  because the pinned Unity generator emits unescaped string arguments in its
  command-line test-list output. Child-process isolation supplies failure status,
  signal number, and diagnostics without this broken runner path.
- [Test helpers](support/test_helpers.c) use linker wrapping to observe project
  allocations/frees and audit them before fatal exits. Allocation injection
  tracks up to six owned allocations (incoming and confirmation messages).
  Fatal paths run in forked children with an alarm and core dumps disabled.
  Unity assertions are caught inside the child rather than jumping back into the
  inherited parent runner. Child exit verifiers check pending mock expectations
  before accepting an exit; parent-side copies are then discarded.
  Exit 123 denotes a child assertion failure and 124 a missing free.
  Parent assertions require the intended exit status and diagnostic, so a
  crash, timeout, unexpected mock call, or cleanup omission cannot masquerade
  as the expected failure. These are Linux/Docker tests, not portable host tests.

## Other limits

- There is no live-broker, hardware, race-detector, or real-time verification.
  Cross-module flow is tested, but external libraries are mocked; this is not an
  end-to-end production deployment.
- Tests preserve the message handler's channels 1-255 and values 0-255. Although
  frames have 512 slots plus a start code, setting channels 256-512 is not supported
  by the external protocol. Pure light-state operations support all 512 slots.
- Allocation injection covers project message allocations, not allocations
  internal to external libraries. The free audit is not a general leak detector.
- Signal tests assert the application stops frames and delegates cleanup. Separate
  lifecycle and gateway tests require disconnect draining, timeout handling, and
  normal/fatal hardware cleanup through the public internal close operations.
- FTDI error handling uses explicit return-code checks, not assertion side
  effects, so initialization and frame writes also execute with assertions
  disabled.
- Numeric validation is bounded and length-aware. These tests are not a sanitizer
  or exhaustive input-space proof.
- No numerical code-coverage threshold is claimed; the coverage map identifies
  actual observable assertions rather than equating executed lines with correctness.

## Historical baseline evidence

The pre-expansion baseline executed 28 passing cases. The original red expansion
executed 123 cases: 47 passed, 76 failed, none ignored. Adapting callback sequencing
for safe deferred destruction and asserting copied byte lengths still produced
an observed red run: 37 passed, 86 failed, none ignored.
Three additional cases cover interrupted DMX delays during signal handling.

The green implementation was verified with:

```sh
docker build --target test -t mqtt2dmx-test:local .
docker run --rm --network none -v "$PWD:/workspace" mqtt2dmx-test:local test:all
docker build -t mqtt2dmx:local .
```

The environment build succeeded. Tests compiled, linked, and executed freshly
in the mounted checkout, without using cached passing results:

| Suite | Passed | Failed | Total |
| --- | ---: | ---: | ---: |
| Application | 49 | 0 | 49 |
| MQTT | 30 | 0 | 30 |
| DMX | 23 | 0 | 23 |
| Gateway | 18 | 0 | 18 |
| Harness | 6 | 0 | 6 |
| **Total** | **126** | **0** | **126** |

No cases were ignored. Docker returned exit status 0. The production build also
executed all 126 tests freshly, compiled and linked against the existing
Alpine/Paho/FTDI dependencies, and produced the runtime image successfully.
No broker connection or hardware runtime verification occurred.

## Phases 1-3 verification (2026-10-31)

Focused Docker red tests observed callback-inline delivery, ignored destroy-time
failure, fatal handling of retryable reconnect errors, missing confirmation-error
propagation, and command application after stop intent. The corresponding fixes
passed subsequent focused/full runs; infrastructure crashes or unconsumed
parent-process mocks were corrected before counting intended assertion failures.

Final verification:

```sh
docker build -t mqtt2dmx:local .
```

The test gate freshly compiled and executed all 186 cases; none were ignored.
Production freshly compiled all seven production C modules with `-fno-common`,
linked real Paho/FTDI dependencies and pthread support, and produced the runtime
image (exit 0). Dependency layers reused cache; tests and production compilation
did not.

| Suite | Passed | Failed |
| --- | ---: | ---: |
| Application | 54 | 0 |
| MQTT | 46 | 0 |
| DMX | 26 | 0 |
| Gateway | 21 | 0 |
| Commands | 15 | 0 |
| Lights | 6 | 0 |
| Shutdown | 9 | 0 |
| Concurrent handoff | 3 | 0 |
| Harness | 6 | 0 |
| **Total** | **186** | **0** |

Before the final build, the Docker-built concurrent executable was repeated
100 times (300 passing cases):

```sh
docker run --rm --network none -v "$PWD:/workspace" --entrypoint sh mqtt2dmx-test:local -c \
  'for iteration in $(seq 1 100); do
     build/test/out/test_mqtt_concurrency/test_mqtt_concurrency.out > /tmp/concurrency-result.txt 2>&1 ||
       { cat /tmp/concurrency-result.txt; exit 1; }
   done'
```

This repeated controlled handoff/stop tests without external services. It does
not establish race-freedom under every schedule or real Paho reconnect behavior.
No broker connection, FTDI actuation, hardware timing, or race-detector run was
performed. The existing dependency versions and Docker production stages remain
unchanged.
