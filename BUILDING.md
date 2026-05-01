# Building Kinectix

Kinectix uses **CMake**. The upstream `xb` premake wrapper from xenia-canary is being phased out — don't use it. Build directly with CMake.

## Quickstart (Windows)

```cmd
git clone --recurse-submodules https://github.com/RedMadKnight/Kinectix.git
cd Kinectix
mkdir build
cd build
cmake .. -G "Visual Studio 18 2026" -A x64
cmake --build . --config Release
```

Output binaries land in `build/bin/Windows/Release/xenia.exe` (the upstream binary name is preserved inside the build for compatibility; releases are distributed as `kinectix-*.zip`).

## Toolchain — Windows (primary platform)

- **Visual Studio 2026 Community / Professional / Enterprise** with the **Desktop development with C++** workload. Without that workload CMake will see the `Visual Studio 18 2026` generator in `--help` but fail at configure with "could not find any instance". Sanity check: `where cl` should resolve.
- **Windows 11 SDK** (any modern revision; we target Windows 10.0.22621.0+).
- **CMake 3.20+** (bundled with VS, or install separately).
- **Python 3.10+** for shader compilation scripts and `tools/nui-trace/parser.py`.
- **clang-format 20.1.7** specifically — CI lint rejects any other version's output:
  ```cmd
  pip install clang-format==20.1.7 --break-system-packages
  clang-format --version   :: must report 20.1.7
  ```
- **Git for Windows** with submodule support.

For exercising the libfreenect backend with real hardware, see also [README.md § Hardware & driver requirements](README.md) — Kinect 1473, USB power adapter, libusbK driver via Zadig, MS Kinect SDK 1.8 must NOT be installed (incompatible with Win 11).

## Toolchain — Linux (default backend only, currently)

The libfreenect backend is Windows-gated for now (Stage 4 M2 onward). On Linux you can still build the null backend and the telemetry tracer:

```bash
sudo apt install -y build-essential cmake git curl python3 clang-format-20
git clone --recurse-submodules https://github.com/RedMadKnight/Kinectix.git
cd Kinectix
cmake -S . -B build -G Ninja
cmake --build build --config Release
```

Linux/macOS hardware support arrives post-Stage-5.

## NUI backend selection

There are **no compile-time NUI build flags** in Kinectix. Backend is selected at runtime:

```
--nui_backend=none      (default — null backend, no Kinect input)
--nui_backend=recorded  (replay a .xnuirec fixture; Stage 3 deprioritized, reader is a stub)
--nui_backend=freenect  (libfreenect over libusbK, Kinect v1 — Stage 4 M2 onward)
--nui_backend=freenect2 (Kinect v2, future)
--nui_backend=mediapipe (webcam + pose estimation, Stage 5 candidate)
--nui_record_path=<path-to-.xnuirec>   (only for --nui_backend=recorded)
```

A backend selected via `--nui_backend` that wasn't compiled into this binary (e.g. selecting `freenect` on a Linux build) falls back to `none` with a warning; it is not a fatal error.

All vendored dependencies (libfreenect, libusb, freenect-msvc-compat shim) live as git submodules under `third_party/`. `git clone --recurse-submodules` pulls everything; if you cloned without that flag, run:

```cmd
git submodule update --init --recursive
```

## Telemetry — capturing real Kinect title call sequences

Stage 2 adds an opt-in tracer that logs every XAM NUI export entry made by a running title. We need this data to know which of the ~28 NUI functions real games actually call, in what order, and with what arguments — that's the input to writing real implementations rather than stubs.

```
--nui_telemetry   (default: off)
```

When on, every entry into a function in `src/xenia/kernel/xam/xam_nui.cc` emits a one-line `XELOGI` trace like:

```
i> [nui] XamNuiGetDeviceStatus(status_ptr=82A40C00)
i> [nui] XamNuiIsDeviceReady()
i> [nui] XamNuiHudSetEngagedTrackingID(id=00000001)
i> [nui] XamNuiSkeletonGetBestSkeletonIndex(unk=0)
```

