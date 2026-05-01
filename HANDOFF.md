# Kinectix — Session Handoff (2026-05-01, after Stage 4 M1.5: libfreenect reads from Kinect 1473)

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
- **Branch model**: `main` (our work) ↔ `canary` (mirror of `upstream/canary_experimental`). The `upstream` remote points to xenia-canary; weekly `canary-sync` workflow fast-forwards `canary`.

## Build system

**CMake** (NOT premake). Tooling:
- Visual Studio **2026** (Windows host build, generator string `"Visual Studio 18 2026"`)
- clang-format **version 20** specifically — CI lint rejects other versions
- `vcpkg` for libfreenect deps (libusb-1.0 / freeglut / pthreads) — local clone at `C:\Users\kjani\vcpkg\`
- libfreenect built standalone at `C:\Users\kjani\libfreenect\` for sanity testing — **M2 will integrate as `third_party/libfreenect` submodule** in Kinectix proper

## Status by stage

| Stage | What | Status |
|---|---|---|
| 0 | Scaffolding (README, ARCHITECTURE, ROADMAP, CONTRIBUTING, STRUCTURE, BUILDING, CI) | ✅ green |
| 1 | NUI module wired into CMake build, null backend default, no runtime change | ✅ green |
| 2 | XAM NUI telemetry tracer (`--nui_telemetry`, XE_NUI_TRACE macro on all 28 exports) | ✅ green, tag `v0.0.2-telemetry` |
| 2.5 | Bootstrap unblock z capture KA — broadcast `XN_SYS_NUI_HARDWARESTATUSCHANGED` + stub `XamUnk2B001` | ✅ merged `60af925e8` (PR #5), **verified** via trace_d/e/f/g/h |
| 3 | Real flatbuffer impl for `xnuirec_reader` (recorded backend) | ⏸ DEPRIORITIZED (real Kinect available) |
| 4 M1.5 | Hardware sanity — `freenect-camtest.exe` reads frames from Kinect 1473 via libusbK | ✅ **DONE 2026-05-01** |
| 4 M2 | Backend scaffold w Kinectix (`third_party/libfreenect` submodule + `src/xenia/hid/nui/freenect/`) | 🚧 next session |
| 4 M3 | Real depth/color frame capture + threaded reader | ⏳ |
| 4 M4 | Fake T-pose skeleton stub w `XamNuiSkeletonGet*` | ⏳ |
| 4 M5 | Notification broadcast `kXNotificationSystemNUISkeletonTrackingStatusChanged` | ⏳ |
| 4 M6 | First test in KA — czy "is anybody there?" → main menu | ⏳ |
| 5 | Real skeleton tracking (NiTE2 / MediaPipe Pose / własny ML) | ⏳ later |

Branch protection on `main`: PR required, status checks (Lint + Build) required, linear history required, no force pushes.

## Files added/modified vs. upstream (current state, no Stage 4 code yet)

- `src/xenia/hid/nui/` — entire NUI module (interface, manager, null backend, recorded backend, constants)
- `src/xenia/hid/nui/CMakeLists.txt` — module build
- `src/xenia/hid/nui/recorded/xnuirec_reader.cc` — STUB (`Open()` always false)
- `src/xenia/hid/nui/recorded/xnuirec.fbs` — flatbuffer schema for `.xnuirec` recording format
- `src/xenia/CMakeLists.txt` — `add_subdirectory(hid/nui)`
- `src/xenia/app/CMakeLists.txt` — link `xenia-hid-nui` to `xenia-app`
- `src/xenia/emulator.cc` — `NuiManager::Setup/Shutdown` around `input_system_->Setup()`
- `src/xenia/kernel/xam/xam_nui.cc` — Stage 2 + 2.5 (telemetry + broadcast)
- `src/xenia/kernel/xam/apps/xam_app.cc` — Stage 2.5 stub case `0x0002B001`
- `tools/nui-trace/parser.py` + `README.md` — analyzer logów
- `BUILDING.md` — telemetry usage section
- `.github/workflows/canary-sync.yml` — weekly cron

## Cvars added

```
--nui_backend=none|recorded|freenect|freenect2|mediapipe   (default: none)
--nui_record_path=<path>                                   (only for recorded)
--nui_telemetry                                            (default: false)
```

## Stage 2.5 verification (2026-05-01) — recap

Traces d/e/f/g/h: tylko `XamNuiGetDeviceStatus` (1×) + `XamShowNuiTroubleshooterUI` (per X button press) wywoływane przez tytuł. Visual progression: bootstrap → "stand before sensor" → "IS ANYBODY THERE? Please stand in the sensor's view, OR connect a controller and press any button" + `(X) launch Kinect Tuner / (Y) disable auto-tilt`. Gamepad fallback **fake** — A/B/START/BACK ignored. X button → `XamShowNuiTroubleshooterUI` modal dialog (already implemented, no-op gate). Title enforces `XEX_SYSTEM_SKELETAL_TRACKING_REQUIRED`. **Stage 4 (real Kinect) is mandatory** to leave that screen.

Side-finding: `XamVoiceSetMicArrayIdleUsers` (ordinal `0x48C` / 1164) entries in `xam_table.inc:960` ale brak `DECLARE_XAM_EXPORT1` w `xam_voice.cc`. Title gracefully ignores NULL — does NOT block. Side-PR for stub'y w pendingach.

## Stage 4 progress so far (2026-05-01)

**Hardware**: User has Kinect v1 models **1414** + **1473**, plus **USB power adapter**. Tests show:
- **1473**: works — LED stable green when libfreenect opens device, reads depth + color frames cleanly via libusbK driver after Zadig swap.
- **1414**: blinks green continuously (USB enumeration loop, suspected xHCI quirk on Win 11). **Skip 1414 — use 1473.**

**Driver pivot — Win 11 incompatibility with MS Kinect SDK 1.8** (gotcha #11):
- `kinectcamera.sys` v1.6.0.476 fails to load on Win 11 with **Code 39 + "Bad Image"** — root cause: WDF coinstaller `WdfCoInstaller01009.dll` (KMDF 1.9, 2010) niekompatybilny z Win 11 KMDF runtime.
- **Done**: uninstalled MS Kinect SDK 1.8 + Toolkit + Speech Platform Runtime. Used **Zadig** to replace driver binding for 3 Kinect device entries (NUI Camera/Audio/Motor) with **libusbK**. Device Manager: 3× libusbK USB Devices, no yellow exclamation marks.
- **Trade-off**: lost MS skeleton tracking out-of-the-box. Stage 5 dorobi (NiTE2 / MediaPipe Pose / własny ML — decision after M6).

**libfreenect built standalone** at `C:\Users\kjani\libfreenect\` (HEAD of OpenKinect/libfreenect master, ~2024). vcpkg deps installed at `C:\Users\kjani\vcpkg\installed\x64-windows\`:
- libusb-1.0 (instalowany jako `libusb:x64-windows`)
- freeglut (`freeglut:x64-windows`)
- pthreads (`pthreads:x64-windows@3.0.0` = pthreads4w v3.0.0 — ALE libfreenect's custom `cmake_modules/FindThreads.cmake` go nie znajduje, bo szuka `pthreadVC2.lib` zamiast `pthreadVC3.lib`. Workaround: `BUILD_C_SYNC=OFF` w cmake configure — to eliminuje jedyny moduł libfreenecta wymagający pthreads. Mainline lib + examples i tak działają bez explicit pthreads link, bo Win MSVC podstawia native CreateThread.)

CMake configure command (proven working):
```cmd
cmake .. -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\Users\kjani\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DBUILD_EXAMPLES=ON ^
  -DBUILD_FAKENECT=OFF ^
  -DBUILD_OPENNI2_DRIVER=OFF ^
  -DBUILD_C_SYNC=OFF
