# LED transitions and animations plan

Created: 2026-10-31.

## Scope and current status

Extracted from the remaining phases 4-5 and animation-related phase 6 work in
[the architecture plan](architecture-refactor-plan.md). Phases 1-3 are complete;
their recorded 186 passing tests are historical evidence, not a new test run.
The implementation now includes strict configured HA JSON lights, calibrated
logical transitions, bounded retained discovery/state/removal scheduling, and
availability. The existing main-thread ownership, bounded incoming handoff,
stable output frames, monotonic clock and legacy raw confirmations are preserved.

Updated: 2026-10-31 during implementation.

Approved direction: native Home Assistant MQTT JSON lights with MQTT
auto-discovery, gateway-side
per-light calibration, and locally executed transitions with an overridable
default duration. Light topics use stable configured IDs, not DMX channel
numbers; each light configuration maps its ID to a DMX channel.
This supersedes the custom comma-separated protocol and
raw-value interpolation proposal. The current request authorizes complete runtime
implementation and documentation, but not staging, a commit, or real equipment.

## Implementation contracts (2026-10-31)

- Opt in using `LIGHTS_CONFIG`, a read-only mounted JSON file, at most 128 KiB.
  Jansson provides strict UTF-8/JSON parsing, duplicate-key rejection and escaping;
  add its development/runtime packages to the existing Docker stages.
  Maximum 255 lights, IDs/gateway IDs 1-48 lowercase ASCII letters/digits/hyphens
  (no leading/trailing hyphens), names 1-96 bytes, prefixes/topics 1-128 bytes.
  Reject unknown configuration and command fields rather than ignore typos.
- Incoming payloads become 512 bytes (64 queued payload buffers occupy at most
  32,832 bytes including terminators, 28,672 bytes more than before).
  Preserve 64 messages, 1024-byte topics and 16 deliveries/frame. Discovery
  serialization has a separate 4096-byte bound (including worst-case escaping).
- Brightness-only commands are valid. ON with zero brightness means OFF.
  OFF with brightness updates remembered brightness when nonzero, but targets
  zero. ON without brightness recalls the last accepted nonzero target (255
  initially); OFF during a fade preserves that target, not an intermediate value.
  Empty commands, unsupported keys and unconfigured IDs are rejected visibly.
- Durations are finite 0-3600 seconds, rounded to nearest millisecond, ties up.
  Default 500 ms and smoothstep; explicit zero is immediate. Interpolation uses
  double logical brightness with bounded values 0-255 and elapsed milliseconds;
  integer outputs round nearest, ties up. Positive fractions below one map to
  minimum, never extrapolate through zero. Retarget from the unrounded current
  value. Unsigned elapsed subtraction avoids deadline addition overflow.
  Interpolation only converts elapsed values below 3,600,000 ms to double;
  brightness stays in 0-255 and easing in 0-1. Duration conversion's maximum
  3,600,000 fits uint32_t; calibration intermediates are bounded doubles.
- Raw commands preserve exact output until the next JSON command. Inverse mapping:
  zero -> zero, positive below minimum -> one, otherwise invert calibration;
  minimum 255 maps every positive raw value to logical 255. JSON snapshots round
  inverse brightness to integers; the next fade starts from that logical value.
- State is retained accepted target, not physical delivery. No per-frame MQTT.
  New interface publications are serialized one in flight, at most one scheduled
  per frame. Reconnect and HA birth schedule cleanup, discovery, current state,
  then online, waiting for completion between publications. Coalesce births
  during an existing refresh. Updates during refresh are dirty/coalesced.
  Do not advertise readiness until pending snapshots complete; QoS 0 completion
  is not proof of HA registration. Known offline skips routine publications.
- Cleanup uses an explicit bounded `removals` manifest in the persistent config:
  at most 64 exact discovery/state topic pairs. Keep entries until successful
  cleanup is observed; repeat empty retained deletes on every reconnect/birth.
  Validate topic shape, reject active discovery-topic collisions, never wildcard-delete.
  Shared current state topics may be cleared during migration then republished
  before online; this supports gateway identity changes without changing light topics.
  Operators explicitly authorize old gateway/prefix entries through this manifest.
  Discovery disabled does not implicitly erase anything; manifest cleanup still
  runs. No filesystem writes or undocumented automatic inventory are needed.
