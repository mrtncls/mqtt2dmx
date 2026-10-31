# MQTT to FTDI DMX gateway

A C gateway with legacy raw-channel commands and an optional Home Assistant MQTT
JSON light interface, calibrated brightness, local transitions and discovery.

## Releases

Use the [release skill](.github/skills/release/SKILL.md) (`/release`) to build and
publish to Docker Hub as `mrtncls/mqtt2dmx:YYYY-MM-DD.N`. The date is UTC and the
number starts at 1 each day, incrementing from the highest existing dated tag.
Every release also updates `mrtncls/mqtt2dmx:latest` to the same image and verifies
both remote tags. The production build includes the required test gate.

## Environment variables

MQTT_ADDRESS: required
MQTT_CLIENTID: optional, default mqtt2dmx
MQTT_TOPIC: optional, default dmx/channels/+/set
LIGHTS_CONFIG: optional, path to the JSON light configuration inside the container

Without `LIGHTS_CONFIG`, only the existing raw interface is enabled.

## Home Assistant lights

Copy [lights.example.json](lights.example.json) to your own configuration, check
the channel assignments against your fixtures, and mount it read-only. Example
runtime template (replace the broker and specific USB device with approved values):

```sh
docker run --rm \
  --device /dev/bus/usb/<bus>/<device> \
  --mount type=bind,src="$PWD/lights.json",dst=/etc/mqtt2dmx/lights.json,readonly \
  -e MQTT_ADDRESS='<broker-address>' \
  -e LIGHTS_CONFIG=/etc/mqtt2dmx/lights.json \
  mqtt2dmx:local
```

Do not grant all USB devices or privileged access just to run tests. Unit tests
need neither hardware nor a broker. The configuration is read once before opening
the device; invalid configuration fails startup without output. There is no live
reload, state restoration from disk, or persistence of remembered brightness.

### Configuration

The file is strict UTF-8 JSON, at most 128 KiB. Duplicate keys, unknown fields,
wrong types and invalid settings are rejected, not silently defaulted.

| Field | Meaning / default |
| --- | --- |
| `gateway_id` | Required stable identity; never derive it from a channel or changing client ID |
| `name` | Gateway device display name; defaults to `gateway_id` |
| `prefix` | Operational topic prefix; default `dmx` |
| `discovery` | Boolean, default `true`; disable only for manual setup/explicit cleanup |
| `discovery_prefix` | Default `homeassistant` |
| `birth_topic` | HA birth topic, default `homeassistant/status`; recognized payload is `online` |
| `lights` | Required array, 0-255 light definitions |
| `removals` | Optional persistent removal manifest, 0-64 exact topic pairs; see below |

Each light requires `id`, `name`, and `channel`. Optional `minimum` defaults to 0,
`transition` to 0.5 seconds, and `curve` to `smoothstep` (`linear` is also supported).
`channel` is an integer 1-255 and `minimum` an integer 0-255. IDs and channel
assignments must be unique. IDs/gateway IDs are 1-48 lowercase ASCII letters,
digits or hyphens, with no leading/trailing hyphen. Names are 1-96 UTF-8 bytes.
Prefixes and birth topics are 1-128 ASCII letters/digits, underscores, hyphens
or `/`, without empty segments or leading/trailing `/`. No MQTT wildcards.

`MQTT_TOPIC` remains the independent legacy subscription. Light commands subscribe
to `<prefix>/lights/+/set`; discovery also subscribes to the HA birth topic. All
enabled filters are resubscribed after reconnect. With lights enabled, legacy
filters that overlap generated publications, removal topics or the HA birth topic
are rejected; birth topics must not overlap light commands or publications.
Use distinct operational prefixes **and** gateway IDs for multiple gateways.

### Commands, state and transitions

For the example light:

```text
Command: dmx/lights/zithoek-wandlampen/set
State:   dmx/lights/zithoek-wandlampen/state
```

```json
{"state":"ON","brightness":128,"transition":2}
{"state":"OFF","transition":1.5}
{"state":"ON"}
{"brightness":64,"transition":0}
```

Publish commands at QoS 0 **without retain**. Retained deliveries are rejected
to avoid replaying stale commands on reconnect. Brightness must be an integer
0-255. Durations must be finite JSON numbers from 0 through 3600 seconds, rounded
to nearest millisecond, ties up. Explicit zero is immediate; omitted duration uses
the per-light default. A zero default disables implicit fading.

- Brightness-only commands are valid. At least state or brightness is required.
- `ON` with brightness zero means OFF. `OFF` with brightness sets the off target
  and remembers the supplied brightness if nonzero.
