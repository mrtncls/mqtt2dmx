# MQTT to FTDI DMX gateway

docker run --name mqtt2dmx --rm --privileged -v /dev/bus/usb:/dev/bus/usb -e MQTT_ADDRESS=192.168.1.10:1883 mrtncls/mqtt2dmx

## Environment variables

MQTT_ADDRESS: required
MQTT_CLIENTID: optional, default mqtt2dmx
MQTT_TOPIC: optional, default dmx/channels/+/set

## MQTT topic

Publish message to dmx/channels/16/set with payload 199.
A dmx frame with value 199 will be send to channel 16.
A confirmation will be published to dmx/channels/16 with payload 199.

Commands must end with `/set`. Channels are decimal digits in the range 1-255
and values are decimal digits in the range 0-255. Leading zeros are accepted;
confirmations use canonical decimal values. Empty fields, signs, whitespace,
trailing junk, embedded NULs, and out-of-range values are rejected without an
update or confirmation. The frame still contains all 512 DMX slots, but channels
256-512 are not addressable through the MQTT protocol. Internal light state
supports all 512 slots; slot 0 remains the DMX start code.

## Error handling and shutdown

Dependency and allocation failures are reported to the application, which closes
both adapters and returns an unsuccessful exit status. Paho callbacks never
apply commands, write frames, or destroy the client. `MQTT_Poll()` transfers owned
input to the main thread, where commands are decoded without mutating incoming
buffers and applied before confirmation publication. Confirmations indicate
application, not physical delivery; QoS remains 0 and messages are not retained.

The incoming queue holds 64 messages, with topics limited to 1024 bytes and
payloads to 64 bytes. At most 16 commands are applied before each frame, so traffic
cannot starve output. Oversize messages and queue overflow are reported, rejected
without confirmation, and cause unsuccessful shutdown with resource cleanup.
Initial connection, subscription, and publication failures remain fatal.
After an established connection is lost, only Paho owns reconnect attempts;
the connected callback requests resubscription on the main thread.

SIGINT/SIGTERM stop frame generation and new command acceptance. Queued commands
are discarded without confirmations. Outstanding publications and asynchronous
disconnect are drained for up to 5000 ms, measured with a monotonic clock. Timeout
or adapter failure returns unsuccessfully; MQTT destruction and FTDI close/deinit
are idempotent and occur on normal and fatal exits. Pinned Paho may invoke failure
callbacks during destruction; their owned messages are released exactly once.
The deadline bounds draining, not additional time spent inside library teardown.
No blackout or final frame is sent, and closing output does not establish a
fixture's state. Interrupted DMX delays are still retried.

## Build

### Repository layout

```text
src/             Production C sources and colocated internal headers
tests/           Unity/CMock suites, runner, and compiler wrapper
  support/       Test helpers and external-library mock declarations
  fakes/         Thread-safe Paho test double for concurrent handoff tests
build/           Ignored generated test output
.agent-memory/   Architecture/refactor plans and handoff context
.github/         Contributor instructions and development skills
Dockerfile       Docker test, build, and runtime stages
Makefile         Production compilation (inside Docker)
project.yml      Ceedling configuration
Gemfile*         Pinned test-tool dependencies
```

[Application code](src/mqtt2dmx.c) owns configuration, light state, confirmations,
and the main loop. [Command decoding/formatting](src/command.c) and
[light-state operations](src/lights.c) are pure contracts. [MQTT](src/mqtt.c)
owns the synchronized incoming queue, Paho client, and in-flight publication
lifetimes. [DMX](src/dmx.c) owns FTDI output and transmits stable 513-byte snapshots;
[shutdown](src/shutdown.c) coordinates draining and adapter cleanup using the
[monotonic clock](src/clock.c). Module state is private, headers are self-contained,
and both test and production compilation use `-fno-common`.
Internal headers stay beside their implementations: this executable does not
currently expose a public library API. Tests discover sources under `src/`;
generated mocks remain in `build/`, never in production source directories.
Production objects and the executable are generated in the Docker build working
directory, with only the executable and runtime libraries copied to the final image.

### Production image

Build the production image:

```sh
docker build -t mqtt2dmx:local .
```

The multi-stage [Dockerfile](Dockerfile) requires the full test suite to pass
before building the application and producing the runtime image. Failed,
ignored, or zero executed tests fail the build. Unchanged inputs may reuse a
cached successful test stage; use `--no-cache` to force a fresh run.
Ruby, Ceedling, mocks, and test reports are not included in the runtime image.

## Tests

The suite preserves the original protocol, ownership, frame, failure, and signal
regressions, adapted to main-thread command application and application-owned
cleanup. New pure-contract, concurrency, saturation, and deadline tests extend
the baseline. The runner and production gate reject failures, ignored tests, and
empty suites. Final Docker verification passed all 186 cases.

Run the full deterministic C test suite by building the results target:

```sh
docker build --target test-results .
```

No separate `docker run` is needed, and this target does not build the production
application or runtime image. Normal production builds already consume this
stage, so a separate test command is optional when building the production image.
Unchanged inputs may reuse cached passing results. To force fresh execution
(also rebuilding the test environment's dependency layers):

```sh
docker build --no-cache --target test-results .
```

### Focused suites and mounted checkouts

For a focused suite, build the standalone `test` environment, then run a selector:

```sh
docker build --target test -t mqtt2dmx-test:local .
docker run --rm --network none mqtt2dmx-test:local test:mqtt2dmx
```

The `test` target only prepares the environment; unlike `test-results`, it does
not execute tests during the build. Focused selectors are `test:mqtt2dmx`,
`test:mqtt`, `test:dmx`, `test:gateway`, `test:harness`, `test:command`,
`test:lights`, `test:shutdown`, and `test:mqtt_concurrency`.

Rebuild the standalone image after editing tests or sources. Alternatively, mount
the checkout to test current files and retain generated reports in `build/`:

```sh
docker run --rm --network none -v "$PWD:/workspace" mqtt2dmx-test:local test:all
```

Building `test-results` keeps reports inside the build stage, not in the checkout.
The runner fails on failed, ignored, or zero executed tests. Each invocation
cleans generated test output first; do not run simultaneous tests against the
same mounted checkout. No broker, USB device, or privileged access is needed.

See [test coverage and limitations](tests/README.md) for the intended contracts,
red/green regression results, and remaining external verification limits.
Mocked tests do not verify production linkage; use the production build above
for that check.