- Graceful stop publishes retained offline once while connected and drains it
  within the existing five-second budget. No final frame; loss uses retained will.
  Frames continue locally while disconnected.
- Replace per-channel frame logging with one summary per 256 changed frames;
  log accepted raw/JSON commands immediately. Deterministic acceptance: one
  timestamp per frame, <=16 input messages and <=1 HA publication per frame,
  exact endpoint on first successful frame at/after deadline. Physical timing
  and perceived smoothness thresholds require fixture/operator agreement and
  are not claimed from mocks.

Include existing Home Assistant scenes through normal light commands. Exclude
gateway-owned scenes/groups, external channels 256-512, RGB interpolation,
scheduled sequences, unrelated dependency upgrades, and unapproved broker/device
execution. MQTT discovery is part of the initial implementation, not a follow-up.
Unrelated phase 6 build hygiene remains in the architecture plan.

## Home Assistant MQTT contract

Keep immediate raw commands and their exact confirmations unchanged. Example
prefix `dmx` below is illustrative; preserve configured legacy prefixes.

| Topic | Direction | Meaning |
| --- | --- | --- |
| `dmx/channels/10/set` | Command | Existing immediate raw decimal value |
| `dmx/channels/10` | Confirmation | Existing applied raw value |
| `dmx/lights/keuken-spots/set` | Command | Native Home Assistant JSON command for a configured light ID |
| `dmx/lights/keuken-spots/state` | State | JSON logical brightness and ON/OFF state for that light |
| `dmx/availability` | Availability | Gateway online/offline status |
| `homeassistant/light/mqtt2dmx_gateway-1_keuken-spots/config` | Discovery | Retained HA light configuration |

Resolve the exact light ID through configuration to its DMX channel and
calibration. The ID is independent of both the channel number and display name:
rewiring a light or changing its display name must not change its MQTT topics or
HA identity. The example `keuken-spots` requires its own configured channel;
no channel assignment is implied here. There is no numeric-channel fallback
under `dmx/lights/`; numeric raw access remains under `dmx/channels/`.

Example commands:

```json
{"state":"ON","brightness":128,"transition":2}
{"state":"OFF","transition":1.5}
{"state":"ON"}
```

Home Assistant specifies `transition` in seconds, including fractional seconds;
convert to bounded internal time units with a documented rounding rule. Support
ordinary ON/OFF commands and optional brightness/transition fields. Do not
require command IDs, custom curve fields, or a separate fade action in HA.
Do not advertise unsupported colors, effects, or flashing.

Use a bounded JSON parser, not substring matching or a handwritten partial JSON
grammar. Jansson is selected and integrated; see implementation contracts above.
Test actual HA payloads, key reordering, and JSON whitespace. The current incoming
payload limit was 64 bytes: the implemented finite limit is 512 bytes,
including its queue memory cost, rather than silently relaxing limits. Preserve
the 64-message queue, 16-command drain budget, and established error policy unless
an explicit change is approved.

Reject malformed JSON, embedded NULs, invalid types, duplicate recognized keys,
out-of-range brightness, and invalid durations without changing light state.
Proposed duration range: 0-3600 seconds. Finalize handling of unknown fields,
brightness-only commands, ON with brightness 0, OFF with a brightness field,
and unconfigured light IDs before implementation. Missing optional fields are
valid; invalid supplied values must not silently become defaults.

Preserve `MQTT_TOPIC` for legacy input. Add an opt-in light configuration and
separate light command subscription (`dmx/lights/+/set` by default), with
custom-prefix support. Resubscribe
all enabled filters after reconnect, including the HA birth subscription used
by discovery; never subscribe to the gateway's own state, availability, or
discovery publications.

## Per-light configuration and calibration

