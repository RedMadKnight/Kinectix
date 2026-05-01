# Kinectix — Session Handoff (2026-05-01, after Stage 2.5 verified, mid-Stage 4 setup)

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

**CMake** (NOT premake — xenia-canary uses CMake despite stale references in some old docs). Tooling:
- Visual Studio **2026** (Windows host build, generator string `"Visual Studio 18 2026"`)
- clang-format **version 20** specifically — CI lint will reject other versions
- `vcpkg` for libfreenect deps (libusb / glfw3 / glew) — local clone at `C:\Users\kjani\vcpkg\`
- libfreenect built from source at `C:\Users\kjani\libfreenect\` (NOT vendored into Kinectix yet — Stage 4 M2 will integrate as third_party submodule or `find_package`)

## Status by stage

| Stage | What | Status |
|---|---|---|
| 0 | Scaffolding (README, ARCHITECTURE, ROADMAP, CONTRIBUTING, STRUCTURE, BUILDING, CI) | ✅ green |
| 1 | NUI module wired into CMake build, null backend default, no runtime change | ✅ green |
| 2 | XAM NUI telemetry tracer (`--nui_telemetry`, XE_NUI_TRACE macro on all 28 exports) | ✅ green, tag `v0.0.2-telemetry` |
| 2.5 | Bootstrap unblock z capture KA — broadcast `XN_SYS_NUI_HARDWARESTATUSCHANGED` + stub `XamUnk2B001` | ✅ merged `60af925e8` (PR #5), **verified** via trace_d/e/f/g/h |
| 3 | Real flatbuffer impl for `xnuirec_reader` (recorded backend) | ⏸ **DEPRIORITIZED** — see "Stage decision" below |
| 4 | libfreenect backend (Kinect v1 over USB) | 🚧 in progress — driver pivot done, libfreenect compile pending |
| 5 | Real skeleton tracking (NiTE2 / MediaPipe Pose / own ML) | ⏳ later |

Branch protection on `main`: PR required, status checks (Lint + Build) required, linear history required, no force pushes.

## Files added/modified vs. upstream

- `src/xenia/hid/nui/` — entire NUI module (interface, manager, null backend, recorded backend, constants)
- `src/xenia/hid/nui/CMakeLists.txt` — module build
- `src/xenia/hid/nui/recorded/xnuirec_reader.cc` — STUB (`Open()` always false)
- `src/xenia/hid/nui/recorded/xnuirec.fbs` — flatbuffer schema for `.xnuirec` recording format
- `src/xenia/CMakeLists.txt` — `add_subdirectory(hid/nui)`
- `src/xenia/app/CMakeLists.txt` — link `xenia-hid-nui` to `xenia-app`
- `src/xenia/emulator.cc` — `NuiManager::Setup/Shutdown` around `input_system_->Setup()`
- `src/xenia/kernel/xam/xam_nui.cc` — Stage 2: added `DEFINE_bool(nui_telemetry)` and `XE_NUI_TRACE` macro on all 28 exports; Stage 2.5: `MaybeBroadcastNuiHardwareStatus()` helper (one-shot atomic, broadcasts `kXNotificationSystemNUIHardwareStatusChanged=0x00060019` z arg=1) wywoływany w `XamNuiGetDeviceStatus_entry` gdy `allow_nui_initialization=true`
- `src/xenia/kernel/xam/apps/xam_app.cc` — Stage 2.5: stub case `0x0002B001` (XamUnk2B001) zwraca `X_E_SUCCESS`
- `tools/nui-trace/parser.py` + `README.md` — analyzer logów (summary/init_sequence/diff/mermaid)
- `BUILDING.md` — telemetry usage section
- `.github/workflows/canary-sync.yml` — weekly cron

## Cvars added

```
--nui_backend=none|recorded|freenect|freenect2|mediapipe   (default: none)
--nui_record_path=<path>                                   (only for recorded)
--nui_telemetry                                            (default: false)
```

## Stage 2.5 — verification (2026-05-01, after merge of `60af925e8`)

Captured **trace_d/e/f/g/h** (KA, `--allow_nui_initialization=true`, `--nui_telemetry`, post-fix builds). Parser results in `out/stage25*/`. Findings:

- `XamNuiGetDeviceStatus` called **once** at boot → broadcast fires (linia ~1547 every trace).
- After broadcast title spawns 30+ XThreads, loads `\SpringfieldGame\CookedXenon`, `\Video`, `\Demos`, `\Flash\gbl`, `\content\…\4D5308ED\00000002`, audio (`AudioSystem::RegisterClient`), socket bind. **Title progresses past initial XAM gate.**
- Visual progression observed: bootstrap → "stand before sensor" → **"IS ANYBODY THERE? Please stand in the sensor's view, OR connect a controller and press any button"** prompt with `(X) launch Kinect Tuner / (Y) disable auto-tilt` overlay.
- Gamepad fallback tooltip is **misleading**: A/B/START/BACK on this screen wywołują **żadnych** [nui] shimów. Title silently ignores those buttons.
- Only buttons actively wired:
  - **X (button)** → `XamShowNuiTroubleshooterUI(user_index=0xFF, tracking_id=0, flags=0)` — modal XAM dialog "The game has indicated there is a problem with NUI (Kinect)". After OK title returns to "is anybody there?"; **does not unblock**.
  - **Y (button)** → "disable auto-tilt" (no NUI shim, separate non-tracerowany path).
- `XamShowNuiTroubleshooterUI` already fully implemented in `xam_nui.cc:422-477` (modal dialog, `fence.Wait()`); not a gate to fix.
- `XamUnk2B001` (case `0x0002B001` w `xam_app.cc`) **never invoked** — title dispatches different message channels post-broadcast. Stub stays as defensive guard.
- Side-finding: `XamVoiceSetMicArrayIdleUsers` (ordinal `0x48C` / 1164) jest w `xam_table.inc:960` ale **brak DECLARE_XAM_EXPORT1** w `xam_voice.cc` (~20× unimplemented errors per session). Title gracefully ignores NULL — **does NOT block** progression. TODO: stub it (and 0x48D/E/0/1 mic array peers) in a separate small PR; nice-to-have, not blocker.

**Conclusion**: title enforces `XEX_SYSTEM_SKELETAL_TRACKING_REQUIRED`, gamepad fallback is fake, **Stage 3 (recorded backend with fake T-pose) lub Stage 4 (real Kinect) is mandatory** to leave the "is anybody there?" screen.

## Stage decision (2026-05-01)

User has **real Kinect v1 hardware**: models **1414** (older) and **1473** (post-2012 rev), plus **USB power adapter**. Decision: skip Stage 3 (recorded backend), go straight to Stage 4 (real Kinect via libfreenect). Reasoning:

- Real hardware = real gameplay (not just main menu); validates implementation end-to-end.
- Stage 3 fake T-pose would only get title to atrium/menu, not gameplay (game requires actual gestures).
- Stage 3 still useful for CI fixture replay later — but lower priority. Park it.

**Driver pivot — Win11 incompatibility with MS Kinect SDK 1.8**:

- Initially planned to use **Microsoft Kinect for Windows SDK 1.8** (out-of-the-box skeleton tracking, 1:1 mapping pod oryginalne 360 NUI API).
- **MS SDK 1.8 driver `kinectcamera.sys` v1.6.0.476 fails to load on Windows 11** with **Code 39 + "Bad Image" error**. Root cause: WDF coinstaller `WdfCoInstaller01009.dll` (KMDF 1.9 z 2010) niekompatybilny z runtime'em WDF w Win 11. Microsoft deprecated SDK 1.8 w 2014, brak fixu od strony MS.
- **Pivot**: **libfreenect + libusbK** (przez Zadig). Kinect 1473 + zasilacz USB; Zadig zamienia bind dla 3 device'ów (Camera/Audio/Motor) z `kinectcamera.sys` na `libusbK`. **Done** — Device Manager pokazuje 3× libusbK USB Devices bez wykrzykników, LED Kinecta migocze (normalny stan bez aktywnego owner'a USB; ustabilizuje się gdy Glview/backend otworzy device).
- Trade-off: tracimy out-of-the-box skeleton tracking. **Stage 5** dorobi (NiTE2 / MediaPipe Pose / własny ML — decyzja po sanity-check Glview).
- 1414 migocze cały czas (USB enumeration loop) — odłożone, 1473 wystarcza do testów Stage 4.

## Immediate context (where we are RIGHT NOW)

User czeka aż **Visual Studio 2026 doinstaluje "Desktop development with C++" workload** (~5-10 min, ~6 GB). Powód: cmake bundled w VS2026 widzi generator `"Visual Studio 18 2026"`, ale przy próbie configure libfreenect zwraca `could not find any instance of Visual Studio` — bo brak MSVC compilera + Windows SDK po stronie VS2026 install. Po reinstalacji workloadu sequencja:

```cmd
:: 1. Sanity check w "x64 Native Tools Command Prompt for VS 2026":
where cl
:: powinno pokazać cl.exe w C:\Program Files\Microsoft Visual Studio\2026\...\bin\Hostx64\x64\

