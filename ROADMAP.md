# Roadmap

A staged plan from "fork exists, nothing works" to "shipped Kinect support". This document is updated as stages land — see [README.md § Status](README.md#status) for the running status table.

## Stage 0 — Project scaffolding ✅ DONE

Docs (README, ARCHITECTURE, ROADMAP, CONTRIBUTING, STRUCTURE, BUILDING), CI workflows (lint, build-windows, canary-sync, orchestrator), branch model (`main` ↔ `canary` mirror), repo on GitHub at [RedMadKnight/Kinectix](https://github.com/RedMadKnight/Kinectix). Branch protection on `main`: PR + status checks + linear history.

## Stage 1 — INuiBackend wiring ✅ DONE

Landed `INuiBackend` interface (`nui_backend.h`), `NuiManager` singleton, null backend (`hid/nui/null/`), recorded backend stub (`hid/nui/recorded/` — `XnuirecReader::Open()` always returns false, kept compiled for ABI completeness). Wired `xenia-hid-nui` library into `xenia-app` via `src/xenia/CMakeLists.txt` and `src/xenia/app/CMakeLists.txt`. Default `--nui_backend=none` produces a binary functionally identical to upstream xenia-canary at default settings.

## Stage 2 — XAM NUI telemetry ✅ DONE — tag `v0.0.2-telemetry`

Patched `src/xenia/kernel/xam/xam_nui.cc` with `XE_NUI_TRACE` macro on all 28 XAM NUI exports. Cvar `--nui_telemetry` (default off) gates the trace; cost when off is one global-bool load + branch (predicted not-taken via `XE_UNLIKELY`). Trace lines emit through `XELOGI` as `i> [nui] FunctionName(arg=value, …)`. Parser tool at `tools/nui-trace/parser.py` analyzes captured logs (summary, init sequence, A/B diff, mermaid sequence diagram).

Empirical findings against Kinect Adventures: titles call only `XamNuiGetDeviceStatus` (1×) + `XamShowNuiTroubleshooterUI` (per X button press) at the bootstrap screen. The "is anybody there?" gate enforces `XEX_SYSTEM_SKELETAL_TRACKING_REQUIRED` — no controller fallback for these titles, real (or simulated) skeleton tracking is mandatory.

## Stage 2.5 — Bootstrap unblock ✅ DONE — PR #5

Two missing kernel-side calls that Kinect Adventures gates on:

- Broadcast `XN_SYS_NUI_HARDWARESTATUSCHANGED` notification on first `XamNuiGetDeviceStatus`. Without it, the title never refreshes its sensor-presence cache.
- Stub `XamUnk2B001` (XAM ordinal `0x0002B001`) in `src/xenia/kernel/xam/apps/xam_app.cc`. Title checks return value but tolerates simple success.

Side-finding: `XamVoiceSetMicArrayIdleUsers`/`MuteMicArray`/`GetMicArrayUnderrunStatus`/`GetMicArrayAudioEx`/`DisableMicArray` (ordinals 0x48C–0x491) have entries in `xam_table.inc` but no `DECLARE_XAM_EXPORT1` declarations in `xam_voice.cc`. Title gracefully ignores NULL return — does not block. Side-PR for stubs pending.

## Stage 3 — Recorded backend ⏸ DEPRIORITIZED

Originally planned as our deterministic regression oracle: capture a `.xnuirec` fixture once, replay through `RecordedBackend` for CI tests. Deprioritized once we confirmed real Kinect hardware (1473) is functional locally, making real-time depth+color development feasible. The `xnuirec.fbs` schema and `XnuirecReader` skeleton remain in tree for potential CI fixture replay post-Stage-4. Pick this back up if/when CI gains the ability to mock USB hardware end-to-end.

## Stage 4 — libfreenect backend (Kinect v1)

End-to-end goal: Kinect Adventures clears the "is anybody there?" gate and reaches the main menu, exercising the full XAM NUI ↔ `INuiBackend` ↔ libfreenect ↔ libusb path.

### M1.5 — Hardware sanity ✅ DONE 2026-05-01

`freenect-camtest.exe` reads depth + color frames from Kinect 1473 via libusbK driver (Zadig-installed). Confirmed: Microsoft Kinect SDK 1.8 is incompatible with Windows 11 (`kinectcamera.sys` Code 39 + Bad Image — KMDF coinstaller mismatch). Pivoted to libfreenect over libusbK as the only viable path. Trade-off accepted: lose MS skeleton tracking out-of-the-box; we provide our own (Stage 5).

Hardware: Kinect 1473 stable; 1414 has USB enumeration loop on Win 11 — skip 1414.

### M2 — Backend scaffold 🚧 in progress

Vendor libfreenect at commit `09a1f09` as `third_party/libfreenect` git submodule. Inline `freenect` STATIC target in `third_party/CMakeLists.txt` mirroring upstream `src/CMakeLists.txt:23` SRC list (8 files); links against existing vendored `libusb` (no vcpkg dependency). MSVC POSIX shim at `third_party/freenect-msvc-compat/unistd.h` (usleep/sleep → Sleep). Backend module `src/xenia/hid/nui/freenect/{nui_freenect.h,.cc,CMakeLists.txt}` with minimal `Setup()/Shutdown()`. Wire `freenect` arm in `NuiManager`'s `--nui_backend` switch.

### M3 — Frame capture ⏳ pending

Threaded reader: dedicated `std::thread` running `freenect_process_events()` loop. Lock-free triple-buffered slots for depth (320×240 11-bit, packed `uint16_t`) and color (640×480 RGB888 → BGRA). Game thread calls `Poll*()` reading the latest committed slot — no blocking on Kinect I/O. Shutdown joins the reader thread cleanly.

### M4 — Fake T-pose skeleton stub ⏳ pending

Hardcoded skeleton frame: 20 joints in T-pose centered at `(0, 0, 2.5m)` from sensor. Joint positions in canonical Xbox 360 NUI skeleton format (struct layout to be confirmed against Stage 0 telemetry). `XamNuiSkeletonGetNextFrame`/`XamNuiSkeletonGetBestSkeletonIndex` return this. Goal: title accepts as "skeleton tracked" → past "is anybody there?".

### M5 — Notification broadcast ⏳ pending

First `XamNuiSkeletonGetNextFrame` triggers `kXNotificationSystemNUISkeletonTrackingStatusChanged` broadcast (notification ID to confirm — likely `0x0006001A` or `0x0006001B`, check `notify_listener.h`). Argument: "skeleton 0 tracked".

### M6 — First end-to-end ⏳ pending

Capture `trace_i` log with M3+M4+M5 build, run Kinect Adventures, verify "is anybody there?" → main menu transition. Compare against `trace_h` (last pre-Stage-4 capture) via `parser.py --trace-a/--trace-b`.

## Stage 5 — Real skeleton tracking ⏳ pending — decision after M6

If fake T-pose passes the bootstrap gate but doesn't get into actual gameplay (gameplay requires real gestures/poses), pick a real tracker:

- **MediaPipe Pose** — least overhead, RGB-based, 33 landmarks → mapping to 20-joint Kinect skeleton. Single-person. Recommended starting point.
- **NiTE 2** — hard to find in 2026, archived downloads only, license uncertain. Investigate but don't depend on.
- **Custom ML** — random forest on depth (the original Microsoft 2011 approach). High effort, highest fidelity, longest runway.

## Cross-stage tracks

- **Weekly canary sync.** `.github/workflows/canary-sync.yml` cron-fast-forwards `canary` branch from upstream's `canary_experimental`. Manual rebase of `main` follows. First manual run pending verification.
- **Game compatibility tracking.** `docs/compat.md` post-Stage-4: which titles work with which backend. Kinect Adventures is the canonical smoke test.
- **Cherry-pick non-NUI bug fixes upstream.** Per CONTRIBUTING.md scope rules — anything outside the NUI allowlist gets PR'd to xenia-canary first, cherry-picked locally.

## Linux / macOS

Stage 4 is Windows-first because:
1. Real Kinect hardware (1473) needs libusbK driver via Zadig — Windows-only setup.
2. Win 11 + MS SDK 1.8 incompatibility was the original blocker forcing us off the SDK path.
3. CI Windows runners get the binary tested.

Linux/macOS path: `freenect` target is currently `if(WIN32)`-gated. Lifting that gate requires:
- Making `libusb` build on non-Windows (already supported upstream, minor CMake work).
- Verifying libfreenect's POSIX path works without our `unistd.h` shim (it should — POSIX system header exists natively).
- CI Linux build with `freenect` enabled.

Targeted post-Stage-5 alongside MediaPipe — same contributors likely care about both.

## What we are NOT doing in v1.0

- XAUDIO2 microphone integration. Kinect mic array is a separate, large effort. Tracked as v2.0 scope.
- Azure Kinect SDK. EOL'd by Microsoft, libdepthengine is closed-source. Skip.
- VR tracker / SlimeVR / Vive proxy. Suggested in upstream issue #2302 but couples our project to VR ecosystems we don't want to maintain.
- Multi-Kinect setups (research demos). Out of scope.
- Recording/replaying gameplay video. That's xenia's job, not ours.

## Done criteria for v1.0

- Stage 4 M6 verified end-to-end on Kinect Adventures (bootstrap → main menu).
- Stage 5 real skeleton tracking working at least on Kinect Adventures.
- One additional reference title (Fruit Ninja Kinect or Kinect Sports) playable.
- Pre-built Windows binaries on GitHub Releases.
- Documented setup path for a new user (`docs/getting-started.md` post-M6).
- An average of less than one hour of upstream-rebase pain per week.
