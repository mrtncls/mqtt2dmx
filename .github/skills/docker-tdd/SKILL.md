---
name: docker-tdd
description: "Use when coding, debugging, building, testing, mocking, or running mqtt2dmx. Follow Docker-only execution and red-green-refactor TDD, and report missing test infrastructure."
---

# Docker-only development and TDD

## Start with the actual repository

1. Read the [repository instructions](../../copilot-instructions.md) and relevant
   plans using the [memory guide](../../../.agent-memory/README.md).
2. Inspect the current [Dockerfile](../../../Dockerfile),
   [Makefile](../../../Makefile), and any test configuration. Confirm the requested
   scope and acceptance criteria before changing behavior.
3. Check Docker availability with `docker version`. If Docker is unavailable or
   permission is denied, report the error and stop execution. Do not switch to a
   host toolchain or change host permissions.

All compilation, dependency installation, test generation/execution, and
application execution must occur in Docker. Do not install GCC, Ruby, Ceedling,
or native dependencies on the host.

## Current build commands

Run from the repository root:

```sh
docker build -t mqtt2dmx:local .
```

This requires the full test suite to pass, builds the application, and produces
the runtime image. Unchanged inputs may reuse cached passing test results; add
`--no-cache` to force a fresh run. To build the application stage without
packaging the runtime image (also gated on the full suite):

```sh
docker build --target build -t mqtt2dmx-build:local .
```

The Dockerfile invokes `make` inside the image. Do not run `make` on the host.
`make run` builds and starts the application; it still requires Docker and explicit
broker/device approval. Use the Docker build commands above for build-only work.
Both commands consume the `test-results` stage, which runs the full suite via
`ruby tests/run.rb test:all` and provides a success marker only after the runner
passes. The runtime image does not include Ruby, Ceedling, mocks, or test reports.

The existing image uses an old Alpine base and fetches Paho during its build.
If that build fails, preserve the actual error in the handoff. Do not silently
upgrade dependencies or change the Dockerfile outside the approved scope.

## Configured testing and mocking stack

- [Unity](https://www.throwtheswitch.org/unity): assertions and unit tests in C.
- [CMock](https://www.throwtheswitch.org/cmock): generate mocks from C function
  declarations, with argument expectations, return values, and callbacks.
- [Ceedling](https://www.throwtheswitch.org/ceedling): orchestrate test discovery,
  mock generation, compilation, execution, and reporting.

This stack supports the C code without introducing C++ tests. Its Ruby-based
tooling belongs in the Docker test environment, not on the host.

Prefer the results target for a full-suite check:

```sh
docker build --target test-results .
```

Tests execute during the build; no separate `docker run` is needed. This target
does not build the production application or runtime image. Normal production
builds already consume it, so they do not need a separate full-suite invocation.
Unchanged inputs may reuse cached passing results. To force fresh execution:

```sh
docker build --no-cache --target test-results .
```

This also rebuilds the test environment's dependency layers. Report whether
tests actually executed or Docker reused cached passing results.

For focused suites or mounted-checkout testing, build the standalone test
environment from the same Dockerfile:

```sh
docker build --target test -t mqtt2dmx-test:local .
docker run --rm --network none mqtt2dmx-test:local test:mqtt2dmx
```

The `test` target prepares the environment without executing tests. Supported
selectors are `test:all`, `test:mqtt2dmx`, `test:mqtt`, `test:dmx`, `test:gateway`,
and `test:harness`; pass one per invocation. Rebuild after editing sources or tests,
or mount the checkout:

```sh
docker run --rm --network none -v "$PWD:/workspace" mqtt2dmx-test:local test:all
```

The runner cleans generated output, propagates Ceedling failures, and rejects
missing reports or failed, ignored, or zero executed tests. Building `test-results`
keeps reports inside the build stage, not in the checkout. Mounted output stays
under ignored `build/`; do not run simultaneous tests on the same mounted checkout.
See [test documentation](../../../tests/README.md) for pinned versions, coverage,
and known limitations.

Preserve the harness's isolation: generate mocks as artifacts, mock dependencies
rather than the unit under test, and keep delays and callbacks deterministic.
The compiler wrapper separates the application entry point from Unity's runner
without test-only production directives. No broker or USB device is required.

## Red-green-refactor

1. Translate the requested behavior into an observable acceptance test; for a
   bug, write a regression test that reproduces it.
2. Add the smallest focused unit test and mock expectations. Run it in Docker
   against unchanged production behavior and confirm the expected assertion
   failure. Resolve infrastructure or compilation errors before calling it red.
3. Implement only what is needed to pass. Rerun the focused test in Docker.
4. Refactor without changing behavior; rerun the focused and relevant regression
   tests in Docker. Expand to the full suite for shared/cross-module changes or
   when the affected scope cannot be confidently bounded.
5. Build the production image using the current Dockerfile. Passing mocked unit
   tests does not guarantee the production binary links with real dependencies.
6. Record commands, actual results, and limitations. Do not claim completion if
   required tests or the production build failed or could not run.

Documentation-only changes require document validation, not an artificial
red-green cycle or application execution.

## Running the application

Runtime is an integration/hardware operation, not a unit test. Read the
[usage documentation](../../../README.md) and obtain explicit approval for the
broker endpoint and FTDI device before connecting. Do not use a real broker or
actuate connected equipment merely to validate an instruction change.

After approval and a successful image build, the following is a template, not a
command to execute with literal placeholders:

```sh
docker run --rm \
  --device /dev/bus/usb/<bus>/<device> \
  -e MQTT_ADDRESS='<approved-broker-address>' \
  mqtt2dmx:local
```

Use the actual approved USB device path and broker address. `MQTT_ADDRESS` is
required; `MQTT_CLIENTID` defaults to `mqtt2dmx` and `MQTT_TOPIC` defaults to
`dmx/channels/+/set`. Explicitly agree on a safe test topic/client ID when sharing
a broker.

Prefer specific device access over the README's broad privileged example.
Confirm that the selected mapping works for the device; if additional access is
needed, report the error and request approval rather than adding `--privileged`
or exposing all USB devices automatically. Without hardware or broker approval,
report runtime verification as not performed.

Use task-owned containers and inspect logs to verify startup rather than assuming
a running process means the integration works. Stop only containers started for
the task when finished. Do not prune shared containers, images, or volumes.

## Handoff

Update the relevant plan with concise verification results and blockers. Report
the red/green test evidence, regression scope, production build outcome, and
whether approved runtime verification occurred. Keep secrets and raw logs out of
plan files; never turn missing verification into a success claim.