Store stable light ID, display name, DMX channel, minimum DMX brightness,
default transition duration, and curve once in gateway configuration. Select a
simple configuration file format and Docker mount/environment wiring before
implementation. Proposed ID syntax: lowercase ASCII slug with letters, digits,
and hyphens; agree a finite length bound. Reject empty IDs, topic separators,
MQTT wildcards, duplicate IDs, duplicate channel assignments, out-of-range
channels, and invalid settings explicitly at startup. One configured light maps
to one channel; groups and aliases are outside the first release.

Unknown light IDs must be logged and rejected without output changes or success
state publications. Resolve by exact ID, never by display name. Changing a
channel mapping preserves the ID; renaming an ID is an explicit MQTT/HA migration
that also requires cleanup of old retained state and discovery entries.
Live configuration reload is not assumed.

For example, the supplied channel 10 template uses minimum DMX brightness 66.
Preserve its mapping for logical integer brightness B:

- B = 0: raw DMX 0 (off).
- B = 1: raw DMX minimum.
- B = 255: raw DMX 255.
- B in 1-255: `round(minimum + (B - 1) * (255 - minimum) / 254)`.

Fade in logical brightness space, then calibrate and quantize for output. Retain
enough internal precision to avoid accumulating rounding errors during retargets.
Specify rounding ties and fractional brightness behavior near zero before coding;
do not extrapolate the positive-brightness formula through the off boundary.
Test against the existing HA template at integer brightness values.

The minimum stable brightness imposes an unavoidable on/off threshold. Logical
fading avoids spending a raw ramp below that threshold, but cannot eliminate its
visible jump. Preserve remembered nonzero brightness for ON commands that omit
brightness; define startup fallback and OFF-during-fade memory behavior.
Disk persistence is not assumed.

Legacy raw commands remain uncalibrated. They cancel any active fade, set the
exact raw byte, and keep the original confirmation. Reconcile JSON state for a
configured light too. Specify inverse mapping and subsequent fade behavior for
raw values below the calibrated minimum, and the degenerate minimum of 255;
these values cannot always be represented exactly in logical brightness space.

## Home Assistant configuration and scenes

After gateway support is implemented, automatically discover each configured
light as a JSON light. No per-light HA YAML is needed in the normal setup:
configure HA's MQTT integration and the gateway's light definitions.
Replace existing template entities while retaining their entity IDs so scenes
and automations still refer to the same lights. Avoid running two competing
entities during migration.

The following manual configuration is an optional troubleshooting/opt-out
alternative, not an additional entity to enable alongside discovery:

```yaml
- schema: json
  name: Zithoek wandlampen
  unique_id: mqtt2dmx_light_zithoek-wandlampen
  command_topic: "dmx/lights/zithoek-wandlampen/set"
  state_topic: "dmx/lights/zithoek-wandlampen/state"
  availability_topic: "dmx/availability"
  brightness: true
  supported_color_modes: ["brightness"]
  transition: true
```

For this example, configure ID `zithoek-wandlampen` with DMX channel 10 and
minimum brightness 66. HA no longer needs either hardware value. Keep HA unique
IDs independent of channels, and include a stable gateway identity if multiple
gateways share HA. Preserve any already-established unique ID during migration.

Scene editors store target states, not the activation duration. An automation,
script, or dashboard action can activate an unchanged scene with:

```yaml
action: scene.turn_on
target:
  entity_id: scene.avond
data:
  transition: 2
```

HA sends individual commands with durations; the gateway generates intermediate
frames. Scene commands are not an atomic batch and may start on different frames.
Do not promise synchronized start times across all scene lights.

| Command | Planned behavior |
| --- | --- |
| Explicit positive `transition` | Use that duration |
| Explicit `transition: 0` | Apply immediately |
| Omitted `transition` | Use configured per-light default |
| Legacy raw `/set` | Apply immediately, regardless of defaults |

Recommended initial default: 0.5 seconds and smoothstep, configurable per light.
This also affects ordinary brightness changes and direct scene activation.
A configured zero default disables implicit fading. Explicit HA durations always
override the default.

## Home Assistant MQTT auto-discovery

