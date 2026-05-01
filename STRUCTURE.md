# Source structure

This is the file-level layout inside the forked xenia-canary tree. Files marked **NEW** are added by Kinectix; **MODIFIED** are inherited from xenia-canary and changed. Per-file status flags reflect actual state as of Stage 4 M2: ✅ landed, 🚧 in progress, ⏳ planned, ⏸ deprioritized.

```
src/xenia/
├── hid/
│   └── nui/                                              [NEW] ✅
│       │
│       ├── nui_backend.h                                 [NEW] ✅
│       │   # INuiBackend abstract interface.
│       │   # SkeletonJoint, SkeletonFrame, DepthFrame, ColorFrame structs.
│       │
│       ├── nui_manager.h                                 [NEW] ✅
│       ├── nui_manager.cc                                [NEW] ✅
│       │   # Singleton owning the active backend.
│       │   # Reads --nui_backend cvar, instantiates concrete backend.
│       │   # Translates host timestamps to guest timestamps.
│       │
│       ├── nui_constants.h                               [NEW] ✅
│       │   # NUI_SKELETON_POSITION_* joint indices, capability bitfields.
│       │
│       ├── null/                                         [NEW] ✅
│       │   └── null_backend.{h,cc}
│       │       # Default no-op backend (--nui_backend=none).
│       │       # Built always, returns IsConnected()=false.
│       │
│       ├── recorded/                                     [NEW] ⏸ deprioritized
│       │   ├── recorded_backend.{h,cc}                   ✅ stub landed
│       │   ├── xnuirec.fbs                               ✅ schema landed
│       │   ├── xnuirec_reader.{h,cc}                     ✅ stub: Open() always false
│       │   └── xnuirec_writer.{h,cc}                     ⏳ pending
│       │   # Stage 3 deprioritized once real Kinect became available.
│       │   # Schema and reader skeleton landed for future fixture replay
│       │   # (CI regression suite candidate post-v1.0).
│       │
│       ├── freenect/                                     [NEW] 🚧 Stage 4 M2
│       │   ├── nui_freenect.{h,cc}                       🚧 in progress
│       │   │   # Kinect v1 via libfreenect (third_party/libfreenect).
│       │   │   # Producer thread: libfreenect callbacks → triple-buffered
│       │   │   # depth (320×240 11-bit) and color (640×480 RGB888) slots.
│       │   │   # Skeleton tracking deferred to Stage 5.
│       │   ├── freenect2_backend.{h,cc}                  ⏳ Kinect v2, future
│       │   └── CMakeLists.txt                            🚧
│       │
│       ├── mediapipe/                                    [NEW] ⏳ Stage 5 candidate
│       │   ├── mediapipe_backend.{h,cc}                  ⏳ planned
│       │   └── pose_to_skeleton.cc                       ⏳ planned
│       │
│       ├── tests/                                        [NEW] ⏳ Stage 4 M3+
│       │   ├── nui_manager_test.cc                       ⏳ planned
│       │   ├── joint_mapping_test.cc                     ⏳ planned (when v2 lands)
│       │   └── fixtures/                                 ⏳ deferred with Stage 3
│       │
│       └── CMakeLists.txt                                [NEW] ✅
│           # Always builds null + recorded (stub).
│           # freenect/ subdir gated on WIN32 (Stage 4 M2 onward).
│
├── kernel/xam/
│   ├── xam_nui.cc                                        [MODIFIED] ✅
│   │   # Stage 2: telemetry tracer (XE_NUI_TRACE on all 28 exports).
│   │   # Stage 2.5: XN_SYS_NUI_HARDWARESTATUSCHANGED broadcast.
│   │   # Stage 4 M3+: route XamNuiSkeletonGet*/XamNuiGetDeviceStatus to
│   │   #   NuiManager → backend->PollSkeleton/IsConnected.
│   │
│   └── apps/xam_app.cc                                   [MODIFIED] ✅
│       # Stage 2.5: stub case 0x0002B001 (XamUnk2B001).

third_party/                                              [extended]
├── libfreenect/                                          [NEW submodule] ✅
│   # OpenKinect/libfreenect pinned to 09a1f09 (2024-01-06).
│   # Built inline by third_party/CMakeLists.txt's `freenect` STATIC
│   # target — we do NOT call libfreenect's own CMakeLists.txt
│   # (avoids its FindLibUSB/FindThreads modules clashing with ours).
│
├── freenect-msvc-compat/                                 [NEW] ✅
│   └── unistd.h
│       # Minimal POSIX shim for MSVC: usleep + sleep mapping to Sleep().
│       # libfreenect upstream includes <unistd.h> unconditionally; MSVC
│       # ships none. PRIVATE include for `freenect` target only.
│
└── libusb/                                               [existing] ✅
    # Already vendored upstream; we link `freenect` against the existing
    # `libusb` static target rather than introducing a vcpkg dependency.

tools/                                                    [NEW]
└── nui-trace/                                            [NEW] ✅
    ├── parser.py                                         ✅ stdlib-only
    │   # Analyzes captured --nui_telemetry logs.
    │   # CLI: --trace-a, --trace-b, --out-dir, --init-count, --seq-count
    │   # Produces summary.txt, init_sequence.txt, diff_a_vs_b.txt,
    │   # mermaid_seq.md.
    └── README.md                                         ✅

docs/                                                     ⏳ planned
├── telemetry/                                            ⏳ Stage 0/2 captures land here
│   └── (per-title trace_*.log + parsed summaries)
├── getting-started.md                                    ⏳ Stage 4 M6+
├── compat.md                                             ⏳ post-Stage-4
└── architecture/                                         ⏳ optional supplements

.github/
├── workflows/
│   ├── canary-sync.yml                                   [NEW] ✅
│   ├── lint.yml                                          [NEW] ✅
│   ├── build-windows.yml                                 [NEW] ✅
│   └── orchestrator.yml                                  [NEW] ✅
└── PULL_REQUEST_TEMPLATE.md                              ⏳ optional

README.md                                                 [NEW, replaces upstream] ✅
ARCHITECTURE.md                                           [NEW] ✅
ROADMAP.md                                                [NEW] ✅
CONTRIBUTING.md                                           [NEW] ✅
STRUCTURE.md                                              [NEW, this file] ✅
BUILDING.md                                               [NEW] ✅
LICENSE                                                   [INHERITED, BSD 3-Clause]
```

