# Contributing to Kinectix

## Scope rules — read first

**The single most important rule:** if your change touches files outside this allowlist, the PR will be closed and asked to be redirected to xenia-canary upstream:

- `src/xenia/hid/nui/**`
- `src/xenia/kernel/xam/xam_nui.cc`
- `src/xenia/kernel/xam/xam_nui.h`
- `src/xenia/kernel/xam/apps/xam_app.cc` — only the NUI-related case stubs (e.g. `XamUnk2B001`)
- `third_party/libfreenect/**` (submodule pointer + our wrapper rules)
- `third_party/freenect-msvc-compat/**` (POSIX shim for libfreenect on MSVC)
- `.github/**`
- top-level `*.md` (docs)
- `tools/nui-trace/**` (telemetry parser)
- CMake additions for NUI targets only (`add_subdirectory(hid/nui)`, `target_link_libraries(... xenia-hid-nui)`, the inline `freenect` STATIC target in `third_party/CMakeLists.txt`)

Anything else — GPU fixes, kernel bugs, audio, controller — belongs in [xenia-canary](https://github.com/xenia-canary/xenia-canary). We will happily help you draft that PR, but Kinectix does not carry private divergence outside its scope. Every byte of drift is rebase pain when we sync from upstream.

If you have a non-NUI bug fix that you've found while testing Kinectix, the workflow is:

1. Open the PR against xenia-canary first.
2. Once it's merged (or even queued for review), open a Kinectix issue linking it. We'll cherry-pick it locally so users don't have to wait, and it'll naturally come through on the next upstream sync.

## Commit message convention

All commits whose primary purpose is NUI-related must be prefixed:

```
[nui] Add INuiBackend interface and NuiManager skeleton

Introduces the abstract backend interface used by xam_nui.cc to fetch
skeleton/depth/color frames. Provides the NuiManager singleton and a
no-op default backend.

Refs: #12
```

Bug fixes destined for upstream use the prefix the upstream uses (no `[nui]`):

```
GPU: Fix incorrect tile size in resolve path
```

## Branch model

- `canary` — automated mirror of `xenia-canary/xenia-canary`'s `canary_experimental` branch. Do not push directly.
- `main` — protected (linear history, lint+build status checks required). PRs only. Rebased on `canary` weekly by `.github/workflows/canary-sync.yml`.
- Working branches — name freely (e.g. `stage4-m2-libfreenect-vendor`, `nui-fix-skeleton-broadcast`). PR'd to `main`.

Note: the `upstream` name in this repo refers to the **remote** pointing at `xenia-canary/xenia-canary` (used for `git fetch upstream`), not a branch. The mirror branch is `canary`. **Never name a local branch `upstream`** — it conflicts with the remote namespace.

If you fork from `main` and the weekly canary rebase happens before your PR merges, you'll need to rebase your branch. This is normal. Keep features small to minimize the pain.

## Code style

We inherit xenia's style guide verbatim. Run **clang-format version 20.1.7 specifically** with the project's `.clang-format` before opening a PR — CI's lint job rejects any other version's output, even patch-level differences:

```cmd
pip install clang-format==20.1.7 --break-system-packages
clang-format --style=file -n -Werror <changed-files>
clang-format --style=file -i <changed-files>   # auto-fix
```

PRs that don't format-clean will get a CI red, not a close — fix and push.

## Tests

Stage 4 is feature-development phase, and our test infrastructure is still being built up. For now, behavioral changes must demonstrate correctness via at least one of:

- **Telemetry trace** captured via `--nui_telemetry`, parsed with `tools/nui-trace/parser.py`, attached to the PR. Required for any change that affects which XAM NUI exports get called or in what order (`xam_nui.cc`, `NuiManager`, backend implementations of `Poll*`).
- **Reference-title screenshot or short video** demonstrating the change in a Kinect title (Kinect Adventures bootstrap is the canonical smoke test). Required for any change with visible UI/runtime effect.
- **Unit test** under `src/xenia/hid/nui/tests/` (directory not yet populated — first contributor here is welcome to seed it). Required for self-contained logic like joint mapping math.

The recorded backend (Stage 3) was our originally planned regression oracle — that approach is deprioritized while we focus on real Kinect support. CI will gain a fixture replay suite post-Stage-4 if useful.

If your change touches the libfreenect backend (Stage 4+), additionally:

1. Document the manual test in the PR description (e.g. "ran with Kinect 1473 + libusbK driver against Kinect Adventures, observed `freenect-camtest.exe`-equivalent depth+color stream").
2. Mention any deviation from the canonical hardware setup documented in [README.md § Hardware & driver requirements](README.md).

## Bringing in a new backend

If you want to add a new `INuiBackend` (say, OpenXR body tracking, or VR trackers):

1. Open a discussion issue first. Backend additions expand our maintenance surface; we want to agree on it before code.
2. The backend must be selectable via `--nui_backend=<your-backend>` cvar — runtime, not compile-time. Inactive backends are dead code in the binary but contribute negligibly to size; we deliberately avoid `KINECTIX_NUI_*` build flags so contributors get a uniform build.
3. The default `--nui_backend=none` (null backend) must keep working — backend init failure must not break non-Kinect titles.
4. The backend cannot pull in dependencies that change xenia-canary's build assumptions (no new global LDFLAGS, no replacing CRTs, etc.). Vendor heavy deps as `third_party/<lib>` submodules and inline-wrap them in `third_party/CMakeLists.txt`, following the libfreenect pattern.

## Branding and trademarks

- Do not use the Xenia name or logo in PRs, screenshots, or branches. Use "Kinectix" or "this fork".
- Do not refer to the project as "Xenia with Kinect" in commit messages, issue titles, or release notes.
- Do not link to upstream Xenia issues from Kinectix marketing material in a way that suggests endorsement.
- If a maintainer of upstream Xenia asks us to change something, default to yes. We are guests in their ecosystem.

## Code of conduct

Be kind. We are working on motion control emulation for fifteen-year-old games. Take it seriously, but not too seriously.

## Who reviews what

- Backend changes (`hid/nui/recorded`, `hid/nui/freenect*`, `hid/nui/mediapipe`): any maintainer.
- `xam_nui.cc` and the `INuiBackend` interface itself: requires two maintainer LGTMs because contract changes propagate.
- `.github/workflows/canary-sync.yml` and other CI: requires a maintainer LGTM and a known-good run on a fork before merge.

## Release process

(TODO once we cut v0.1.) Sketch:

1. Tag from `main` after a passing CI run with all NUI fixtures green.
2. CI builds Windows and Linux binaries.
3. GitHub Release with changelog (auto-generated from `[nui]` prefixed commits since last tag).
4. Binaries named `kinectix-<version>-<platform>.zip`, never `xenia-*`.