:: 2. vcpkg deps (jeśli nie zrobione)
cd C:\Users\kjani\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install libusb:x64-windows glfw3:x64-windows glew:x64-windows

:: 3. Build libfreenect
cd C:\Users\kjani\libfreenect
rmdir /S /Q build
mkdir build
cd build
cmake .. -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\Users\kjani\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DBUILD_EXAMPLES=ON -DBUILD_AUDIO=OFF -DBUILD_FAKENECT=OFF ^
  -DBUILD_OPENNI2_DRIVER=OFF -DBUILD_C_SYNC=ON
cmake --build . --config Release --target freenect-glview

:: 4. Sanity test
bin\Release\freenect-glview.exe
```

Spodziewane: okno z 2 panelami (depth + RGB), LED Kinecta przechodzi w stałe zielone gdy Glview chwyci device.

## Next milestones (Stage 4)

- **M1.5** (current): hardware sanity — Glview pokazuje depth+RGB streams z 1473.
- **M2**: backend scaffold — `src/xenia/hid/nui/freenect/{nui_freenect.cc, nui_freenect.h}`, integracja libfreenect jako third_party submodule (lub vcpkg manifest mode w Kinectix CMake), wpiąć cvar `--nui_backend=freenect`.
- **M3**: backend implementation — `freenect_init`/`freenect_open_device`/`freenect_set_depth_callback`/`freenect_set_video_callback`, threaded reader, frame buffer (depth: 11-bit 320x240 lub 640x480, color: RGB 640x480).
- **M4**: fake T-pose skeleton stub — żeby `XamNuiSkeletonGet*` shimy zwracały coś sensownego do tytułu, even przed Stage 5 real skeleton inference. To powinno wystarczyć żeby tytuł wyszedł z "is anybody there?" do main menu (gameplay wymaga real skeleton — Stage 5).
- **M5**: notification broadcast — `kXNotificationSystemNUISkeletonTrackingStatusChanged` (sprawdzić exact ID — `0x0006001A`?) gdy fake skeleton "tracked".
- **M6**: first test in KA — czy "is anybody there?" wyjdzie do main menu / atrium z fake skeleton.

Po M6 → Stage 5 (real skeleton tracking).

## Parser tool (gotowy)

**`tools/nui-trace/parser.py`** (PR #4, commit `4ce9163c1` + fix-up `1c69b6225`) — stdlib-only, regex `\[nui\]\s+(\w+)\(`, CLI: `--trace-a` (required), `--trace-b` (optional → diff), `--out-dir`, `--init-count`, `--seq-count`. Produkuje:

1. `summary.txt` — top functions by call frequency
2. `init_sequence.txt` — first N **unique** calls in order
3. `diff_a_vs_b.txt` — symmetric diff (gdy `--trace-b` podany)
4. `mermaid_seq.md` — sequence diagram

## Pending

1. **Stage 4 M2-M6** — libfreenect backend implementation (priority, in progress).
2. **Stage 5** — real skeleton tracking (decision after M1.5: NiTE2 archive vs MediaPipe Pose vs custom ML).
3. **Side-fix PR**: stub `XamVoiceSetMicArrayIdleUsers` (0x48C), `XamVoiceMuteMicArray` (0x48D), `XamVoiceGetMicArrayUnderrunStatus` (0x48E), `XamVoiceGetMicArrayAudioEx` (0x490), `XamVoiceDisableMicArray` (0x491) — entries exist in `xam_table.inc` but no `DECLARE_XAM_EXPORT1` in `xam_voice.cc`. KA tolerates missing, but cleaner stack.
4. Open prepared issues in GitHub (content w `docs/issues/` if survived):
   - `[nui] Stage 0/2: telemetry capture per reference title` (tracking)
   - `[nui] Stage 4: libfreenect backend (Kinect v1, Win11 + libusbK driver)` — note: ROADMAP terminology differs (Stage 2 in ROADMAP), HANDOFF Stage 4 is the canonical naming for this session continuity
   - `[nui] Stage 5: real skeleton tracking (NiTE2 / MediaPipe / custom ML)`
   - `[infra] Win11 + MS Kinect SDK 1.8 incompatibility documented` (informational, helps future contributors)
5. First manual run of `canary-sync` workflow to verify it works
6. Stage 3 (recorded backend) parked — pick up after Stage 4/5 if useful for CI fixture replay

## Critical gotchas (so the new session doesn't re-learn them painfully)

1. **Write tool truncates files >~16KB silently. Edit tool ALSO truncates >~17KB silently** (potwierdzone na `xam_nui.cc` i `xam_app.cc` w sesji 2026-04-30 — Edit kończy w środku stringa bez błędu, py_compile przechodzi bo Python toleruje bare ident at EOF). Dla dużych plików: (a) bash heredocs przez sandbox, (b) generuj patch w sandbox via `diff -u`, normalizuj CRLF→LF, użyj `git apply --ignore-whitespace --whitespace=fix` w cmd, (c) zawsze pełna `Read` całego pliku po Edit żeby zweryfikować że końcówka jest na miejscu.
2. **xenia-canary uses CMake, not premake.** Initial Stage 1 attempt wrote `premake5.lua` — wrong, deleted, replaced with `CMakeLists.txt` matching sibling modules (`src/xenia/hid/xinput/CMakeLists.txt`).
3. **Branch ambiguity**: never name a local branch `upstream` — conflicts with the conventional `upstream` remote. We renamed to `canary`.
4. **clang-format-20 strict**: install in sandbox via `pip install clang-format==20.1.7 --break-system-packages`, run `--style=file:.clang-format -i` to fix and `--dry-run --Werror` to verify before commit.
5. **Argument printing in Xenia shims**: `ParamBase<T>` (used for `dword_t`, `qword_t`, `int_t`, `unknown_t`) has `operator T()` — print via `static_cast<uint32_t>(arg)` etc. `PointerParam` and `PrimitivePointerParam` have `.guest_address()` returning `uint32_t`.
6. **User uses cmd.exe, not Git Bash**: prefer `mkdir`, `copy /Y`, `xcopy /E /I /Y` over `mkdir -p`, `cp`, etc.
7. **GitHub branch protection UI changed**: "Include administrators" is now "Do not allow bypassing the above settings" — for solo-dev, leave it OFF so you can push fixes when CI hangs.
8. **`gh pr create` z forka domyślnie celuje w upstream parent** (xenia-canary/xenia-canary). Zawsze najpierw `gh repo set-default RedMadKnight/Kinectix` w nowej sesji cmd, inaczej PR ląduje w upstream (PR #986 incydent).
9. **`gh run list --workflow "Windows (x86-64)"` zwraca 0** bo to reusable `workflow_call`. Używać `gh run list --workflow Orchestrator --branch main --limit 5` — to jest top-level workflow który wraps Windows_x86/Linux_x86.
10. **Nie commitować z sandboxa** do działającego repo użytkownika — zostawia .git/HEAD.lock + tmp_obj_* których sandbox nie może usunąć (różne mounty). Sandbox: tylko czytanie + generowanie patchy. Commit/push robi user w cmd.
11. **MS Kinect SDK 1.8 NIE działa na Windows 11** (Code 39 + "Bad Image" w `kinectcamera.sys` driver load — WDF coinstaller `WdfCoInstaller01009.dll` z 2010 niekompatybilny z Win11 KMDF runtime). Microsoft deprecated SDK 1.8 w 2014. **Workaround**: libusbK przez Zadig + libfreenect (open-source, cross-platform, brak skeleton trackingu out-of-the-box — Stage 5 dorabia).
12. **VS2026 wymaga zainstalowanego workloadu "Desktop development with C++"** dla cmake generator `"Visual Studio 18 2026"` — bez niego cmake widzi nazwę generatora w `--help` ale rzuca `could not find any instance of Visual Studio` przy próbie configure. Zawsze sprawdzać `where cl` przed cmake configure.
13. **CMake configure cache keeps generator from previous attempts** — jeśli pierwszy configure wybrał VS17 (bo było default w env), zmiana na VS18 nie zadziała bez `rmdir /S /Q build && mkdir build`. CMakeCache.txt blokuje zmianę generator string'a w istniejącym build dir.
14. **Kinect 1414 vs 1473 na Windows 11**: 1414 miga LED-em w pętli (USB enumeration loop, prawdopodobnie xHCI quirk) — nie używać. 1473 LED stała się stała po podpięciu Glview/backendu — używać do testów Stage 4.

## Communication style preferences

- Polish.
- Concise. User dislikes long bullet lists when prose works.
- Technical, direct, no fluff or excessive apologies.
- User runs git / cmd / GitHub UI manually — Claude prepares files, scripts, commit blocks, and waits.
- When CI is the gating factor, hand off and stop generating new code until results come back.

## First task for the new session

1. **Sprawdź gdzie jesteśmy w Stage 4 setup**: czy `freenect-glview.exe` działa i pokazuje depth+RGB streams z Kinecta 1473.
   - Jeśli **TAK** → ruszaj M2: scaffold `src/xenia/hid/nui/freenect/{nui_freenect.cc, nui_freenect.h}` + CMake integration + cvar `--nui_backend=freenect`.
   - Jeśli **NIE** → diagnostyka libfreenect compile / Glview crash (najczęściej: VS2026 C++ workload missing, `where cl` pokazuje pustkę → reinstall workload; albo Zadig nie wymienił driver dla wszystkich 3 Kinect device'ów → ponowne sweep).
2. Jeśli M2 done → M3 (real depth/color frame capture + threaded reader) → M4 (fake T-pose skeleton stub) → M5 (notification broadcast) → M6 (first KA test).
3. Po Stage 4 done → Stage 5 decision (NiTE2 / MediaPipe / custom ML).

Krzysztof prowadzi commity/push/gh sam. Sandbox tylko generuje patche. Czekaj na jego cmd output po każdej akcji.