Cost when off is one global-bool load + branch (predicted not-taken via `XE_UNLIKELY`) — leaving it compiled in for non-Kinect titles is harmless.

### Capturing a trace

1. Build a binary as usual.
2. Run a Kinect title with `--nui_telemetry --log_file=trace.log` (or whatever your usual log routing is).
3. Play through the part of the game that exercises NUI (sensor init, calibration, gameplay, identity prompt — pick one per session, narrower is more useful).
4. Filter the log: `grep "\[nui\]" trace.log > trace.nui.txt`.
5. Attach `trace.nui.txt` to the corresponding telemetry issue (one issue per title, see `Issues` on GitHub).

Note that `XamIsNuiAutomationEnabled` and `XamIsNatalPlaybackEnabled` are tagged `kHighFrequency` upstream — expect those to dominate the log volume. Don't strip them; their cadence is itself signal.

## Build status of the NUI tree

As of Stage 4 M2:

- `xenia-hid-nui` library is built and linked into `xenia-app` on every configuration. At default settings (`--nui_backend=none`) the null backend is installed, exposing no Kinect to the guest, matching upstream behavior.
- `recorded_backend.cc` is built but uses a stub `XnuirecReader` — `Open()` always returns false. The recorded backend is parked while real Kinect support is the priority.
- The `freenect` static library target is built unconditionally on Windows (links against the vendored `libusb` static target). The `xenia-hid-nui-freenect` backend module lands incrementally in Stage 4 M2 — see [ROADMAP.md](ROADMAP.md).

## Common build issues

- **Lint failures on PR.** The `Lint` CI job runs `clang-format-20.1.7` against every changed file (`git-clang-format --commit=origin/canary_experimental --diff`). If your PR has formatting drift:
  ```cmd
  clang-format --style=file -i <changed-files>
  git diff
  ```
  **Always use clang-format 20.1.7 specifically.** Patch versions diverge.
- **CMake configure: "could not find any instance" of Visual Studio 2026.** The "Desktop development with C++" workload is missing or the install is incomplete. Open Visual Studio Installer → Repair on the Community 2026 entry. Verify with:
  ```cmd
  "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -all -prerelease -property isComplete
  ```
  Should return `1`. If `vswhere` reports `isComplete: false`, run Repair and wait for it to finish before retrying configure.
- **CMake configure cache locks the generator.** Switching generator string (e.g. between `Visual Studio 18 2026` and `Ninja`) requires a clean build directory:
  ```cmd
  rmdir /S /Q build && mkdir build && cd build
  cmake .. -G "<new generator>" -A x64
  ```
- **`unistd.h: No such file or directory` when building `freenect`.** libfreenect upstream includes POSIX headers unconditionally. Our `third_party/freenect-msvc-compat/unistd.h` shim should be on the include path automatically (set in `third_party/CMakeLists.txt`'s `freenect` target). If you see this error on a fresh checkout, verify the submodule is initialized: `git submodule status third_party/libfreenect`.
- **`xenia-hid-nui` link errors.** Stage 1 wired this library into `xenia-app` via `src/xenia/CMakeLists.txt` and `src/xenia/app/CMakeLists.txt`. If you hit unresolved symbols here on a fresh checkout, regenerate your build directory.

## How Kinectix differs from running stock xenia-canary

At default settings (`--nui_backend=none`) Kinectix is **functionally identical** to xenia-canary — Kinect titles still report "no Kinect" and behave exactly as they do under upstream. The differences live behind cvars (`--nui_backend`, `--nui_telemetry`) and the Stage 2.5 bootstrap fixes for Kinect Adventures (broadcast `XN_SYS_NUI_HARDWARESTATUSCHANGED` + `XamUnk2B001` stub) — those are no-ops for non-Kinect titles.

The only visible runtime difference at default settings is one extra log line at startup:

```
i> NUI: kinectix: backend=null connected=no caps=0x0
```

…meaning the no-op backend is installed and Kinect-using titles still see no sensor — same as upstream. To exercise the real Kinect path, configure hardware per [README.md § Hardware & driver requirements](README.md) and pass `--nui_backend=freenect` (Stage 4 M2 onward).
