---
name: release
description: "Use when asked to make a release, publish mqtt2dmx, or build and push a Docker image to Docker Hub. Publish mrtncls/mqtt2dmx with a UTC YYYY-MM-DD.N tag and always update latest to the same image."
---

# Release to Docker Hub

## Contract

- Publish only to `docker.io/mrtncls/mqtt2dmx`.
- Use `YYYY-MM-DD.N`, with the date taken from UTC at release start and `N`
  starting at 1 each day. For example, `2026-10-31.1`.
- Choose one more than the highest existing numeric suffix for that date on
  Docker Hub, not the number of tags or the latest local tag.
- Never overwrite an existing dated tag. Always publish `latest` as well.
- Creating or editing this skill does not authorize publishing a release.
  Run this workflow only when the user requests a release.

## 1. Preflight

1. Read the [repository instructions](../../copilot-instructions.md) and invoke
   the [Docker TDD skill](../docker-tdd/SKILL.md). Inspect the current
   [Dockerfile](../../../Dockerfile) and [test documentation](../../../tests/README.md).
2. Run from the repository root. Check `git status --short` and
   `git rev-parse HEAD`. If there are uncommitted or untracked release inputs,
   ask whether to include them before publishing. Do not commit, stash, or
   discard changes automatically. Keep the build context unchanged throughout.
3. Check `docker version` and `docker buildx version`; Buildx is used for remote
   manifest verification. Stop and report unavailable tools or daemon access.
   Never compile or test on the host as a fallback.
4. Use existing Docker Hub authentication. If authentication is missing or
   denied, ask the user to authenticate with `docker login` and retry. Never
   read or print Docker credentials, put tokens in commands or files, or ask
   the user to paste a secret into chat.
5. Use the existing Dockerfile's default platform unless the user requests
   another platform. Report the platform; do not imply a multi-platform release.
   Coordinate releases so only one publisher uses this repository at a time:
   tag discovery and pushes are not atomic and cannot prevent concurrent races.

## 2. Select the dated tag

1. Capture the date once with `date -u +%F`.
2. Retrieve Docker Hub tags from
   `https://hub.docker.com/v2/repositories/mrtncls/mqtt2dmx/tags?page_size=100`.
   Parse the JSON and follow every `next` page until it is null. Use an
   authenticated supported API client if necessary. Only follow pagination
   URLs on Docker Hub; never forward credentials to an unexpected host.
3. Select names matching exactly `<captured-date>.[1-9][0-9]*`, with a literal
   dot. Compare suffixes as integers, not strings. Use the maximum plus one,
   or 1 only when a successful, complete listing has no matches.
   Example: suffixes `1`, `3`, and `10` produce `11`, not `4` or `2`.
4. Stop on HTTP, authentication, rate-limit, pagination, or JSON errors.
   An unsuccessful or incomplete listing is never an empty tag list.
5. Record the chosen tag, source revision, worktree state, and target platform.
   Reuse the captured date even if the build crosses UTC midnight.

## 3. Build and publish

The following commands are templates. Replace `<release-tag>` with the selected
tag and `<image-id>` with the actual image ID; never run literal placeholders.
Persist these values between tool calls explicitly rather than assuming shell
variables survive.

1. Build the production runtime image once:

   ```sh
   docker build --target runtime -t docker.io/mrtncls/mqtt2dmx:<release-tag> .
   docker image inspect --format '{{.Id}} {{.Os}}/{{.Architecture}}' docker.io/mrtncls/mqtt2dmx:<release-tag>
   ```

   The production build depends on `test-results`, which runs the full suite.
   Do not bypass that gate or publish a test/build-stage image. Report whether
   tests ran freshly or reused cached passing results. Stop on build/test failure;
   never push an older local image after a failed build.

2. Repeat the complete remote tag lookup immediately before pushing. If the
   selected tag now exists or a higher suffix has appeared, choose the new
   maximum plus one and tag the already-built image ID with that new dated tag.
   Do not rebuild or overwrite the occupied tag. Stop if another publisher is
   active until release ordering is coordinated.
3. Push the dated tag first:

   ```sh
   docker push docker.io/mrtncls/mqtt2dmx:<release-tag>
   docker buildx imagetools inspect docker.io/mrtncls/mqtt2dmx:<release-tag>
   ```

   Record the pushed digest and confirm the remote digest matches. On failure,
   stop without modifying `latest`.

4. Tag the exact same built image ID as `latest`, then push and verify:

   ```sh
   docker tag <image-id> docker.io/mrtncls/mqtt2dmx:latest
   docker push docker.io/mrtncls/mqtt2dmx:latest
   docker buildx imagetools inspect docker.io/mrtncls/mqtt2dmx:latest
   docker buildx imagetools inspect docker.io/mrtncls/mqtt2dmx:<release-tag>
   ```

   Both remote digests must equal the recorded pushed digest. A local tag or a
   successful build alone is not proof of publication.

## 4. Completion and failures

- Report success only after both remote tags resolve to the same built image.
  Include both full image references, the digest, platform, source revision,
  exact verification commands, and fresh/cached test status.
- If the dated push succeeds but the `latest` push or verification fails, report
  a partial release explicitly. Preserve the dated tag and retry only the
  failed step using the same image after resolving the error. Do not allocate
  another release number, delete a published tag, or claim success.
- If verification suggests a concurrent publisher changed a tag, stop and ask
  for coordination rather than blindly overwriting its work.
- Do not run the application against hardware or a broker for release
  verification. Do not prune shared Docker resources or create Git tags,
  commits, or GitHub releases unless separately requested.
