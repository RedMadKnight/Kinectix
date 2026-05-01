# Kinectix

**A fork of [xenia-canary](https://github.com/xenia-canary/xenia-canary) focused on Kinect / NUI peripheral emulation for Xbox 360 titles.**

> **Not affiliated with the Xenia project.** Kinectix is an independent fork released under the same BSD 3-Clause license as the upstream. The Xenia name and logo are not used in branding; the upstream binary name (`xenia.exe`) is preserved inside the build for compatibility but releases are distributed under the Kinectix name.

## Why this fork exists

Kinect support has been requested in upstream Xenia since 2018 (issues #1241, #2302, #2339; xenia-canary #537). The upstream project has explicitly chosen not to prioritize peripheral emulation — the [FAQ](https://github.com/xenia-project/xenia/wiki/FAQ) lists Kinect alongside mice and microphones as out-of-scope. Multiple maintainers have stated that the blocker is volunteer bandwidth and ongoing maintenance burden, not a hard technical problem.

Kinectix takes ownership of that scope. We track xenia-canary upstream weekly, contribute non-NUI bug fixes back where possible, and develop the NUI emulation layer here as first-class scope.

## Scope

**In scope:**

- XAM NUI HAL (`xam_nui.cc`) implementation for Xbox 360 Kinect (Kinect v1)
- Skeleton tracking pass-through and recorded playback
- libfreenect / libfreenect2 integration as optional backends
- Webcam + ML pose estimation as a fallback backend
- Tooling: skeleton recorder, player, fixture format

**Out of scope (for now):**

- XAUDIO2 microphone array (Kinect microphone) — separate effort
- Kinect Sensor v2 / Azure Kinect SDK paths beyond v2 USB 3.0 adapter
- VR tracker (SlimeVR / Vive) skeleton synthesis
- Any non-NUI feature work — those go to xenia-canary, not here

## Status

| Stage | Description | Status |
|------|-------------|--------|
| 0 | Project scaffolding (docs, CI, branch model, weekly canary-sync) | done |
| 1 | `INuiBackend` interface + null backend wired into CMake | done |
| 2 | XAM NUI telemetry tracer (`--nui_telemetry`, all 28 exports) | done — tag `v0.0.2-telemetry` |
| 2.5 | Bootstrap unblock: `XN_SYS_NUI_HARDWARESTATUSCHANGED` broadcast + `XamUnk2B001` stub | done |
| 3 | Recorded backend (`.xnuirec` files via flatbuffers) | deprioritized — real Kinect available |
| 4 M1.5 | Hardware sanity: `freenect-camtest.exe` reads frames from Kinect 1473 via libusbK | done |
| 4 M2 | Backend scaffold: `third_party/libfreenect` submodule + `src/xenia/hid/nui/freenect/` | in progress |
| 4 M3 | Real depth + color frame capture, threaded reader | pending |
| 4 M4 | Fake T-pose skeleton stub via `XamNuiSkeletonGet*` | pending |
| 4 M5 | Notification broadcast: `kXNotificationSystemNUISkeletonTrackingStatusChanged` | pending |
| 4 M6 | First end-to-end: Kinect Adventures clears "is anybody there?" → main menu | pending |
| 5 | Real skeleton tracking (NiTE2 / MediaPipe Pose / custom ML) — decision post-M6 | pending |

See [ROADMAP.md](ROADMAP.md) for stage detail.

## Reference titles

Validation targets, in priority order:

1. **Kinect Adventures** — flagship bundled title, simplest skeleton interactions
2. **Fruit Ninja Kinect** — minimal API surface, gesture-driven
3. **Kinect Sports** — multiplayer, broader skeleton coverage
4. **Dance Central** — high-fidelity tracking, stress test for skeleton smoothing
5. **Kinect Fun Lab** — known to exercise edge-case NUI calls

## Branch model

- `canary` — mirror of `xenia-canary/xenia-canary`'s `canary_experimental` branch, updated weekly by [`canary-sync.yml`](.github/workflows/canary-sync.yml). Never commit here directly.
- `main` — integration branch. Rebased on `canary` weekly. All code review lands here.
- `feature/nui-*` — working branches for in-progress NUI work, PR'd to `main`.
- `release/*` — tagged release branches with pre-built binaries.

**Rule:** any commit touching files outside `src/xenia/hid/nui/`, `src/xenia/kernel/xam/xam_nui.cc`, `.github/`, and top-level docs must be a bug fix. Bug fixes get cherry-picked into a PR against `xenia-canary` upstream within a week. We don't carry private divergence; it is rebase poison.

## Hardware & driver requirements

Required to actually exercise the NUI emulation path with real hardware (M1.5+):

- **Kinect for Xbox 360, model 1473** (the bottom-of-base sticker number). Model 1414 has a known USB enumeration loop on Windows 11 (LED blinks continuously, suspected xHCI quirk) — skip 1414.
- **USB power adapter** for the Kinect — the Xbox 360 console connector does not work standalone.
- **Windows 11 host with libusbK driver bound via [Zadig](https://zadig.akeo.ie/)**. Microsoft Kinect SDK 1.8 is not compatible with Windows 11 (`kinectcamera.sys`'s WDF coinstaller refuses to load — Code 39 + "Bad Image"). We use libfreenect over libusbK instead. Trade-off: lose MS skeleton tracking out-of-the-box; we provide our own (Stage 5).
- **Visual Studio 2026** with Desktop development with C++ workload, **CMake 3.20+**, **clang-format 20.1.7** (CI is strict on this exact version).

A `none` (null) backend builds and runs without any of the above — useful for telemetry capture (Stage 2) and for working on non-hardware code paths.

## Building

xenia-canary uses **CMake** (not premake — the upstream `xb` premake wrapper is being phased out).

```cmd
git clone --recurse-submodules https://github.com/RedMadKnight/Kinectix
cd Kinectix
mkdir build
cd build
cmake .. -G "Visual Studio 18 2026" -A x64
cmake --build . --config Release
```

See [BUILDING.md](BUILDING.md) for full setup, including `--nui_telemetry` capture instructions and how to run the trace parser in `tools/nui-trace/`.

NUI backend is selected at runtime via the `--nui_backend` cvar:

```
--nui_backend=none      (default — null backend, no Kinect input)
--nui_backend=recorded  (replay a .xnuirec fixture, Stage 3 deprioritized)
--nui_backend=freenect  (libfreenect / Kinect v1, Stage 4)
--nui_backend=freenect2 (Kinect v2, future)
--nui_backend=mediapipe (webcam pose estimation, Stage 5 candidate)
```

There are no compile-time backend flags. All vendored dependencies (libfreenect, libusb) are git submodules under `third_party/` and build automatically.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The short version:

- Stay inside `src/xenia/hid/nui/`, `src/xenia/kernel/xam/xam_nui.cc`, and tooling under `tools/nui-trace/` unless you're fixing a bug we'll cherry-pick upstream.
- Prefix NUI commits with `[nui]`; infra/build commits with `[infra]`.
- Do not use Xenia branding in PRs, screenshots, or release artifacts.
- Run `clang-format --style=file -n -Werror <files>` (version **20.1.7** specifically) before pushing — CI lint is strict.
- For behavioral changes, attach either a captured trace from `tools/nui-trace/parser.py` (showing the affected XAM NUI calls) or, where applicable, before/after screenshots from a reference title.

## License

BSD 3-Clause, inherited from xenia-canary. See [LICENSE](LICENSE).

## Trademark notice

"Xbox", "Kinect", and "Xbox 360" are trademarks of Microsoft Corporation. "Xenia" is the name of an independent emulator project; Kinectix is a fork and is not endorsed by, affiliated with, or sponsored by the Xenia project or Microsoft. Kinectix does not distribute Microsoft software or game data.