- `ON` without brightness recalls the last accepted nonzero target, initially 255.
  OFF during a fade remembers that target, not an intermediate output value.
- Only exact configured IDs route to lights. There is no numeric-channel fallback.
- Unknown fields (including unsupported colors/effects), duplicate keys, invalid
  types, malformed JSON, embedded NULs and invalid values are logged and rejected
  without altering the target or output. Optional fields may be absent, not invalid.

State snapshots look like `{"state":"ON","brightness":128}`. They describe the
**accepted target**, not instantaneous DMX output or proof of physical delivery.
Snapshots are retained and coalesced; no MQTT message is sent for each fade frame.

Transitions interpolate unrounded logical brightness using elapsed monotonic time.
Linear uses `t`; smoothstep uses `3*t*t - 2*t*t*t`. Retargeting starts from the current
unrounded value at command application time. Late frames jump ahead; the first
frame at/after the deadline contains the exact endpoint. Internal completion is
recorded only after a successful frame submission. All lights in one frame share
one timestamp. Separate scene commands are not an atomic/synchronized batch.

Calibration maps logical zero to raw zero, positive fractions below one to
`minimum`, and brightness `B` from 1-255 to:

```text
round(minimum + (B - 1) * (255 - minimum) / 254)
```

Rounding is nearest, ties up. With minimum 66, integer brightness 1/128/255 maps
to raw 66/161/255. The unavoidable off/on threshold and 8-bit stepping remain;
there is no gamma correction, temporal dithering, RGB, or repeating effect.

Legacy raw commands cancel a fade and hold the exact uncalibrated byte. A mapped
light also gets a logical state snapshot: raw zero maps to zero; positive values
at/below minimum map to one; other values use the inverse calibration formula,
rounded for state but kept unrounded internally. With minimum 255, every positive
raw value maps to logical 255. The next JSON command starts in that logical space,
so a below-minimum raw value may jump to the minimum. Other channels continue.

### Discovery, availability and removal

HA's MQTT integration automatically discovers each configured light; do not also
enable a manual/template entity for it. Retained configuration topics are:

```text
<discovery_prefix>/light/mqtt2dmx_<gateway_id>_<light_id>/config
```

Slugs cannot contain underscores, so the identity encoding is unambiguous. Channel
and display-name changes preserve identity and topics. Lights share the gateway
device identifier and advertise only JSON brightness and transition support.

Startup, reconnect and HA's `online` birth schedule a bounded refresh: retained
offline availability, manifest cleanup, discovery configurations, current states,
then retained online availability. One HA publication is scheduled per frame,
waiting for outstanding publications to complete before the next. Birth bursts
during refresh coalesce. Target changes during refresh are sent before online.
Large configurations therefore take several seconds to announce readiness.
QoS 0 completion does not prove that HA registered an entity or a fixture changed.

Availability uses `<prefix>/availability`, with `online`/`offline`. The retained
Last Will reports offline on unexpected loss. Graceful shutdown sends offline
when connected within the existing drain budget. Local fades continue during
retryable connection loss; reconnect publishes current authoritative targets,
never restores hardware output from retained state and never restarts fades.

Removing a light from `lights` alone cannot remove old retained broker data.
Keep an explicit manifest in the same persistent mounted configuration, for example:

```json
"removals": [
  {
    "discovery": "homeassistant/light/mqtt2dmx_gateway-1_old-light/config",
    "state": "dmx/lights/old-light/state"
  }
]
```

Each entry authorizes **only those two exact topics** for empty retained
publication. Never list another gateway's topics. Wildcards and removal of an
active discovery topic are rejected while discovery is enabled. Shared current
state topics are allowed during identity migration: cleanup runs while offline,
and current snapshots replace them before online.

Keep the manifest across restarts/offline periods until the gateway logs
`Discovery removal manifest completed` **and** broker/HA inspection confirms
the old entities/snapshots are gone. It is intentionally not deleted automatically:
QoS 0 completion is not a broker delivery guarantee. Retrying the same exact
manifest on reconnect/birth is safe. Then remove acknowledged entries from the
file and restart. This also handles changed light IDs, gateway IDs, operational
prefixes and discovery prefixes; list the exact previous topics.

Setting `discovery: false` alone does not erase old discovery. Before switching
to manual setup, disable discovery **and** list the old discovery/state pairs in
`removals`, run and verify cleanup, then remove the completed manifest entries.
Cleanup still works with discovery disabled or with an empty light list.

