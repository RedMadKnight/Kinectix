# Kinectix — Session Handoff (2026-05-02, after Stage 4 M5 (partial): NUI hardware probe stubs + status bitmask + UIApproach broadcast, PR #10)

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
- **Branch model**: `main` (our work) ↔ `canary` (mirror of `upstream/canary_experimental`). Weekly `canary-sync` workflow fast-forwards `canary`.

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
| 4 M2 | Backend scaffold prep — `third_party/libfreenect` submodule + `freenect.lib` builds | ✅ PR #6 merged 2026-05-01 |
| 4 M3 | Backend module + threaded reader + triple-buffered depth/color | ✅ PR #7 squashed to `6eaaacbc0` 2026-05-02 |
| 4 M4 | Fake T-pose skeleton stub + `XamNuiSkeletonGetBestSkeletonIndex` wired to NuiManager | ✅ PR #8 squashed to `df6ffb1bd` 2026-05-02, smoke-confirmed `caps=0x7` |
| 4 M5 | NUI hardware probe stubs (`PsCam`/`Mca`/`Detroit`/`XamXStudio`) + `XamNuiGetDeviceStatus` bitmask `0x44` + `kXNotificationSystemNUIUIApproach(1)` broadcast | ⚠️ **PR #10 merged 2026-05-02 — semantically correct, KA bootstrap STILL stalled** |
| 4 M5.4 | Decompile KA `default.xex` (IDA / Ghidra) → identify exact gate that rejects NUI even with M5 patches | 🚧 **NEXT — Krzysztof handling decompile manually** |
| 4 M6 | Kinect Adventures: "is anybody there?" → main menu | ⏳ blocked on M5.4 findings |
| 5 | Real skeleton tracking (MediaPipe / NiTE2 / custom ML) — decision after M6, OR sooner if KA unreachable | ⏳ |

Branch protection on `main`: PR + lint+build status checks + linear history required.

## Stage 4 M5 — what landed (PR #10, partial)

Three changes addressing KA's NUI bootstrap surface. Two files, ~132 LOC net.