Include discovery in the initial release. Generate discovery payloads from the
same validated light configuration used for routing and calibration; do not
maintain separate per-light HA definitions.

- Use retained single-component discovery at
  `<discovery_prefix>/light/<unique_id>/config`, with prefix `homeassistant`
  by default. Per-light messages allow bounded serialization and independent
  removal; device-level discovery can be reconsidered if scale warrants it.
- Require a stable configured gateway ID. Derive a collision-free unique ID
  from gateway ID and light ID, for example
  `mqtt2dmx_gateway-1_keuken-spots`. Finalize an unambiguous encoding within HA's
  discovery-topic character restrictions. Never derive identity from a channel,
  display name, process ID, or a random value regenerated at startup.
- Include `schema: json`, the light name, unique ID, command/state topics,
  brightness support, `supported_color_modes: ["brightness"]`, transition
  support, and availability with explicit `online`/`offline` payloads.
  Include shared device identifiers/name for the gateway so its lights appear
  under one HA device, and accurate origin metadata. Do not invent hardware
  manufacturer or firmware version information.
- Keep operational topics unchanged: discovery points HA at
  `dmx/lights/<light-id>/set` and `/state`. Support custom operational and
  discovery prefixes. Multiple gateways require distinct operational prefixes
  as well as distinct identities to prevent shared command/availability topics.
- Make discovery explicitly configurable (recommended enabled for the new light
  interface). Provide discovery prefix and HA birth-topic settings; default birth
  topic `homeassistant/status`, online payload `online`. HA's MQTT integration
  must use the matching broker/discovery settings.

Publish current discovery configurations on gateway startup/reconnect and
republish them on HA's online birth message, along with current light state and
gateway availability. Subscribe to the birth topic again after reconnect.
Retained discovery supports HA starting after the gateway; birth-triggered
republishing also supports recovery when broker retained data was lost.
Discovery and birth messages must never apply a light command or restart a fade.

Schedule bounded batches on the main thread without starving DMX output or
creating an unbounded in-flight publication backlog. Coalesce repeated birth
notifications. Define publication ordering/completion tracking so configuration
and authoritative state are available before advertising readiness; do not treat
a publish call alone as proof that HA has registered the entity. Apply the
existing explicit publication-error policy and clean up owned buffers on failure.
Bound and test outgoing discovery payloads independently of incoming command
limits, including JSON escaping for names and configured topic lengths.

Removal and migration are required, not deferred:

- Publish an empty retained payload to the exact old discovery config topic to
  remove a light. Clear its old retained state as appropriate; never wildcard
  delete broker topics or remove another gateway's entities.
- Select a bounded persistent inventory of previously published discovery topics
  or an explicit removal manifest before implementation. Merely comparing the
  current configuration in memory cannot find deletions across restarts.
  Define Docker persistence, failure handling, and retryable cleanup when offline.
- Handle removed/renamed light IDs and changed gateway IDs/discovery prefixes.
  Changing only display name or DMX channel updates the same entity.
  Disabling discovery does not itself erase retained configs: provide a
  documented explicit cleanup operation before switching to manual setup.
- Preserve existing HA entity IDs during template migration. Unique IDs and
  discovery topic names do not guarantee the same HA entity ID. Remove/disable
  the old manual definition, restore the intended entity ID in HA as needed,
  and verify scenes still reference it without duplicate entities.

## State, availability, and reconnect

Use the native JSON state shape, for example
`{"state":"ON","brightness":128}`, with logical rather than raw brightness.
Do not overload legacy confirmations or require custom accepted/completed/
cancelled events for the initial integration.

Recommended state model, to finalize before coding: publish the accepted target
when the owner applies a command so HA controls remain responsive. This is target
state, not instantaneous output or proof of physical delivery. Keep instantaneous
brightness separate internally for retargeting. No per-frame MQTT publications;
completion tracking is internal unless a later diagnostics API is approved.

Keep QoS 0 and non-retained commands. Preserve legacy non-retained confirmations.
For new JSON state and availability, plan retained snapshots plus a retained
offline Last Will. Finalize startup/reconnect ordering: publish current
authoritative state before online, replace stale retained snapshots, and do not
restore hardware output from retained state. Handle graceful offline publication
within the existing shutdown budget without sending a final DMX frame.