cmake --build . --config Release
```

Build output:
- `build\lib\Release\freenect.dll` ✓ (200 KB)
- `build\lib\Release\freenect.lib` ✓ (220 KB static)
- `build\lib\Release\libusb-1.0.dll` ✓ (153 KB)
- `build\bin\Release\freenect-camtest.exe` ✓ (141 KB)
- `build\bin\Release\freenect-wavrecord.exe` ✓ (155 KB)
- (no `freenect-glview.exe` — wycięte w newer libfreenec­cie or OpenGL example deps brakują; nie potrzebujemy go)

`freenect-camtest.exe` confirmed (M1.5):
```
[Stream 70] Negotiated packet size 1920    ← depth stream up
[Stream 80] Negotiated packet size 1920    ← color stream up
Received depth frame at 979547179
... initial ~10 lost packets, libfreenect resync ...
Received depth frame at <continuous timestamps, ~30 fps>
```
First ~10 lost packets are normal (USB isochronous endpoint sync); after `[Stream 70] Lost too many packets, resyncing... write_register: 0x0047 <= 0x00`, stable run. Camtest is **infinite loop** — must Ctrl+C to stop (gotcha #15).

## Immediate context (where we are RIGHT NOW)

**Stage 4 M1.5 ✅ done**. Library compile + sanity test verified. Next session starts at **Stage 4 M2 — backend scaffold in Kinectix**.

## M2 plan (start of next session) — Opcja A (third_party submodule)

Decision: vendor libfreenect as `third_party/libfreenect` git submodule. Rationale: contributor `git clone --recurse-submodules && cmake .. && build` → working Kinect support out-of-the-box, zero local libfreenect setup. Reproducible builds. Pin specific commit. libusb-1.0 separately as either second submodule (`third_party/libusb`) or vcpkg manifest dependency in Kinectix root (`vcpkg.json`).

**Concrete steps for M2** (do them in order):

1. **Get HEAD commit hash** from local libfreenect build:
   ```cmd
   cd C:\Users\kjani\libfreenect
   git rev-parse HEAD
   ```
   Pin submodule to this commit (so we don't take random updates from upstream).

2. **Add libfreenect as submodule** in Kinectix:
   ```cmd
   cd C:\Users\kjani\Kinectix
   git submodule add https://github.com/OpenKinect/libfreenect.git third_party/libfreenect
   git -C third_party/libfreenect checkout <commit-hash-from-step-1>
   ```

3. **libusb dependency** — decision: vcpkg manifest mode (lighter than yet another submodule):
   - Create/extend `vcpkg.json` in Kinectix root with `"libusb"` as dependency
   - Kinectix root CMake: `find_package(libusb REQUIRED)` (vcpkg toolchain handles install transparently)
   - Document in BUILDING.md: contributor needs `vcpkg` available; CI runners on Windows already have it

4. **Wrap libfreenect build** in Kinectix CMake:
   - `third_party/libfreenect` ma własny `CMakeLists.txt` — możemy `add_subdirectory(third_party/libfreenect EXCLUDE_FROM_ALL)` z preset opcjami: `BUILD_C_SYNC=OFF`, `BUILD_EXAMPLES=OFF`, `BUILD_FAKENECT=OFF`, `BUILD_OPENNI2_DRIVER=OFF`, `BUILD_AUDIO=OFF`
   - Tworzy target `freenect` (shared) i `freenectstatic` (static). Linkujemy `freenect` do naszego backendu.
   - Hide upstream warnings: `target_compile_options(freenect PRIVATE /W0)` lub equivalent.

5. **Backend module** `src/xenia/hid/nui/freenect/`:
   - `nui_freenect.h` — `class FreenectBackend : public INuiBackend` (declaration)
   - `nui_freenect.cc` — implementation:
     - Setup: `freenect_init()`, `freenect_open_device(dev_idx=0)`, register depth + video callbacks
     - Threaded reader: dedicated `std::thread` running `freenect_process_events()` loop
     - Frame buffer: lock-free triple-buffered for depth (320×240 11-bit) i color (640×480 RGB888)
     - Shutdown: `freenect_close_device()`, `freenect_shutdown()`, join reader thread
   - `CMakeLists.txt` — defines `xenia-hid-nui-freenect` library, links `freenect` target

6. **Wire backend into NuiManager** in `src/xenia/hid/nui/nui_manager.cc`:
   - Branch on `--nui_backend=freenect` → instantiate `FreenectBackend` instead of null backend
   - Already done conditionally for `recorded`; add `freenect` arm.

7. **Fake T-pose skeleton stub** w `XamNuiSkeletonGet*` (M4 — może być w tym samym PR, może osobnym):
   - Hardcoded skeleton frame: 20 jointów w T-pose at `(0, 0, 2.5m)` from sensor (centered, 2.5m away)
   - Joint positions w canonical Xbox 360 NUI skeleton format (sprawdzić exact struct w xam_nui.cc)
   - Tytuł powinien zaakceptować jako "skeleton tracked" → wyjść z "is anybody there?"

8. **Notification broadcast** w `XamNuiSkeletonGetNextFrame` (M5):
   - First call → broadcast `kXNotificationSystemNUISkeletonTrackingStatusChanged` (sprawdzić ID — najpewniej `0x0006001A` lub `0x0006001B`, zerknąć w `notify_listener.h` lub kernel headers) z arg = "skeleton 0 tracked"

9. **First test M6**: Capture trace_i with new build, KA, sprawdź czy "is anybody there?" → main menu.

## Parser tool (gotowy)

**`tools/nui-trace/parser.py`** (PR #4, commit `4ce9163c1` + `1c69b6225`) — stdlib-only, regex `\[nui\]\s+(\w+)\(`, CLI: `--trace-a`, `--trace-b`, `--out-dir`, `--init-count`, `--seq-count`. Produkuje `summary.txt`, `init_sequence.txt`, `diff_a_vs_b.txt`, `mermaid_seq.md`.

## Pending (longer-horizon)

1. **Stage 4 M2-M6** (priority, next session — see "M2 plan" above).
2. **Stage 5** — real skeleton tracking. Decision criteria after M6: jeśli fake T-pose przepuści tytuł do main menu ale nie do gameplay (gameplay wymaga real gestures/poses), wybieramy real tracker:
   - **MediaPipe Pose** — najmniej overhead, RGB-based, 33 jointów → mapping do 20. Single-person.
   - **NiTE2** — w 2026 hard to find, archived downloads only, license uncertain.
   - **Custom ML** — high effort, random forest on depth (oryginalna metoda Microsoft 2011).
3. **Side-fix PR**: stub `XamVoiceSetMicArrayIdleUsers` (0x48C), `XamVoiceMuteMicArray` (0x48D), `XamVoiceGetMicArrayUnderrunStatus` (0x48E), `XamVoiceGetMicArrayAudioEx` (0x490), `XamVoiceDisableMicArray` (0x491). Entries exist in `xam_table.inc` ale brak `DECLARE_XAM_EXPORT1` w `xam_voice.cc`. KA tolerates missing, but cleaner stack.
4. Open prepared issues in GitHub:
   - `[nui] Stage 0/2: telemetry capture per reference title`
   - `[nui] Stage 4: libfreenect backend (Kinect v1, Win11 + libusbK driver)`
   - `[nui] Stage 5: real skeleton tracking decision`
   - `[infra] Win11 + MS Kinect SDK 1.8 incompatibility documented` (issue draft prepared at `C:\Users\kjani\issue-win11-mssdk-incompat.md`, ~6 KB — `gh issue create --body-file ...`)
5. First manual run of `canary-sync` workflow to verify it works.
6. Stage 3 (recorded backend) parked — pick up after Stage 4/5 if useful for CI fixture replay.

## Critical gotchas (so the new session doesn't re-learn them painfully)

1. **Write tool truncates files >~16KB silently. Edit tool ALSO truncates >~17KB silently** (potwierdzone na `xam_nui.cc` i `xam_app.cc`). Dla dużych plików: bash heredocs przez sandbox, generuj patch w sandbox via `diff -u`, normalizuj CRLF→LF, użyj `git apply --ignore-whitespace --whitespace=fix`. Zawsze pełna `Read` po Edit.
2. **xenia-canary uses CMake, not premake.**
3. **Branch ambiguity**: never name a local branch `upstream` — conflicts with the conventional remote.
4. **clang-format-20 strict**: `pip install clang-format==20.1.7 --break-system-packages`, `--style=file:.clang-format -i` to fix, `--dry-run --Werror` to verify.
5. **Argument printing in Xenia shims**: `ParamBase<T>` (used for `dword_t`, etc.) ma `operator T()` — print via `static_cast<uint32_t>(arg)`. `PointerParam` / `PrimitivePointerParam` mają `.guest_address()`.
6. **User uses cmd.exe, not Git Bash**: `mkdir`, `copy /Y`, `xcopy /E /I /Y`.
7. **GitHub branch protection UI**: "Include administrators" → "Do not allow bypassing" — for solo-dev leave OFF.
8. **`gh pr create` z forka domyślnie celuje w upstream parent**. Zawsze `gh repo set-default RedMadKnight/Kinectix`.
9. **`gh run list --workflow "Windows (x86-64)"` zwraca 0** — używać `gh run list --workflow Orchestrator --branch main`.
10. **Nie commitować z sandboxa** — sandbox tylko czyta + generuje patche. Commit/push robi user w cmd.
11. **MS Kinect SDK 1.8 NIE działa na Windows 11** (Code 39 + Bad Image). Idziemy libfreenect + libusbK przez Zadig.
12. **VS2026 wymaga "Desktop development with C++" workload** — bez niego cmake widzi VS18 generator w `--help` ale rzuca `could not find any instance` przy configure. Sanity check: `where cl`.
13. **CMake configure cache locks generator** — przy zmianie generator string'a wymagane `rmdir /S /Q build && mkdir build`.
14. **Kinect 1414 vs 1473 na Windows 11**: 1414 LED miga w pętli (USB enum loop, prawdopodobnie xHCI quirk). 1473 stable — używać. **Odepnij 1414 jeśli nie jest potrzebny** — minimum interferencji USB.
15. **`freenect-camtest.exe` to infinite loop** (czyta klatki forever) — kończyć Ctrl+C. Pierwsze ~10 packetów zwykle "Lost"/"Invalid magic" przy USB sync startup; libfreenect resyncuje (`Lost too many packets, resyncing...`) i wtedy clean run. To NORMAL behavior.
16. **libfreenect's custom `cmake_modules/FindThreads.cmake` szuka pthreadVC2.lib**, vcpkg pthreads instaluje pthreadVC3.lib. Workaround: `-DBUILD_C_SYNC=OFF` (jedyny moduł żądający pthreads). Examples + main lib działają bez pthreads — Win MSVC podstawia native threads transparently. Nie próbować naprawić FindThreads — strata czasu.
17. **`-DBUILD_AUDIO=OFF` ignored przez nowsze libfreenect** (warning: "Manually-specified variables were not used by the project"). Audio domyślnie OFF i tak — opcja została wycięta z newer CMakeLists. Pomijać flag.

## Communication style preferences

- Polish.
- Concise. User dislikes long bullet lists when prose works.
- Technical, direct, no fluff or excessive apologies.
- User runs git / cmd / GitHub UI manually — Claude prepares files, scripts, commit blocks, and waits.
- When CI is the gating factor, hand off and stop generating new code until results come back.

## First task for the new session

1. **Confirm M1.5 still works** — sanity: `cd C:\Users\kjani\libfreenect\build\bin\Release && freenect-camtest.exe`, ~2s patrz na stream startup → "Received depth frame" linie → Ctrl+C. Kinect 1473 LED stałe zielone w trakcie. Jeśli OK → proceed do M2.
2. **Get libfreenect HEAD commit**: `cd C:\Users\kjani\libfreenect && git rev-parse HEAD` → zapisz hash.
3. **Stage 4 M2 — backend scaffold w Kinectix**: follow "M2 plan" sekcja powyżej. Steps 1-7. Submodule + vcpkg manifest dla libusb + `add_subdirectory(third_party/libfreenect)` + nowy `src/xenia/hid/nui/freenect/` module + wpięcie w NuiManager.
4. **M3-M6** w kolejnych iteracjach — frame capture, fake T-pose, notification broadcast, KA test.
5. Po M6 — **decyzja Stage 5** (real skeleton tracking).

Krzysztof prowadzi commity/push/gh sam. Sandbox tylko generuje patche. Czekaj na jego cmd output po każdej akcji.