### Migrating Home Assistant templates and scenes

1. Back up the existing entity IDs used by scenes and automations.
2. Disable/remove the old template/manual light definitions; avoid two competing
   entities controlling the same channel.
3. Configure the gateway light IDs and the matching HA MQTT broker/discovery
   settings. Check the discovered entities and shared gateway device.
4. In HA, restore the intended entity IDs if needed. Stable unique IDs/discovery
   topics do **not** guarantee the old entity ID. Preserve an already-established
   gateway/light identity; an ID change is an explicit removal/migration.
5. Verify scenes and automations still reference the intended entities.

Existing HA scenes work through ordinary light commands. For an explicit duration:

```yaml
action: scene.turn_on
target:
  entity_id: scene.avond
data:
  transition: 2
```

Omitted transition uses each light's default; explicit zero is immediate.
Scene editors store targets, not the activation duration.

Optional troubleshooting/manual setup, **instead of** discovery (after cleanup):

```yaml
mqtt:
  light:
    - schema: json
      name: Zithoek wandlampen
      unique_id: mqtt2dmx_gateway-1_zithoek-wandlampen
      command_topic: dmx/lights/zithoek-wandlampen/set
      state_topic: dmx/lights/zithoek-wandlampen/state
      availability_topic: dmx/availability
      brightness: true
      supported_color_modes: ["brightness"]
      transition: true
```

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
payloads to 512 bytes. At most 16 commands are applied before each frame, so traffic
cannot starve output. Oversize messages and queue overflow are reported, rejected
without confirmation, and cause unsuccessful shutdown with resource cleanup.
Initial connection, subscription, and publication failures remain fatal.
After an established connection is lost, only Paho owns reconnect attempts;
the connected callback requests resubscription on the main thread.

The queued payload buffers occupy at most 32,832 bytes including terminators
(28,672 more than the previous 64-byte limit). On a 64-bit build the 64 queued
messages, including maximum topics and metadata, occupy 101,504 bytes, excluding
allocator overhead, the currently consumed message and outstanding publications.
HA serialization is separately bounded to 4096 bytes and at most one HA message
is in flight. Legacy confirmations retain their original publication semantics.
Accepted commands are logged immediately. Animation-driven output changes log
one channel-count summary on the first and then every 256th changed frame,
never one line per changed channel per frame.

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
  app/           Application lifecycle, shutdown, and HA publication scheduler
  adapters/      MQTT/Paho, DMX/FTDI, and monotonic clock adapters
  config/        Light configuration and strict JSON helpers
  lighting/      Light state, transitions, and brightness calibration
  protocol/      Legacy commands and Home Assistant JSON/topic contracts
tests/           Unity/CMock suites, runner, and compiler wrapper
  unit/          Module-focused suites, grouped like src/
  integration/   Cross-module gateway and concurrent MQTT handoff suites
  harness/       Tests of the test infrastructure itself
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

[Application code](src/app/mqtt2dmx.c) owns configuration, light state, confirmations,
and the main loop. [Configuration](src/config/config.c), [strict JSON helpers](src/config/json_contract.c),
[HA protocol](src/protocol/ha_protocol.c), [transitions/calibration](src/lighting/transition.c) and
[HA owner/scheduler](src/app/ha.c) implement the optional interface.
[Command decoding/formatting](src/protocol/command.c) and
[light-state operations](src/lighting/lights.c) are pure contracts. [MQTT](src/adapters/mqtt.c)
owns the synchronized incoming queue, Paho client, and in-flight publication
lifetimes. [DMX](src/adapters/dmx.c) owns FTDI output and transmits stable 513-byte snapshots;
[shutdown](src/app/shutdown.c) coordinates draining and adapter cleanup using the
[monotonic clock](src/adapters/clock.c). Module state is private, headers are self-contained,
and both test and production compilation use `-fno-common`.
Internal headers stay beside their implementations: this executable does not
currently expose a public library API. Tests discover sources under `src/`;
generated mocks remain in `build/`, never in production source directories.
Build configuration adds each source group to the header search path, so includes
and Ceedling `TEST_SOURCE_FILE` declarations use unique basenames. Keep source,
header, and test basenames unique across groups; focused test selectors are unchanged.
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
empty suites. The earlier raw-only architecture baseline passed 186 cases;
the current suite additionally covers the optional light interface.

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
`test:lights`, `test:shutdown`, `test:mqtt_concurrency`, `test:config`,
`test:transition`, `test:ha_protocol`, `test:ha`, and `test:home_gateway`.

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