During retryable established-connection loss, local fades continue. Avoid offline
event queues; retain bounded current state and republish it on reconnect.
Do not attempt routine state publications while known disconnected; preserve
the existing fatal policy for attempted publication failures unless separately
approved. No QoS 0 delivery or exactly-once guarantee is implied.

## What looks good on dimmable LEDs?

| Curve / feature | Use | First release recommendation |
| --- | --- | --- |
| `linear`: E(t) = t | Constant rate of change in logical brightness; useful baseline | Include |
| `smoothstep`: E(t) = 3t^2 - 2t^3 | Gentle start and finish for room-light fades | Include; gateway default |
| `smootherstep` | Even softer endpoint acceleration | Defer unless comparison shows a benefit |
| Ease-in / ease-out | Deliberately slow starts or slow finishes | Defer |
| Pulse / breathe | Repeated rise and fall | Later animation phase; needs repeat and stop semantics |
| Flicker / strobe | Effect lighting rather than ordinary dimming | Outside initial scope |

For normalized elapsed time t in [0,1], evaluate
`start + (target - start) * E(t)`. Easing changes the timing profile, not the
fixture's brightness response. Smoothstep has zero endpoint slope but cannot
guarantee smooth velocity across a mid-fade retarget.

Human brightness perception and the LED driver need separate consideration.
A linear DMX ramp need not look perceptually linear. Gamma/fixture calibration
is a separate mapping, not a substitute for easing; some drivers already apply
a dimming curve. Compare on the actual fixture before adding correction, avoid
blindly applying gamma twice, and preserve existing `/set` raw-value semantics.
The first release includes the existing minimum-brightness calibration, not
additional gamma correction or a claim of perceptually uniform brightness.

8-bit output has only 256 levels. Low-end steps or the fixture's minimum stable
brightness may dominate appearance, especially on long fades. Faster sampling
or a different easing curve cannot create extra DMX levels. Do not add temporal
dithering or 16-bit fixture addressing incidentally.

## Engine and output requirements

- Independent transition per channel in fixed-capacity storage, with protected
  start-code slot 0; external channel range remains 1-255.
- Start timing when the main thread applies a command, not when MQTT receives it.
  Use monotonic elapsed time, one evaluation timestamp across a frame, signed
  descending arithmetic, wide intermediates, and specified nearest rounding.
- New commands replace rather than queue the same channel's transition. Retarget
  from its value evaluated at the application timestamp, not its old target.
  Preserve accepted queue ordering and the 16-command-per-frame budget.
- An immediate `/set` cancels an active fade and applies its exact value while
  preserving the existing confirmation. Other channels continue unaffected.
- Zero duration and an equal current/target value finish immediately. Mark
  output completion only after the endpoint frame is successfully submitted;
  that still does not establish the fixture's physical brightness.
- Late frames jump to the correct elapsed-time value rather than replaying ticks;
  exact target is emitted at/after the deadline. No sleep or thread per fade.
- Continue repeated DMX frames while idle and active. Preserve transport timings
  initially; the 12 ms post-write delay is not the full frame period.
- Stop intent stops transitions/output with existing shutdown: discard queued
  input, drain outstanding MQTT work, no blackout or final frame. Do not promise
  physical off state during shutdown.
- Rate-limit/summarize animation-driven change logging rather than printing each
  changed channel every frame; preserve immediate-command observability.
  Agree the logging policy and cadence/timing acceptance thresholds before wiring.

## Implementation sequence and acceptance gates

1. **Finalize implementation contracts.** The JSON/HA direction is selected.
   Resolve the explicitly open configuration/parser choices, input bounds,
   optional-field edge cases, rounding, inverse calibration, remembered
   brightness, state/availability ordering, logging, and timing thresholds.
   Decisions are recorded above; the current user request authorizes implementation.