## Status as of Stage 4 M2

- **NEW landed (✅)**: docs (README/ARCHITECTURE/ROADMAP/CONTRIBUTING/STRUCTURE/BUILDING), CI (lint, build-windows, canary-sync, orchestrator), `src/xenia/hid/nui/` core (interface + manager + null + recorded stub), `xam_nui.cc` Stage 2 telemetry + Stage 2.5 broadcast, `tools/nui-trace/parser.py`, `third_party/libfreenect` submodule, `third_party/freenect-msvc-compat/unistd.h` shim.
- **MODIFIED upstream files**: minimal — `src/xenia/kernel/xam/xam_nui.cc`, `src/xenia/kernel/xam/apps/xam_app.cc`, plus CMake glue in `src/xenia/CMakeLists.txt`, `src/xenia/app/CMakeLists.txt`, `third_party/CMakeLists.txt`, `.gitmodules`.
- **DELETED upstream**: 0.

The discipline of touching almost no upstream files keeps the weekly rebase tractable. Every additional `[MODIFIED]` line is a future merge-conflict liability. Resist the urge to "fix this little thing while I'm here" inside Kinectix — fix it upstream instead.

## What's not in this layout

- **NUI backend selection is runtime, not compile-time.** No `KINECTIX_NUI_FREENECT=ON` build flags. Selection via `--nui_backend=...` cvar; unused backends are dead code in the binary but contribute negligibly to size. Linux/macOS contributors get null backend until libfreenect cross-platform support lands post-Stage-5.
- **`xam_nui_constants.cc`** — none. Constants live in `nui_constants.h` as `constexpr`.
- **"experimental" backend directory** — none. New backends go through the discussion-issue process per CONTRIBUTING.md and either land in `hid/nui/` proper or stay out.
- **System / find_package for libfreenect** — we vendor instead, see `third_party/libfreenect/` above. Reproducible builds via submodule pin (`09a1f09`); zero vcpkg dependency for the main build path.
