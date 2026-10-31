# Repository agent instructions

## Project context

This is a C MQTT-to-FTDI-DMX gateway. Production sources and internal headers are
in `src/`: `mqtt2dmx.c` handles messages and application lifecycle, `mqtt.c` wraps
Paho MQTT, and `dmx.c` drives FTDI hardware. Tests and their helpers live in `tests/`.
Follow the surrounding C style and preserve behavior outside the requested scope.
See [README.md](../README.md) for application configuration and usage.

## Code style

- Avoid source-level conditional compilation directives when practical, especially
  test-only `#ifdef` branches in production code. Prefer build configuration or
  ordinary C interfaces for test isolation. Keep necessary includes, header guards,
  and platform-specific directives when they are justified.

## Planning and scope

- Before substantial work, read relevant plans in `.agent-memory/`, check their
  assumptions against the current repository, and record scope and acceptance
  criteria. Follow the [memory guide](../.agent-memory/README.md).
- Keep changes focused. Do not reorganize sources, add dependencies, change build
  infrastructure, or fix unrelated issues without authorization.
- Preserve existing user changes. Never commit unless requested; include saved
  plans in commits only when explicitly requested.

## Mandatory development workflow

- **Always build, test, and run the application inside Docker.** Do not invoke
  host compilers, `make`, Ceedling, or the application, or install host toolchains
  as a fallback. File inspection and editing may happen on the host.
- **Always use TDD for behavioral code changes:** write a focused test first,
  observe its intended assertion failure in Docker, implement the smallest fix,
  rerun to green, then refactor and rerun relevant regression tests.
- Compilation, dependency, and infrastructure failures are not a valid red phase.
  Never replace a failing test with weaker assertions to obtain a green result.
- Name tests `test_given_<context>_when_<action>_then_<outcome>` and separate
  their bodies with `Given (Arrange)`, `When (Act)`, and `Then (Assert)` comments.
  Keep setup and mock expectations in Arrange, the behavior under test explicit
  in Act, and outcome assertions and mock verification in Assert. Setup helpers
  must not hide the action under test. Split independent scenarios into separate
  tests; callbacks may assert transient arguments during Act when necessary.
  See [test conventions](../tests/README.md#test-structure).
- Use the [Docker TDD skill](skills/docker-tdd/SKILL.md) when coding, debugging,
  building, testing, or running this project.
- The configured test stack is **Unity + CMock + Ceedling**, using `project.yml`
  and `tests/run.rb`. Prefer `docker build --target test-results .` for the full
  suite: no separate `docker run` or production build is needed. Normal production
  builds already require this stage. Unchanged inputs may reuse cached passing
  results; `docker build --no-cache --target test-results .` forces fresh execution
  and rebuilds dependency layers. Use the `test` target plus `docker run` only
  for focused suites or mounted-checkout testing; building `test` alone does not
  execute tests. The runner rejects failed, ignored, or zero executed tests.
  See [test documentation](../tests/README.md) for coverage and limitations.
  Do not invent passing results.
- Hardware or broker-connected execution requires explicit approval. Unit tests
  must not need physical USB hardware, a live broker, or privileged containers.
- Report exact verification commands, outcomes, and remaining blockers. A
  successful `test` environment build is not proof that tests passed. For
  `test-results` and production builds, distinguish fresh execution from cached
  passing results.
- Documentation/instruction-only changes do not need artificial tests or an
  application build. Validate their structure, links, and accuracy instead.