2. **Calibration and pure transition engine (original phase 4).** Add a small C module under
   `src/` and matching tests under `tests/`, using existing light-state types.
   Fake-time tests cover both curves, start/midpoint/end, ascending/descending,
   rounding, zero/equal values, maximum duration, late ticks, retarget/cancel,
   independent channels, defaults versus explicit zero, remembered brightness,
   calibrated on/off thresholds, and protected slot 0. Test configuration ID
   validation, duplicate IDs/channels, and channel mappings. Quantify overflow bounds.
3. **Protocol integration (part of phase 5).** Extend immutable decoding and
   state formatting; wire opt-in subscriptions, reconnect, and owned messages.
   Test all malformed/boundary inputs, canonical output, target-state updates,
   exact ID lookup, unknown IDs, custom prefixes, both filters, actual HA JSON examples, retained state/
   availability, and unchanged immediate confirmations. Custom command IDs
   and event correlation are not part of the initial wire contract. Verify a
   channel remap changes only hardware routing, not topics or HA identity.
4. **Output integration (remainder of phase 5).** Apply commands and evaluate
   transitions on the owner thread before stable frame construction. Gateway
   fake-clock tests assert exact calibrated frame bytes and state order, successful-write
   completion gating, bounded draining, simultaneous fades, supersession,
   legacy raw overrides (including JSON state reconciliation through the
   channel-to-light mapping), disconnect/publication errors, and shutdown.
   Preserve idle frame output.
5. **MQTT auto-discovery.** Generate retained per-light configurations and shared
   device metadata, subscribe to HA birth, and implement bounded republishing
   and stale-entry cleanup. Tests cover payload schema/escaping/size, stable
   identity across channel/name changes, multiple gateways, custom prefixes,
   startup/reconnect/HA restart, repeated birth messages, publication failures,
   offline removal retries, and retained cleanup after restart. Assert discovery
   traffic does not change outputs, restart fades, or starve frames.
6. **Migration documentation and deterministic verification.** Document gateway
   configuration, automatic discovery, optional manual setup, cleanup and
   entity-ID preservation, scene activation,
   default transitions, and known limitations. Run full Docker regressions and
   production linkage before any hardware use.
7. **Approved operational verification (animation-related phase 6).** In an
   isolated broker/HA environment, verify automatic creation of one light entity
   per configured light with no manual YAML, correct gateway device grouping,
   retained discovery when HA starts later, HA/gateway/broker restart recovery,
   removal and rename cleanup, and absence of duplicate entities. Verify scene activation
   with explicit, omitted, and zero durations, including OFF and mid-fade
   replacement. Test subscriptions, reconnect, availability, and shutdown.
   With explicit device approval, measure actual frame cadence, endpoint timing,
   low-brightness stepping, and compare curves.
   Agree measurable thresholds first; mocks cannot validate perceived smoothness.

Each step depends on the previous gate. The existing ownership/lifecycle
foundation must remain intact; further effects wait until single fades pass.

## Mandatory per-task TDD cycle

Every implementation task in every phase must follow its own
**red -> green -> refactor -> green** cycle. Do not implement a whole phase
first and add tests afterward. Split larger tasks into observable behaviors
and repeat the cycle for each increment, including configuration, calibration,
JSON handling, transitions, routing, state, discovery, and lifecycle changes.

1. **Red:** write the smallest focused acceptance/regression test and run it
   in Docker before implementing the behavior. Observe the intended assertion
   failure and record why it fails. Compilation errors, missing dependencies,
   broken infrastructure, and skipped tests are not valid red evidence.
   Add only the minimal interface scaffolding needed to execute the test.
2. **Green:** implement only what is needed to satisfy that behavior. Rerun
   the same test in Docker and confirm it passes without weakening assertions
   or skipping scenarios. Run the affected regression tests.
3. **Refactor:** explicitly review the now-working code and tests for clarity,
   duplication, and fit with existing helpers. Make justified behavior-preserving
   improvements, then rerun the focused and affected regression tests in Docker.
   They must remain green. If no useful refactor is needed, record that decision
   rather than making artificial changes, and still perform the post-review run.
