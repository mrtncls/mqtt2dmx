# Targeted maintainability refactors

Approved: 2026-10-31, following the four refactor recommendations.

## Scope and acceptance criteria

1. Centralize HA topic construction in a transport-independent protocol module.
   Configuration collision checks, routing, discovery, publication and application
   subscriptions must share the same topic definitions. Preserve exact topic
   bytes, configured bounds, stable identities and discovery-disabled behavior.
2. Extract private MQTT queue and lifecycle helpers. Preserve the public API,
   singleton ownership, callback ordering, 64-entry queue, 16-message drain budget,
   exactly-once message release, shutdown behavior and reconnect policy.
   Never hold the project mutex across Paho calls or application callbacks.
3. Separate configuration parsing into defaults/general fields, lights,
   removals and cross-topic validation. Improve semantic diagnostics with field
   names and zero-based array indices. Preserve strict rejection, defaults and
   transactional updates: invalid input must leave the entire destination intact.
4. Simplify the Makefile with one source list, generated header dependencies and
   separate compile/link flags. Preserve the executable and native libraries,
   support incremental and parallel builds, and enable warnings at compilation.

No dependency upgrades, protocol/lighting changes, generic framework, HA ownership
split, commit, image publication, broker connection or hardware execution.

## Sequence and validation

- Establish a fresh full-suite baseline in Docker.
- Add characterization tests for custom topic collision checks and transactional
  parsing. Add failing diagnostic assertions before changing diagnostics.
- Extract topics, MQTT helpers and configuration stages, then simplify Make.
- Run focused configuration tests to demonstrate diagnostic red/green.
- Run the full suite and build the production image using
  `docker build -t mqtt2dmx:local .`; distinguish fresh tests from cached results.
- In the production build stage, check clean/parallel build, no-op rebuild and
  rebuild after touching a transitive header; verify compiler warnings are enabled.
- Update user/test documentation and record actual results here.

All builds/tests execute in Docker. Existing Unity/CMock/Ceedling infrastructure
is sufficient. Pure refactoring uses passing characterization tests rather than
artificial behavior changes to manufacture a red phase.

## Status

All four refactors are implemented and verified. No commit or publication was
performed. Broker/hardware execution was not performed and was not approved.

- Added `ha_topics.c/.h` for command/state/discovery topics, the command filter,
  availability and routing. Existing `ha_protocol.h` includes the shared contract.
  Configuration validation, HA JSON output, the scheduler and application now
  use the shared builders. Added six direct topic cases and seven configuration
  characterization cases.
- MQTT polling now delegates to private shutdown, subscription and delivery
  helpers. Stop/close share queue-discard logic with explicit locked ownership.
  The existing public API, locking boundaries and callback behavior are unchanged.
- Configuration stages now report semantic field paths, including zero-based
  light/removal indices, duplicate fields and topic collisions. Twenty-three
  diagnostic cases were added. Rejection tests compare the entire destination
  rather than only a count field.
- Make now has a single source list, separate flags, warning-enabled compilation
  and generated transitive dependencies under `build/native/`. Default, `make`
  and `build` targets are incremental aliases. The existing `run` target now has
  a defined build prerequisite; running it remains approval-gated.
- Updated README, test coverage/selector documentation and the Docker workflow's
  formerly stale statement about the missing `build` target.

## Verification results (2026-10-31)

All commands ran from the repository root; all compilation/test execution was
inside Docker. No new test tooling or dependencies were required.

1. `docker run --rm --network none -v "$PWD:/workspace" mqtt2dmx-test:local test:all`
   freshly ran the baseline: **317 passed, 0 failed, 0 ignored**.
2. `docker run --rm --network none -v "$PWD:/workspace" mqtt2dmx-test:local test:config`
   against unchanged production code: **50 passed, 23 failed, 0 ignored**.
   All failures were the intended assertions for field/index-specific diagnostics,
   not compilation or infrastructure errors.
3. The same focused command after implementation: **73 passed, 0 failed,
   0 ignored**.
4. `docker build -t mqtt2dmx:local .` freshly executed **353 tests, all passing,
   none ignored**, then freshly compiled/linked the real Paho/FTDI/Jansson
   production binary and built the runtime image. No compiler warnings.
5. `docker build --target build -t mqtt2dmx-build:local .` exposed the same
   production build stage, reusing cached passing tests/native build.
6. `docker run --rm --network none mqtt2dmx-build:local sh -ec '...'` checked
   the following in a disposable container, without running the application:
   - `make clean` removed the executable/native outputs and preserved an
     unrelated marker under `build/test/`.
   - `make -j2` compiled all 13 sources with `-Wall`, linked successfully,
     generated 13 dependency files and emitted no warnings.
   - `make -q`, `make -q build` and `make -q make` confirmed no-op rebuilds.
   - After `touch src/config/config.h`, exactly seven direct/transitive
     dependents recompiled: application, HA owner, config, JSON helpers,
     transitions, HA JSON protocol and HA topics. The binary relinked and
     `make -q` then succeeded.
   - After `touch src/protocol/ha_topics.c`, only its object rebuilt, followed
     by linking; `make -q` then succeeded.

Remaining limitations: mocked tests do not establish physical DMX timing or
live-broker behavior. No runtime policy changed, and neither verification was
attempted. No blockers remain for this approved refactor scope.