**`src/xenia/kernel/xam/xam_nui.cc`:**
- `XamNuiGetDeviceStatus_entry` — `status` field is now a bitmask: `0x44` (sensor present + ready) when backend connected, `0x40` (present but not ready) when `allow_nui_initialization=true` but no backend, `0` otherwise. Reverse-engineered from XAM build `0.0.13599.32`. Previous boolean `1` was semantically wrong. Sanity log added (visible at `--log_level=3`).
- `MaybeBroadcastNuiHardwareStatus` — broadcasts `kXNotificationSystemNUIHardwareStatusChanged` with payload `0x44` instead of `1` (matches the bitmask above; KA's listener mask `0x87` includes the NUI bit `0x80` and was rejecting the boolean). Now also broadcasts `kXNotificationSystemNUIUIApproach(1)` (`0x0006001B`) right after — synthesizes "player has entered the sensor's tracking volume", asserted by the M4 fake T-pose backend. Both fire only on first device-status probe (atomic gate).
- New: `XamXStudioRequest_entry` (xam ordinal `0x686`) — was registered in `xam_table.inc` but had no `_entry` shim. Now returns `X_ERROR_SUCCESS` and zeroes `*out_ptr`.

**`src/xenia/kernel/xboxkrnl/xboxkrnl_usbcam.cc`:**
- `PsCamDeviceRequest_entry` (xboxkrnl `0x30D`) — KA imports this directly. Was unimplemented (resolver left trampoline `nullptr`). Returns `X_ERROR_SUCCESS`.
- `McaDeviceRequest_entry` (xboxkrnl `0x30E`) — Microphone Array probe sibling stub.
- `DetroitDeviceRequest_entry` (xboxkrnl `0x30F`) — "Detroit" launch-era Natal codename stub.

**Why "(partial)":** KA (`4D5308ED`) STILL stalls on its "Is anybody there?" calibration screen with all four stubs registered (no longer flagged `!!` in title import dump) and bitmask `0x44` reaching the listener. Telemetry confirms:
- `XamNuiGetDeviceStatus → status=0x44 backend_connected=true` ✅
- Both broadcasts fire ✅
- KA proceeds **directly to `XamShowNuiHardwareRequiredUI`** (~2 log lines after the device-status probe), then idles for ~125k log lines.
- KA never calls `XamNuiSkeletonGetBestSkeletonIndex`, never calls `XNotifyGetNext` from the main thread, never calls `XamInputGetState` (count = 0). It also never calls `PsCam` / `Mca` / `Detroit` / `XamXStudio` directly — those stubs are infrastructure for other titles, not KA's bootstrap path.

**The unidentified gate.** KA decides "NUI not ready" through some channel we have not surfaced. Top candidates:
1. NUI shared-memory IO region the launch-era runtime memory-mapped into the title's address space (xenia returns zero-filled pages).
2. Controller-input event delivery that bypasses `XamInputGetState` (event-driven HID path in another thread).
3. An XAM/xboxkrnl call invoked from one of KA's 31 worker threads that we miss in main-thread telemetry.

M5.4 = decompile `default.xex`, locate xref to ordinal `0x47D` (`XamShowNuiHardwareRequiredUI`), backtrack to find the comparator that decides to call it.

## Stage 4 M4 — what landed (PR #8, squashed `df6ffb1bd`)

Fake T-pose skeleton stub. Five files, ~150 LOC net.

- `freenect_backend.h/.cc` — `kFakeTposeXY` (20 joints sensor-relative meters) + `FillFakeTposeFrame()` + 30 Hz cap. `Capabilities()` now `0x7` (depth+color+skeleton). `PollSkeleton(0)` returns T-pose, others nullopt. `XELOGD` on emission #1 + every 30th.
- `kernel/CMakeLists.txt` — added `xenia-hid-nui` to `target_link_libraries(xenia-kernel PUBLIC ...)`.
- `kernel/xam/xam_nui.cc` — `XamNuiSkeletonGetBestSkeletonIndex_entry` consults NuiManager: returns `0` when `IsConnected() && SupportsSkeleton()`, `-1` otherwise.
- `.gitignore` — added `out/`, `xenia_release/`, `build/version.h`.

## Stage 4 M2 — what landed (PR #6, commit `4edde42`)

Vendored libfreenect + sync stale Stage 0 docs. Submodule pinned to `09a1f098040d00e6070c18174904547ec31d2774`. Inline `freenect` STATIC target in `third_party/CMakeLists.txt`. POSIX `unistd.h` shim in `third_party/freenect-msvc-compat/`. README/ROADMAP/BUILDING/ARCHITECTURE/STRUCTURE/CONTRIBUTING all rewritten for Stage 4 reality.

## Cvars

```
--nui_backend=none|recorded|freenect|freenect2|mediapipe   (default: none)
--nui_record_path=<path>                                   (only for recorded)
--nui_telemetry                                            (default: false)
--allow_nui_initialization                                 (default: false)
```

## Open pendings (post-M5 backlog)

1. **Stage 4 M5.4 — decompile KA `default.xex`.** Krzysztof running IDA / Ghidra manually. Goal: find function calling `XamShowNuiHardwareRequiredUI` (xam ordinal `0x47D` = decimal 1149), identify comparator/branch, learn what KA actually checks beyond `XamNuiGetDeviceStatus` payload + notifications. Output: (a) additional XAM/xboxkrnl call to stub, (b) NUI shared-memory IO region to emulate, or (c) controller-input event path to wire. M5.5 implementation follows.
2. **Cheap parallel research — try a different Kinect title.** If Dance Central / Kinect Sports / Star Wars Kinect progresses past where KA stalls with M5 patch as-is, KA is a launch-era special case — may justify pivoting Stage 5 onto more cooperative title.
3. **Stage 5 decision** — real skeleton tracking. Default candidate: MediaPipe Pose against RGB stream, 33 → 20-joint mapping. May start sooner if M5.4 reveals KA's gate is non-emulatable.
4. **Side-fix PR**: stub `XamVoiceSetMicArrayIdleUsers` (0x48C), `XamVoiceMuteMicArray` (0x48D), `XamVoiceGetMicArrayUnderrunStatus` (0x48E), `XamVoiceGetMicArrayAudioEx` (0x490), `XamVoiceDisableMicArray` (0x491). Entries exist in `xam_table.inc` but `DECLARE_XAM_EXPORT1` missing.
5. **GitHub tracking issues** to open:
   - `[nui] Stage 4 M5.4: decompile KA default.xex to find unidentified bootstrap gate`
   - `[nui] Stage 5: real skeleton tracking decision`
   - `[xam] Stub XamVoice mic-array exports (0x48C–0x491)`
   - `[infra] Document Win11 + MS Kinect SDK 1.8 incompatibility` — body draft at `C:\Users\kjani\issue-win11-mssdk-incompat.md`.
6. **`[infra] Generate version.h via CMake configure_file`** — kill manual quick-fix recipe (gotcha #24); `configure_file()` in root CMakeLists with `execute_process(git rev-parse ...)` at configure time. Overdue.

## Critical gotchas (cumulative — read all)

1. **Write tool truncates files >~16KB silently. Edit tool ALSO truncates >~17KB silently.** For large files: bash heredocs through sandbox + Python `string.replace`, verify size + last 5 lines after every write. Always full `Read` after Edit. Recovery: `git checkout HEAD -- <path>` then re-apply via Python. **Re-confirmed FOUR TIMES in M5 session** — easy to forget when patches feel "small". Even Write tool wrapping a 22KB string fails silently.
2. **xenia-canary uses CMake, not premake.** The `./xb premake` / `./xb build` snippets in old upstream docs are being phased out.
3. **Branch ambiguity**: never name a local branch `upstream` — conflicts with the conventional remote.
4. **clang-format-20.1.7 strict**: `pip install clang-format==20.1.7 --break-system-packages`. Verify via `clang-format --style=file -n -Werror <files>`.
5. **Argument printing in Xenia shims**: `ParamBase<T>` (used for `dword_t` etc.) has `operator T()` — print via `static_cast<uint32_t>(arg)`. `PointerParam` / `PrimitivePointerParam` have `.guest_address()`.
6. **User uses cmd.exe, not Git Bash**: `mkdir`, `copy /Y`, `xcopy /E /I /Y`. Heredocs (`<<EOF`) don't work in cmd — use `notepad <file>` or `--body-file`.
7. **GitHub branch protection UI**: "Include administrators" → "Do not allow bypassing" — for solo-dev leave OFF.
8. **`gh pr create` z forka domyślnie celuje w upstream parent**. Always `gh repo set-default RedMadKnight/Kinectix` first.
9. **`gh pr create --body-file -` waits on stdin in cmd**. Use `--body "..."`, `--body-file <real-file>`, or `--fill`.
10. **`gh pr create --fill`** takes title from FIRST LINE of last commit, body from rest. Make commit message readable as PR title — capitalize, use prefix (`[infra]`, `[nui]`).
11. **Don't commit from sandbox.** Sandbox only reads + generates patches. Commit/push from cmd on Krzysztof's side.
12. **MS Kinect SDK 1.8 does NOT work on Windows 11** (Code 39 + Bad Image — `kinectcamera.sys` WDF coinstaller `WdfCoInstaller01009.dll` is KMDF 1.9 from 2010, incompatible with Win 11 KMDF runtime). We use libfreenect over libusbK driver (Zadig-installed) instead.
13. **VS2026 requires "Desktop development with C++" workload**. Sanity check: `where cl`.
14. **VS2026 install can be in incomplete state** — `vswhere -all -prerelease` shows `"isComplete": false, "state": 13`. Configure may pass; `cmake --build` fails with "could not find specified instance of Visual Studio". Fix: VS Installer → Repair on Community 2026.
15. **CMake configure cache locks generator** — switching generator string requires `rmdir /S /Q build && mkdir build`.
16. **Kinect 1414 vs 1473 on Windows 11**: 1414 LED blinks in a loop (USB enum loop, suspected xHCI quirk). 1473 stable — use 1473.
17. **`freenect-camtest.exe` is infinite loop** — terminate with Ctrl+C. First ~10 packets are typically "Lost"/"Invalid magic" at USB sync startup; libfreenect resyncs then runs clean.
18. **libfreenect upstream `#include <unistd.h>` unconditionally** on Windows. MSVC ships none. Our shim provides `usleep` + `sleep`. PRIVATE include FIRST — don't reorder.
19. **libfreenect's custom `cmake_modules/FindThreads.cmake` looks for `pthreadVC2.lib`** but vcpkg pthreads4w installs `pthreadVC3.lib`. We don't use libfreenect's CMakeLists at all — inline our own target. Apply only if someone switches to `add_subdirectory(third_party/libfreenect)` (don't).
20. **GitHub issues numeric drift**: don't trust old issue titles for stage numbering. Old #1/#2/#3 are closed. ROADMAP.md is now single source of truth.
21. **Default `gh pr create` autonumbers PR title from branch slug** if no commit message body — produces lowercase mash. Use `--fill` with capitalized commit message or `--title` explicitly.
22. **`--log_level` is `int32`, not a string.** Values: `0=error, 1=warning, 2=info, 3=debug`. `--log_level=info` rejected.
23. **XELOG* in xenia is fmt-style (`{}`), NOT printf-style (`%s`).** `%s` becomes literal in log. Pass `std::string` directly; no `.c_str()` needed.
24. **`build/version.h` must exist before xenia-base compiles** — defines `XE_BUILD_BRANCH`, `XE_BUILD_COMMIT`, `XE_BUILD_COMMIT_SHORT`, `XE_BUILD_DATE`. NO generation rule in any CMakeLists. Quick-fix recipe (cmd):
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
    Proper fix backlog item: add `src/xenia/version.h.in` + `configure_file(... ${CMAKE_BINARY_DIR}/version.h)`.
25. **Vulkan SDK is a hard build dependency.** `tools/build/compile_shader_spirv.py` calls `glslangValidator`/`spirv-opt`/`spirv-dis` via `VULKAN_SDK` env. Without it: `FileNotFoundError [WinError 2]` on `xenia-ui-vulkan-spirv-shaders`. Install LunarG SDK from https://vulkan.lunarg.com/, restart cmd, reconfigure CMake.
26. **Vulkan GPU backend has rendering regressions on Win 11** — text/UI artifacts when `gpu = "any"` defaults to Vulkan. Workaround: pin `gpu = "d3d12"` in `xenia-canary.config.toml [GPU]` section, or pass `--gpu=d3d12`.
27. **PR-from-feature-branch is NOT PR-from-main.** If you `git checkout -b new-branch` while still on a feature branch, the new branch inherits both your unstaged edits AND the parent feature branch's commits. Recovery: (a) merge parent feature PR via Squash-and-merge, (b) `git fetch origin && git rebase origin/main` (auto-skips already-applied commits), (c) `git push --force-with-lease`. Lesson: always `git checkout -b X main`, even if you have to stash first.
28. **`git stash push -- <path>` is path-scoped** — stashes only that file. Pair with `git stash pop` after rebase / branch switch. `git stash pop` works across fast-forward main pull.
29. **`.git/config` can rot to "fatal: bad config line N"** — typically a stray whitespace line. Doesn't block git from cmd, but blocks sandbox-side `git diff`. Fix: open in editor, delete line N. Or skip sandbox git entirely.
30. **`--nui_telemetry` is the diagnostic firehose** — gates every XAM NUI export entry log. Default off. Combine with `--log_level=3` for XELOGD lines (e.g. fake T-pose emit). Note: config-dump `nui_telemetry` value reflects on-disk TOML BEFORE CLI override — `--nui_telemetry=true` works even if dump shows `false`.
31. **`Build:` line in `xenia.log` is stale until `build/version.h` is regenerated.** Gotcha #24 again. To verify what's actually in the binary, look at runtime symptoms (`caps=0x7` for M4, `[nui]` log lines for M5) — not the `Build:` line.
32. **clang-format-20.1.7 can segfault on the lint check** for files with CRLF line endings + Unicode em-dash `—` content patterns. Symptom: `PLEASE submit a bug report to https://github.com/llvm/llvm-project/issues/`. CI lint may report as `1+ diffs` error. Workaround: write files with ASCII art only (use `--` not `—`), avoid `clang-format -i` on mixed-encoding files. Re-run CI by `git commit --amend --no-edit && git push --force-with-lease`.
33. **Python heredoc writes can leave trailing NUL bytes** — `\x00\x00` at EOF — which both clang-format and CI lint reject as `1+ diffs`. After every Python rewrite, verify with `tail -3 <file> | cat -A`; if you see `^@^@` strip with `raw.rstrip(b"\x00 \t\r\n") + b"\r\n"`.
34. **PR description (`No description provided.`) only appears AFTER `gh pr create`.** Before the PR exists, no description box and no pencil icon. If `gh pr create --body-file <path>` errors with "file not found", PR is NOT yet created — fix file path and re-run, OR open with `--fill` and edit afterward via `gh pr edit <num> --body-file <path>` or web UI (hover first comment, three-dot menu → Edit).
35. **Required CI checks may report "Expected — Waiting for status to be reported" forever** if workflow never starts (e.g. `paths:` filter excludes changed files) OR if it reports a check name that doesn't match the required-check string in branch protection. For docs-only PRs touching only `HANDOFF.md`, **bypass via "Merge without waiting for requirements to be met" checkbox is the standard fix** — solo-dev branch protection has bypass enabled per gotcha #7. For source-code PRs, check Actions tab.
36. **Kinect Adventures internal codename is "Springfield"** (per disc paths `D:\SpringfieldGame\...` and `D:\Binaries\EpicInternal.txt`). NOT The Simpsons Game. KA was built on Unreal Engine 3 by Good Science Studio with Epic Games' UE3 toolchain. When inspecting xenia logs do NOT assume disc-path prefix names the game; check `Title ID:` and `Title name:` instead.
37. **KA imports only `xboxkrnl` + `xam.xex`, not `xnatal.xex` / `xnauinput.xex`.** Launch-era Kinect titles built against the original NUI XDK have all NUI runtime code linked into XAM itself. Later titles (Dance Central 2/3, Kinect Sports Season Two, Star Wars Kinect) require those separate runtime DLLs. The "drop xnatal.xex on RGH" advice from forums applies to those later titles, not to KA.
38. **Kernel exports without `_entry` shim get `nullptr` trampoline AND the runtime warning never fires.** Code in `xex_module.cc:1330` does `XELOGW("...is unimplemented!", ...)` only when `kernel_export == nullptr` — entries listed in `*_table.inc` without an `_entry` get a non-null `kernel_export` with null `function_data.trampoline`, which silently falls through. Only signal: `!! FunctionName` marker in title's import-table dump at load time. Audit: `grep -E "^   F .* !! [A-Z]" xenia.log`.

## Communication style preferences

- Polish.
- Concise. Krzysztof dislikes long bullet lists when prose works.
- Technical, direct, no fluff or excessive apologies.
- Krzysztof runs git / cmd / GitHub UI manually — Claude prepares files, scripts, commit blocks, and waits.
- When CI is the gating factor, hand off and stop generating new code until results come back.

## First task for the new session

1. **Sanity confirm M5 still healthy on a fresh main pull:**
   ```cmd
   cd C:\Users\kjani\Kinectix
   git fetch origin && git checkout main && git pull
   git submodule update --init --recursive
   cmake -B build -DKINECTIX_NUI_FREENECT=ON
   cmake --build build --config Release --target xenia-app
   build\bin\Windows\Release\xenia_canary.exe --nui_backend=freenect --gpu=d3d12 --log_level=3 --nui_telemetry=true <KA-default.xex>
   ```
   Expected (all confirmed in PR #10 testing):
   - `freenect: backend ready (depth+color streaming).`
   - `NUI: kinectix: backend=freenect connected=yes caps=0x7`
   - `[nui] XamNuiGetDeviceStatus → status=0x44 backend_connected=true`
   - `[nui] broadcasting kXNotificationSystemNUIHardwareStatusChanged(0x44) ...`
   - `[nui] broadcasting kXNotificationSystemNUIUIApproach(1) ...`
   - `[nui] XamShowNuiHardwareRequiredUI()` ← THIS IS THE GATE we still don't fully understand.
   - On-screen: KA renders "Is anybody there? Please stand in the sensor's view, or connect a controller and press any button." then idles.

2. **Stage 4 M5.4 — process Krzysztof's IDA / Ghidra findings on KA `default.xex`.** When he reports back, we want:
   - Cross-references to `XamShowNuiHardwareRequiredUI` (xam ordinal `0x47D` = decimal 1149). What function calls it?
   - In that function, what conditional branch leads to the call? (This is the gate.)
   - What memory / register / API call provides the value the branch tests?
   - Likely shapes for the answer:
     (a) Another XAM/xboxkrnl import we haven't stubbed → add stub, build, retest.
     (b) A pointer dereference at a fixed virtual address → NUI shared-memory IO region, requires emulating it. Add a `MmMapIoSpace` shim that returns a NuiManager-backed buffer.
     (c) An event / handle with a specific name (e.g. via `NtCreateEvent("NuiSkeletonReady")`) — needs a kernel-side signal from our backend.

3. **Implement M5.5 based on M5.4 findings.** Same pattern as M5: small targeted patch, telemetry sanity log, smoke test on KA, PR with "(partial)" suffix if KA still doesn't fully boot. Each iteration narrows the gap.

4. **Stage 4 M6 — KA gameplay reachable.** Once the calibration screen progresses to main menu, M6 is closed.

5. **Stage 5 decision** — real skeleton tracking. May start in parallel with M5.4 if a different Kinect title (Dance Central, Kinect Sports) is more receptive to current M5 patches.

### Standing collaboration rules

Krzysztof handles commits/push/gh on his own. Sandbox only generates patches. Wait for his cmd output after each action. For files >~15KB **always** use Python heredoc (gotcha #1, #32, #33 — re-confirmed FOUR times in M5 session), never raw Edit/Write. After Python rewrite always verify size + last 5 lines + `tail | cat -A` to catch trailing NULs. For new feature branches always start from `main` (`git checkout -b X main`, gotcha #27).