4. **Task completion gate:** record test names, commands, the intended red
   assertion, green results, refactor changes (or rationale for none), and the
   post-refactor green result. A task is incomplete without this evidence.
   Do not rely on cached success as evidence that a newly changed test ran.

Use fake clocks and mocked adapters for deterministic red/green tests; do not
actuate real fixtures to create a failing test. Cross-module changes additionally
require the full suite and production linkage. Approved HA/broker/hardware
acceptance checks supplement TDD, not replace it.

Planning and documentation-only tasks validate accuracy, links, and structure;
they do not require artificial failing runtime tests. This exception does not
permit implementation changes to bypass the per-task cycle.

## Verification and handoff

Behavioral work uses Docker-only Unity/CMock/Ceedling red-green-refactor.
Observe the intended assertion failure before implementation; use fake time
rather than sleeps. Extend the production Makefile when adding a source module.

```sh
docker version
docker build --target test -t mqtt2dmx-test:local .
docker run --rm --network none mqtt2dmx-test:local test:transition
docker run --rm --network none mqtt2dmx-test:local test:gateway
docker build -t mqtt2dmx:local .
```

`test:transition` is a future selector, usable only after adding its suite.
Rebuild the test image after edits or use the documented mounted-checkout
workflow. Cross-module changes require the full suite and real production link;
the production build includes the full-suite gate. Report fresh versus cached
results, errors, and hardware limitations. No host-toolchain fallback.

## Implementation verification (2026-10-31)

All commands ran inside Docker; host activity was file inspection/editing and
Docker orchestration only. The read-only attached plan was not edited. Existing
staged plan changes were preserved; no files were staged or committed.

Focused invocation, with `SUITE` replaced by the selector below:

```sh
docker build --target test -t mqtt2dmx-test:local .
docker run --rm --network none -v "$PWD:/workspace" mqtt2dmx-test:local test:SUITE
```

| Increment / representative tests | Observed red | Green and review |
| --- | --- | --- |
| `config`: stable ID/defaults; `test_given_light_definition_when_parsed_then_maps_stable_id_and_defaults` | Expected true, got false from minimal parser scaffold | Initial 12 passed; shared strict JSON field/duration helpers extracted, subsequent expanded configuration regressions passed |
| `transition`: integer calibration/template mapping | Expected 66/161/255, got zero | All 256 integer values and fractional threshold verified; 5 passed |
| `transition`: elapsed curves, retarget, raw override, remembered values, submission gate | 11 intended failures including midpoint 63.75 versus zero and raw 10 versus zero | 16 passed, then post-review 16 passed; later descending/equal-target tests added |
| `ha_protocol`: actual HA commands and optional fields | Valid HA payloads rejected by scaffold | 27 passed; strict duplicate/NUL/type/range/unknown-field policy retained |
| `ha_protocol`: exact routing, escaped discovery, stable identity | Route returned -2, discovery false, empty identity | 31 passed; review fixed full-length identity storage and raised discovery bound to 4096 for worst-case escaping; boundary tests added |
| `mqtt`: light filters, will, retention, completion/offline drain | 4 intended assertion failures; 46 existing tests passed | 50 passed. Initial failing-test cleanup cascade and old 64-byte boundary fixtures corrected before recording clean red; no production assertions weakened |
| `ha`: owner application, independent fades/raw reconciliation, endpoint gate | 4 intended failures | 4 passed |
| `ha`: bounded refresh/state/removal scheduling | 8 intended failures, 4 owner tests passed | 12 passed; publication helper shared, one in-flight and one scheduled publication/frame retained |
| `home_gateway`: real owner/MQTT/decoder/engine with mocked external adapters | Calibrated midpoint/endpoint absent; raw reconciliation topic missing | 4 passed; expanded queue, retained replay, failure and shutdown regressions added |
| `dmx`: summarized animation logging | Expected summary absent under old per-channel logging | 26 passed; long-run callback test later verifies exactly two summaries across 257 changing frames without exhausting CMock expectation storage |
| Config migration/self-subscription and retained HA commands | 7 config failures and retained command changed output to 255 | 22 config and 13 HA tests passed; allow shared-state cleanup followed by authoritative republish |
| MQTT disconnect-before-subscribe and offline stop | Unexpected subscription count 1; disconnected client not closed | Included in subsequent passing full production gate |

