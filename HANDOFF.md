# Kinectix — Session Handoff (2026-05-02, after Stage 4 M4: fake T-pose skeleton stub via FreenectBackend, PR #8)

> Paste this as your first message in a new Cowork task to resume work with full context.

---

## Identity

- **User**: Krzysztof, communicates in Polish, works in **cmd.exe** on Windows 11.
- **GitHub**: RedMadKnight (`RedMadKnight@users.noreply.github.com`).
- **Role**: solo-dev on a fork.

## Project

- **Name**: Kinectix — independent fork of [xenia-canary](https://github.com/xenia-canary/xenia-canary).
- **Repo**: https://github.com/RedMadKnight/Kinectix
- **Mission**: implement Kinect (NUI) support that upstream xenia refused to prioritize for ~8 years.
- **Local clone**: `C:\Users\kjani\Kinectix\`
- **Branch model**: `main` (our work) ↔ `canary` (mirror of `upstream/canary_experimental`). Weekly `canary-sync` workflow fast-forwards `canary` (manual run verified clean 2026-05-01, run #25231755193, 15s).

## Build system

**CMake** (NOT premake — premake wrapper from upstream is being phased out in our fork). Tooling:
- Visual Studio **2026** (Windows host build, generator `"Visual Studio 18 2026"`)
- clang-format **20.1.7** specifically (CI lint rejects other versions)
- VS2026 needs **"Desktop development with C++" workload** — without it cmake configure fails with "could not find any instance"
- Vendored deps under `third_party/` (libusb, libfreenect, freenect-msvc-compat shim) — no vcpkg requirement

## Status (canonical source: [ROADMAP.md](ROADMAP.md))

| Stage | Description | Status |
|---|---|---|
| 0 | Project scaffolding (docs, CI, branch model) | ✅ |
| 1 | `INuiBackend` interface + null backend wired into CMake | ✅ |
| 2 | XAM NUI telemetry tracer (`--nui_telemetry`, all 28 exports) | ✅ tag `v0.0.2-telemetry` |
| 2.5 | Bootstrap unblock: `XN_SYS_NUI_HARDWARESTATUSCHANGED` broadcast + `XamUnk2B001` stub | ✅ PR #5 |
| 3 | Recorded backend (`.xnuirec` flatbuffers) | ⏸ deprioritized |
| 4 M1.5 | Hardware sanity — `freenect-camtest.exe` reads from Kinect 1473 via libusbK | ✅ |
| 4 M2 | Backend scaffold prep — `third_party/libfreenect` submodule + `freenect.lib` builds in Kinectix | ✅ PR #6 merged 2026-05-01 |
| 4 M3 | Backend module `src/xenia/hid/nui/freenect/` + threaded reader + triple-buffered depth/color | ✅ PR #7 squashed to `6eaaacbc0` on main 2026-05-02 |
| 4 M4 | Fake T-pose skeleton stub + `XamNuiSkeletonGetBestSkeletonIndex` wired to NuiManager | ✅ **PR #8 squashed to `df6ffb1bd` on main 2026-05-02, smoke-confirmed `caps=0x7`** |
| 4 M5 | Notification broadcast (likely `kXNotificationSystemNUISkeletonTrackingStatusChanged`, payload TBD) | 🚧 **NEXT — research-driven, KA telemetry test first** |
| 4 M6 | Kinect Adventures: "is anybody there?" → main menu | ⏳ |
| 5 | Real skeleton tracking (MediaPipe / NiTE2 / custom ML) — decision after M6 | ⏳ |

Branch protection on `main`: PR + lint+build status checks + linear history required.

## Stage 4 M4 — what landed (PR #8, squashed `df6ffb1bd`)

Fake T-pose skeleton stub. Five files, ~150 LOC net.

- `src/xenia/hid/nui/freenect/freenect_backend.h` — added `last_skeleton_emit_us_` + `skeleton_emit_count_` rate-limit state on `FreenectBackend`. Updated scope comment block to include M4.
- `src/xenia/hid/nui/freenect/freenect_backend.cc` — anon namespace with `kFakeTposeXY` (20 joint XY positions in sensor-relative meters, anatomical proportions of an average adult standing 2.5 m from sensor) + `FillFakeTposeFrame()` helper + `kFakeSkeletonPeriodUs = 33333` (30 Hz cap). `Capabilities()` now returns `kCapabilityDepth | kCapabilityColor | kCapabilitySkeleton` (was 0x6, now **0x7**). `PollSkeleton(index)`: `index!=0` → `nullopt`; rate-limited at 30 Hz against `HostNowUs()` so guest pollers spin without amplifying logs; `XELOGD` on emission #1 and every 30th to confirm liveness under `--log_level=3`.
- `src/xenia/kernel/CMakeLists.txt` — added `xenia-hid-nui` to `target_link_libraries(xenia-kernel PUBLIC ...)`. Without this the next change wouldn't link.
- `src/xenia/kernel/xam/xam_nui.cc` — `#include "xenia/hid/nui/nui_manager.h"`. `XamNuiSkeletonGetBestSkeletonIndex_entry` now consults `NuiManager::Instance()->backend()`: returns `0` when `IsConnected() && SupportsSkeleton()`, sign-extended `-1` (`0xffffffffffffffff`) otherwise. Game titles gating on "is anybody being tracked" should now advance.
- `.gitignore` — added `out/`, `xenia_release/`, `build/version.h` (HANDOFF backlog item; the version.h quick-fix recipe must NEVER be committed).

**What's NOT in M4 yet:** `XamNuiSkeletonGetCurrentFrame` / `GetNextFrame` style exports do not exist in `xam_table.inc`. Real skeleton frame data flows through `xnatal.dll` / `Xnui4.dll` guest-side which talks to NUI runtime via `XMsgInProcessCall(0x21008,...)` or shared memory. The exact mechanism KA uses is unknown until live-fire telemetry — that's M5/M6 work, not M4.

**Smoke test result on Krzysztof's Kinect 1473** (log_level=2, no game): `freenect: backend ready (depth+color streaming).` + `NUI: kinectix: backend=freenect connected=yes caps=0x7`. Caps bit confirmed M3 → M4 transition. XELOGD emissions visible under log_level=3.

## Stage 4 M2 — what landed (PR #6, commit `4edde42`)

Two-axis change: vendor libfreenect + sync stale docs to reality.

**Vendoring:**
- `third_party/libfreenect/` — submodule, pinned to `09a1f098040d00e6070c18174904547ec31d2774` (2024-01-06).
- `third_party/CMakeLists.txt` — inline `freenect` STATIC target (8 SRC files: `core/tilt/cameras/flags/usb_libusb10/registration/audio/loader.c`), links against existing `libusb` static target. Skips upstream's `add_subdirectory` to avoid `FindLibUSB.cmake` / `FindThreads.cmake` clashes.
- `third_party/freenect-msvc-compat/unistd.h` — minimal POSIX shim. libfreenect upstream includes `<unistd.h>` unconditionally on every platform; MSVC ships none. Shim provides `usleep` (Sleep + SwitchToThread for sub-ms) and guarded `sleep` macro. PRIVATE include first in `freenect` target so `<unistd.h>` resolves to our shim.
- Build verified clean: `cmake --build . --config Release --target freenect` produces `build\obj\Windows\Release\freenect.lib` (~200KB). Zero warnings under `/W0` directory scope.

**Docs sync (Stage 0 docs were misleading vs. Stage 4 reality):**
- `README.md` — Status table reflects reality, new "Hardware & driver requirements" section, CMake build commands replace stale premake snippets, runtime `--nui_backend` cvar replaces invented `KINECTIX_NUI_*` build flags.
- `ROADMAP.md` — complete rewrite with Stage 4 M1.5/M2/M3/M4/M5/M6 granularity, Stage 5 skeleton tracking decision tree.
- `BUILDING.md` — premake → CMake; removed nonexistent build-flag table; expanded common build issues (VS Installer state, unistd.h shim, generator cache lock).
- `ARCHITECTURE.md` — file tree annotated with status flags; `third_party/` section added; XAM NUI integration described stage-by-stage.
- `STRUCTURE.md` — removed false "no vendored libfreenect" claim; per-file status flags.
- `CONTRIBUTING.md` — scope allowlist extended for `third_party/libfreenect`/`freenect-msvc-compat`/`tools/nui-trace`. Test requirements softened (Stage 3 deprioritized, no `.xnuirec` fixture oracle); new backends gated by `--nui_backend` cvar, not build flag.

**Other housekeeping in this session:**
- Closed stale issues #1, #2 (old Stage 0/1 numbering, work done elsewhere) and #3 (canary-sync verified manually).

## Cvars (unchanged from previous handoff)

```
--nui_backend=none|recorded|freenect|freenect2|mediapipe   (default: none)
--nui_record_path=<path>                                   (only for recorded)
--nui_telemetry                                            (default: false)
```

## M3 plan (start of next session) — backend module + frame capture

**Goal:** `xenia.exe --nui_backend=freenect` opens Kinect 1473, reads depth + color frames continuously into triple-buffered slots, exits cleanly. No skeleton tracking yet (M4). No game integration yet (M6).

**Concrete steps:**

1. **Backend module skeleton** — new files:
   - `src/xenia/hid/nui/freenect/nui_freenect.h` — `class FreenectBackend : public INuiBackend` (declaration). Members: `freenect_context* ctx_`, `freenect_device* dev_`, `std::thread reader_thread_`, `std::atomic<bool> running_`, triple buffers for depth/color.
   - `src/xenia/hid/nui/freenect/nui_freenect.cc` — implementation:
     - `Setup()`: `freenect_init(&ctx_, NULL)`, `freenect_select_subdevices(ctx_, FREENECT_DEVICE_CAMERA)` (motor + audio out), `freenect_open_device(ctx_, &dev_, 0)`, register depth + video callbacks, `freenect_set_depth_mode/MEDIUM/11BIT`, `freenect_set_video_mode/MEDIUM/RGB`, `freenect_start_depth/video`, spawn `reader_thread_`.
     - reader thread loop: `while (running_) { freenect_process_events(ctx_); }`.
     - depth callback: write to next-available depth slot (lock-free triple buffer, atomic seq).
     - video callback: same for color.
     - `Shutdown()`: `running_ = false`, `freenect_stop_depth/video`, `freenect_close_device`, `freenect_shutdown`, `reader_thread_.join()`.
     - `IsConnected()`: `dev_ != nullptr`.
     - `PollDepth/Color()` returning latest committed slot.
     - `PollSkeleton()` returns `std::nullopt` for now (M4 fills in fake T-pose).
   - `src/xenia/hid/nui/freenect/CMakeLists.txt` — `add_library(xenia-hid-nui-freenect STATIC ...)`, `target_link_libraries(xenia-hid-nui-freenect PRIVATE freenect xenia-base)`.

2. **Wire into NUI module CMake**:
   - `src/xenia/hid/nui/CMakeLists.txt` — `if(WIN32) add_subdirectory(freenect) endif()`. Conditionally append `xenia-hid-nui-freenect` to the `xenia-hid-nui` link interface (or directly into `xenia-app`).

3. **Wire into NuiManager**:
   - `src/xenia/hid/nui/nui_manager.cc` — branch on `cvars::nui_backend == "freenect"` → instantiate `FreenectBackend`. Already done conditionally for `recorded`; add `freenect` arm under `#ifdef XE_PLATFORM_WIN32` (or check the project's existing platform define convention).

4. **Triple buffer implementation**:
   - 3-slot rotating buffer with `std::atomic<uint32_t> latest_seq_`.
   - Writer (callback thread): pick slot whose `seq < latest_seq_ - 1`, fill, atomic store new `latest_seq_`.
   - Reader (game thread): atomic load `latest_seq_`, copy from `slots[seq % 3]`. Slot 0 = depth, slot 1 = color, separate buffers per kind.
   - Sizes: depth = `320 * 240 * sizeof(uint16_t) = 150 KB`, color = `640 * 480 * 3 = 900 KB`. Triple = ~3.1 MB total. Acceptable.

5. **Smoke test**:
   - Build `xenia-app` with the new code.
   - Run `xenia.exe --nui_backend=freenect --log_level=info` (no game), watch the log:
     - Setup logs: `freenect: device open, serial=...`, `depth stream up`, `video stream up`.
     - Reader thread should run silently (no log spam — debug log only).
     - On exit (Ctrl+C or close), Shutdown logs cleanly.
   - Optional: add a debug cvar `--nui_freenect_dump_frames=<path>` that writes the first depth frame as PGM and first color frame as PPM. Useful for visual sanity. Drop after M3.

6. **Commit + PR** — `[nui] Stage 4 M3: libfreenect backend module + threaded frame reader`. Single PR, reviewable diff (~300-500 LOC new code).

**Order of operations on Krzysztof's side**: I prep patches in sandbox. Krzysztof commits/pushes/PRs.

## Open pendings (post-M4 backlog)

1. **Stage 4 M5-M6** — research-driven. Need KA live-fire telemetry log first to nail down which notification + payload + frame-fetch path KA actually uses. THEN implement M5 with concrete data, not speculation.
2. **Stage 5 decision** — real skeleton tracking. Default candidate: MediaPipe Pose against RGB stream, 33 → 20-joint mapping. Final call after M6.
3. **Side-fix PR**: stub `XamVoiceSetMicArrayIdleUsers` (0x48C), `XamVoiceMuteMicArray` (0x48D), `XamVoiceGetMicArrayUnderrunStatus` (0x48E), `XamVoiceGetMicArrayAudioEx` (0x490), `XamVoiceDisableMicArray` (0x491). Entries exist in `xam_table.inc` but `DECLARE_XAM_EXPORT1` missing in `xam_voice.cc`. KA tolerates NULL — not blocking — but cleaner stack.
4. **Open new tracking issues in GitHub** (old #1/#2/#3 closed; new ones reflect post-rewrite ROADMAP):
   - `[nui] Stage 4 M5-M6: skeleton notification broadcast + KA bootstrap`
   - `[nui] Stage 5: real skeleton tracking decision`
   - `[xam] Stub XamVoice mic-array exports (0x48C–0x491)`
   - `[infra] Document Win11 + MS Kinect SDK 1.8 incompatibility` — body draft already at `C:\Users\kjani\issue-win11-mssdk-incompat.md` (~6 KB, `gh issue create --body-file ...`).
5. **`[infra] Generate version.h via CMake configure_file`** — kill the manual quick-fix recipe (gotcha #24); wire `src/xenia/version.h.in` + `configure_file()` in root CMakeLists with `execute_process(git rev-parse ...)` at configure time. Closes the premake → CMake migration gap. Now that `build/version.h` is gitignored (M4), the recipe stays in cmd memory only — proper fix elevates priority.

## Critical gotchas (cumulative — read all)

1. **Write tool truncates files >~16KB silently. Edit tool ALSO truncates >~17KB silently.** For large files: bash heredocs through sandbox, generate patch via `diff -u`, normalize CRLF→LF, `git apply --ignore-whitespace --whitespace=fix`. Always full `Read` after Edit.
2. **xenia-canary uses CMake, not premake.** The `./xb premake` / `./xb build` snippets in old upstream docs are being phased out in our fork.
3. **Branch ambiguity**: never name a local branch `upstream` — conflicts with the conventional remote.
4. **clang-format-20.1.7 strict**: `pip install clang-format==20.1.7 --break-system-packages`. Run via `clang-format --style=file -n -Werror <files>` to verify, `-i` to fix.
5. **Argument printing in Xenia shims**: `ParamBase<T>` (used for `dword_t` etc.) has `operator T()` — print via `static_cast<uint32_t>(arg)`. `PointerParam` / `PrimitivePointerParam` have `.guest_address()`.
6. **User uses cmd.exe, not Git Bash**: `mkdir`, `copy /Y`, `xcopy /E /I /Y`. Heredocs (`<<EOF`) don't work in cmd — use `notepad <file>` or `--body-file` for multiline `gh` arguments.
7. **GitHub branch protection UI**: "Include administrators" → "Do not allow bypassing" — for solo-dev leave OFF.
8. **`gh pr create` z forka domyślnie celuje w upstream parent**. Always `gh repo set-default RedMadKnight/Kinectix` first. Once set, persists.
9. **`gh pr create --body-file -` waits on stdin in cmd** (no trivial EOF — Ctrl+Z+Enter works but unfamiliar). Use `--body "..."`, `--body-file <real-file>`, or `--fill` (auto-fills title/body from commit message). Or `gh pr create -w` for browser.
10. **`gh pr create --fill`** takes title from FIRST LINE of last commit, body from rest. Make commit message readable as PR title — capitalize, use prefix (`[infra]`, `[nui]`).
11. **Don't commit from sandbox.** Sandbox only reads + generates patches. Commit/push from cmd on Krzysztof's side.
12. **MS Kinect SDK 1.8 does NOT work on Windows 11** (Code 39 + Bad Image — `kinectcamera.sys` WDF coinstaller `WdfCoInstaller01009.dll` is KMDF 1.9 from 2010, incompatible with Win 11 KMDF runtime). We use libfreenect over libusbK driver (Zadig-installed) instead.
13. **VS2026 requires "Desktop development with C++" workload** — without it cmake sees the `Visual Studio 18 2026` generator in `--help` but throws `could not find any instance` at configure. Sanity check: `where cl`.
14. **VS2026 install can be in incomplete state** even though instance exists. `vswhere -all -prerelease` shows `"isComplete": false, "isLaunchable": false, "state": 13` after an interrupted/queued update. Configure might pass (cmake finds `cl.exe` directly) but `cmake --build` fails with "could not find specified instance of Visual Studio". Fix: open VS Installer → Repair on Community 2026. Verify with `vswhere ... -property isComplete` returning `1`.
15. **CMake configure cache locks generator** — switching generator string requires `rmdir /S /Q build && mkdir build`.
16. **Kinect 1414 vs 1473 on Windows 11**: 1414 LED blinks in a loop (USB enum loop, suspected xHCI quirk). 1473 stable — use 1473. Unplug 1414 if present to minimize USB interference.
17. **`freenect-camtest.exe` is infinite loop** (reads frames forever) — terminate with Ctrl+C. First ~10 packets are typically "Lost"/"Invalid magic" at USB sync startup; libfreenect resyncs (`Lost too many packets, resyncing...`) then runs clean. Normal behavior.
18. **libfreenect upstream `#include <unistd.h>` unconditionally** on Windows. MSVC ships none. Our `third_party/freenect-msvc-compat/unistd.h` shim provides `usleep` (static inline) + `sleep` (macro, guarded by `#ifndef`). Bound to `freenect` target via PRIVATE include FIRST — don't reorder.
19. **libfreenect's custom `cmake_modules/FindThreads.cmake` looks for `pthreadVC2.lib`** but vcpkg pthreads4w installs `pthreadVC3.lib`. Workaround already baked in: we don't use libfreenect's CMakeLists at all — inline our own target in `third_party/CMakeLists.txt`. So this gotcha applies only if someone ever decides to switch to `add_subdirectory(third_party/libfreenect)` (don't).
20. **GitHub issues numeric drift**: don't trust old issue titles for stage numbering. Old #1/#2/#3 used pre-Stage-2 ROADMAP numbering and are closed. ROADMAP.md is now single source of truth; new issues should reference its stage labels.
21. **Default `gh pr create` autonumbers PR title from branch slug** if no commit message body — produces lowercase mash like "stage4 m2 libfreenect vendor". Use `--fill` (with a properly capitalized commit message) or `--title` explicitly.
22. **`--log_level` is `int32`, not a string.** `DEFINE_int32(log_level, 2, ...)` in `src/xenia/base/logging.cc:61`. Values: `0=error, 1=warning, 2=info, 3=debug`. Don't pass `--log_level=info` — xenia rejects it with "Argument 'info' failed to parse". Default 2 (info) is fine for smoke tests; use `--log_level=3` if you need XELOGD too.
23. **XELOG* in xenia is fmt-style (`{}`), NOT printf-style (`%s`).** `template <typename... Args> XELOGE(std::string_view format, const Args&... args)` routes through `fmt::format_to_n` (logging.h:171-189). `%s` becomes a literal `'%s'` in the log with no substitution — silent loss of diagnostic info. Pass `std::string` directly; no `.c_str()` needed (fmt accepts both). M3 PR fixed three leftover `%s` sites in `nui_manager.cc`; check any new XELOG additions.
24. **`build/version.h` must exist before xenia-base compiles** — defines `XE_BUILD_BRANCH`, `XE_BUILD_COMMIT`, `XE_BUILD_COMMIT_SHORT`, `XE_BUILD_DATE`, optionally `XE_BUILD_IS_PR` + `XE_BUILD_PR_NUMBER`. Used in `main_win.cc`, `emulator_window.cc`, `trace_writer.cc`, `windowed_app_main_win.cc`. NO generation rule exists in any CMakeLists (premake → CMake migration gap). Quick-fix recipe (cmd):
    ```cmd
    for /f %i in ('git rev-parse HEAD') do set XE_FULL=%i
    for /f %i in ('git rev-parse --short HEAD') do set XE_SHORT=%i
    for /f %i in ('git rev-parse --abbrev-ref HEAD') do set XE_BRANCH=%i
    > build\version.h echo #pragma once
    >> build\version.h echo #define XE_BUILD_BRANCH "%XE_BRANCH%"
    >> build\version.h echo #define XE_BUILD_COMMIT "%XE_FULL%"
    >> build\version.h echo #define XE_BUILD_COMMIT_SHORT "%XE_SHORT%"
    >> build\version.h echo #define XE_BUILD_DATE "2026-05-02"
    ```
    Proper fix backlog item: add `src/xenia/version.h.in` + `configure_file(... ${CMAKE_BINARY_DIR}/version.h)` in root CMakeLists, ideally with `execute_process(... git rev-parse ...)` at configure time. Separate PR.
25. **Vulkan SDK is a hard build dependency.** `tools/build/compile_shader_spirv.py` calls `glslangValidator`/`spirv-opt`/`spirv-dis` via `VULKAN_SDK` env or PATH (script lines 34-47). Without it: `FileNotFoundError [WinError 2]` on `xenia-ui-vulkan-spirv-shaders` and `xenia-gpu-vulkan-spirv-shaders` targets. Install LunarG SDK from https://vulkan.lunarg.com/, restart cmd (env doesn't propagate to current shell), reconfigure CMake.
26. **Vulkan GPU backend has rendering regressions on Win 11** — text/UI artifacts visible in-game when `gpu = "any"` defaults to Vulkan. Not our problem (upstream xenia issue). Workaround: pin `gpu = "d3d12"` in `xenia-canary.config.toml [GPU]` section, or pass `--gpu=d3d12` on command line. M3 smoke test confirmed clean rendering on D3D12.
27. **PR-from-feature-branch ≠ PR-from-main.** If you `git checkout -b new-branch` while still on a feature branch (because `git checkout main` was blocked by dirty working tree), the new branch inherits both your unstaged edits AND the parent feature branch's commits. The resulting PR shows ALL those commits, not just yours. Recovery without reopening the PR: (a) merge the parent feature PR first via Squash-and-merge, (b) `git fetch origin && git rebase origin/main` — rebase auto-skips commits whose patch is already in main (`skipped previously applied commit <SHA>`), (c) `git push --force-with-lease` and the open PR auto-refreshes with only your delta. Lesson: always start a new feature branch with `git checkout -b X main`, even if you have to stash first.
28. **`git stash push -- <path>` is path-scoped** — stashes only that file, leaves rest of index/working tree intact. Useful when you want to rebase but have one file uncommitted (e.g. HANDOFF.md edits during a code PR). Pair with `git stash pop` after the rebase / branch switch to recover. Note that `git stash pop` can apply across a fast-forward main pull (it merges by patch, not by SHA), so the workflow `stash → merge PR → pull main → stash pop` works cleanly.
29. **`.git/config` can rot to "fatal: bad config line N"** — typically a stray whitespace line. Doesn't block git operations from cmd, but does block sandbox-side `git diff` (sandbox parses config more strictly than Windows git). Fix: open `.git/config` in editor, find line N, delete it. Or just edit files via the file tools and skip sandbox git altogether.
30. **`--nui_telemetry` is the diagnostic firehose** — gates every XAM NUI export entry log via `XELOGI("[nui] Foo(args)")`. Default off. Turn ON for KA bootstrap research, OFF for normal runs. Combine with `--log_level=3` to also see XELOGD lines (e.g. fake T-pose emit confirmations).

## Communication style preferences

- Polish.
- Concise. Krzysztof dislikes long bullet lists when prose works.
- Technical, direct, no fluff or excessive apologies.
- Krzysztof runs git / cmd / GitHub UI manually — Claude prepares files, scripts, commit blocks, and waits.
- When CI is the gating factor, hand off and stop generating new code until results come back.

## First task for the new session

1. **Sanity confirm M4 still healthy on a fresh main pull:**
   ```cmd
   cd C:\Users\kjani\Kinectix
   git fetch origin && git checkout main && git pull
   git submodule update --init --recursive
   cmake -B build -DKINECTIX_NUI_FREENECT=ON
   cmake --build build --config Release --target xenia-app
   build\bin\Windows\Release\xenia_canary.exe --nui_backend=freenect --gpu=d3d12 --log_level=3
   ```
   Should log: `freenect: backend ready (depth+color streaming).` + `NUI: kinectix: backend=freenect connected=yes **caps=0x7**` (skeleton bit set) + within ~3 s `freenect: fake T-pose emit #1 head=(0.00, 1.65, 2.50) state=tracked` + every 30 emissions thereafter. Ctrl+C → `freenect: shutdown complete.`.

2. **Stage 4 M5 research — Kinect Adventures live-fire telemetry log.** Don't implement M5 blind. Run KA with `--nui_telemetry=true --log_level=3`:
   ```cmd
   build\bin\Windows\Release\xenia_canary.exe --nui_backend=freenect --gpu=d3d12 --log_level=3 --nui_telemetry=true <KA-default.xex>
   ```
   Capture log + screenshot of where KA stalls. We want to see:
   - Is `freenect: fake T-pose emit #1 ...` produced (M4 path alive)?
   - Does `[nui] XamNuiSkeletonGetBestSkeletonIndex(...)` log appear, and does our `0` return value satisfy KA?
   - What XAM NUI exports does KA call AFTER `XamNuiGetDeviceStatus` and `XamNuiSkeletonGetBestSkeletonIndex`?
   - Where does KA stop / loop / crash?

   Telemetry data → exact M5 scope (which notification, what payload, where wired). HANDOFF's prior guess of `kXNotificationSystemNUISkeletonTrackingStatusChanged` is plausible but unverified.

3. **Stage 4 M5 — implementation.** Driven by step 2 log. Likely shape: emit notification when `IsConnected()` flips or first skeleton enters `kSkeletonTracked`. Pattern from PR #5 (`XN_SYS_NUI_HARDWARESTATUSCHANGED`): `kernel_state()->BroadcastNotification(<id>, <data>)`. Implementation file probably `nui_manager.cc` or `xam_nui.cc`.

4. **Stage 4 M6 — KA progresses past "is anybody there?" → main menu.** Live-fire validation that M3+M4+M5 plumbing is consistent with what the title queries. May reveal an additional gate (e.g. `XamNuiSkeletonScoreUpdate`, frame-fetch syscall) — fold into M5 or split into M6.5.

5. **Stage 5 decision** (real skeleton tracking) — after M6.

### Post-M4 backlog (separate PRs)

- `[docs] Sync HANDOFF.md after M3+M4 landed` — this very file. Krzysztof has uncommitted edits in working tree from doc maintenance during the M3/M4 sessions.
- `[infra] Generate version.h via CMake configure_file` — kill the manual quick-fix recipe (gotcha #24); wire `src/xenia/version.h.in` + `configure_file()` in root CMakeLists with `execute_process(git rev-parse ...)` at configure time. Closes the premake → CMake migration gap. `build/version.h` is now gitignored (M4 PR), so the recipe stays in cmd memory only — proper fix is overdue.
- `[xam] Stub XamVoice mic-array exports (0x48C–0x491)` — entries exist in `xam_table.inc`, missing `DECLARE_XAM_EXPORT1` in `xam_voice.cc`.
- `[infra] Document Win11 + MS Kinect SDK 1.8 incompatibility` — body draft at `C:\Users\kjani\issue-win11-mssdk-incompat.md`.
- `[nui] Stage 5: real skeleton tracking decision` — placeholder issue, fill after M6.

Krzysztof handles commits/push/gh on his own. Sandbox only generates patches. Wait for his cmd output after each action.
