// Minimal <unistd.h> shim for MSVC, exposing the subset of POSIX symbols that
// libfreenect (third_party/libfreenect) actually consumes:
//   - usleep()  — microsecond sleep, used by flags.c, cameras.c, tilt.c.
//   - sleep()   — second-granularity sleep, used by usb_libusb10.c (which
//                 defines its own #define sleep(x) Sleep((x)*1000) under
//                 _MSC_VER; we guard with #ifndef to avoid redefinition).
//
// libfreenect upstream includes <unistd.h> unconditionally on every platform.
// On MSVC there is no system <unistd.h>, so we ship this shim and inject its
// directory into freenect's PRIVATE include path from third_party/CMakeLists.txt.
//
// Pin context: libfreenect 09a1f09 (2024-01-06). If a future bump introduces
// new POSIX dependencies, extend this shim — do not patch upstream sources.

#ifndef KINECTIX_FREENECT_MSVC_UNISTD_H_
#define KINECTIX_FREENECT_MSVC_UNISTD_H_

#ifndef _WIN32
#error "freenect-msvc-compat/unistd.h is for Windows MSVC builds only"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// usleep: microsecond sleep. Map onto Sleep() (millisecond granularity).
// libfreenect call sites in this version:
//   flags.c: usleep(100000)  — 100 ms, between LED flag commands
//   flags.c: usleep(1)       — 1 µs busy-yield (we map to SwitchToThread)
// Fine-grain sub-millisecond is not achievable via Sleep(), so for sub-ms
// values we yield the scheduler, which matches the spirit of the call.
static __forceinline void usleep(unsigned long usec) {
  if (usec == 0) {
    return;
  }
  if (usec < 1000) {
    SwitchToThread();
    return;
  }
  Sleep((DWORD)(usec / 1000));
}

// sleep: defined by usb_libusb10.c itself under _MSC_VER. Guard so we don't
// trigger C4005 (macro redefinition) when both headers are processed together.
#ifndef sleep
#define sleep(s) Sleep((DWORD)(s) * 1000U)
#endif

#endif  // KINECTIX_FREENECT_MSVC_UNISTD_H_