Review retained the small module boundaries and existing ownership model. No
artificial refactors were added where unnecessary; post-review full regressions
rerun every focused test. New input/state never depends on broker-delivered
retained snapshots. Memory is bounded by validated input/config sizes, fixed
light/queue/manifest storage and serialized new-interface publications.

Full production validation:

```sh
docker build -t mqtt2dmx:local .
```

The first production gate found CMock expectation-pool exhaustion in the new
257-frame logging test. It was corrected with a constant-memory mock callback
that still checks all 257 frame bytes/counts and the exact two-summary threshold.
The next production build freshly executed 311 tests, all passing with none
ignored, linked against real Paho/FTDI/Jansson, and produced the runtime image.
Dependency layers were cached where applicable; that test run was not cached.
Final expanded-suite validation freshly executed **315 tests: 315 passed, zero
failed or ignored**, including example-file validation, maximum manifest/ID
bounds and real shutdown/offline integration. The final `docker build -t
mqtt2dmx:local .` passed; unchanged production compile/runtime layers reused
the preceding successful real-library build. Runtime `ldd /bin/mqtt2dmx` in a
network-disabled container resolved Paho, FTDI, Jansson, libc and libusb, with
no Ruby executable or test tree in the runtime image. The gateway itself was
not started. All local links in the README, test documentation and this plan
resolved, and `git diff --check` passed.

Software gates 1-6 are implemented and verified. Configuration and migration
instructions, manual HA fallback, scene activation, rounding/inverse behavior,
memory limits, error policy and operational limitations are documented in
[README.md](../README.md); [lights.example.json](../lights.example.json) is
validated by the configuration test suite.

Operational gate 7 is **not executed**: no approved broker/HA endpoint or USB
device was supplied. No real broker connection, HA entity creation/removal,
physical output, actual cadence or perceived smoothness is claimed. Next
operational action: agree isolated broker/HA/device access and measurable fixture
timing/visual thresholds, then run the restart/migration/scene checklist above.
Implementation does not add RGB, groups, gateway scenes, sequences, gamma,
dithering, channels 256-512, live reload or unrelated dependency upgrades.

## Transition overwrite verification (2026-10-31)

Follow-up scope: verify that a new transition replaces an active longer one,
adding a red regression only if the behavior is missing. Production code already
retargets at application time; no behavior change or red failure was needed.

Added two integration cases in `tests/integration/test_home_gateway.c`, covering
linear and smoothstep curves through the real MQTT queue, decoder, application,
and transition engine with mocked Paho, clock, and DMX. Acceptance: replace a
10-second ON fade after 2.5 seconds with a 1-second OFF fade, preserve the current
output at replacement, follow the new midpoint and deadline, and remain OFF at
and after the original deadline. Both cases passed against unchanged production
code. README wording now explicitly describes replacement rather than queuing.

Verification:

- `docker build --target test -t mqtt2dmx-test:local .`: succeeded using cached
  test environment layers; this command alone did not execute tests.
- `docker run --rm --network none mqtt2dmx-test:local test:transition`:
  freshly executed 19 tests, all passing.
- `docker run --rm --network none -v "$PWD:/workspace" mqtt2dmx-test:local test:home_gateway`:
  freshly executed 12 tests, all passing, including both new cases.
- `docker build -t mqtt2dmx:local .`: freshly executed all 317 tests, all passing,
  none ignored; production image succeeded with unchanged native build/runtime
  layers cached.

No broker or hardware execution was performed. No software blocker remains for
transition replacement; any observed deployment failure needs a concrete command
sequence and deployed image identification before a failing regression can be
claimed.

## References

- [Home Assistant MQTT JSON lights](https://www.home-assistant.io/integrations/light.mqtt/#json-schema)
- [Home Assistant MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
- [Home Assistant scene activation and transition](https://www.home-assistant.io/actions/scene.turn_on/)
- [Repository test workflow](../tests/README.md)
