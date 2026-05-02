/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Kinectix Contributors. All rights reserved.                 *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_NUI_FREENECT_FREENECT_BACKEND_H_
#define XENIA_HID_NUI_FREENECT_FREENECT_BACKEND_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

#include "xenia/hid/nui/nui_backend.h"

// Forward-declare libfreenect's opaque structs so this header does NOT pull
// in <libfreenect.h>. The implementation TU includes the real header; every
// other consumer (xam_nui.cc, NuiManager, etc.) sees only INuiBackend.
struct _freenect_context;
typedef struct _freenect_context freenect_context;
struct _freenect_device;
typedef struct _freenect_device freenect_device;

namespace xe {
namespace hid {
namespace nui {
namespace freenect {

// ----------------------------------------------------------------------------
// FreenectBackend
//
// INuiBackend implementation backed by libfreenect (Kinect v1, hardware
// IDs 1414 and 1473). Windows-only for now; on other platforms the
// XE_KINECTIX_NUI_FREENECT compile flag never trips and this class is
// not even compiled in.
//
// Threading model:
//   * Initialize()/Shutdown() run on the emulator main thread.
//   * A dedicated reader_thread_ pumps freenect_process_events() in a
//     loop. Depth and video callbacks fire on that thread and write
//     into per-stream triple buffers without locks.
//   * Poll*() reads the latest committed slot from the triple buffer
//     using sequence-counter verification, never blocks.
//
// Stage 4 M3 scope (delivered):
//   * Open device 0 (camera subdevice), 640x480 11-bit depth, 640x480
//     RGB video.
//   * No tilt / motor / accelerometer use.
//
// Stage 4 M4 scope (this file):
//   * PollSkeleton(0) returns a hardcoded T-pose at ~30 Hz so guest
//     code that gates on "is anybody being tracked" can advance.
//   * No real skeleton inference yet — Stage 5 plugs in MediaPipe /
//     NiTE2 / etc. The fake skeleton lets us validate the XAM NUI
//     glue end-to-end before we add a tracker dependency.
//   * SkeletonTrackingStatusChanged notification arrives in M5.
// ----------------------------------------------------------------------------

class FreenectBackend final : public INuiBackend {
 public:
  FreenectBackend();
  ~FreenectBackend() override;

  // INuiBackend.
  bool Initialize() override;
  void Shutdown() override;
  bool IsConnected() const override;
  uint32_t Capabilities() const override;
  std::optional<SkeletonFrame> PollSkeleton(uint32_t index) override;
  std::optional<DepthFrame> PollDepth() override;
  std::optional<ColorFrame> PollColor() override;
  std::string Name() const override { return "freenect"; }

 private:
  // libfreenect C callbacks. Each forwards to the FreenectBackend instance
  // stored in the device's user pointer.
  static void DepthCallbackTrampoline(freenect_device* dev, void* depth_buf,
                                      uint32_t timestamp);
  static void VideoCallbackTrampoline(freenect_device* dev, void* video_buf,
                                      uint32_t timestamp);

  // Instance-side handlers invoked by the trampolines above.
  void OnDepthFrame(const uint16_t* depth_pixels, uint32_t timestamp);
  void OnVideoFrame(const uint8_t* rgb_pixels, uint32_t timestamp);

  // Reader thread entry point: spins on freenect_process_events() while
  // running_ is true.
  void ReaderThreadMain();

  // Microseconds since FreenectBackend startup (host steady_clock).
  uint64_t HostNowUs() const;

  // ---------------------------------------------------------------------------
  // SPSC triple buffer per stream.
  //
  // Each slot carries a monotonically increasing sequence number. The writer
  // (USB callback thread) picks the slot with the lowest seq, fills it, and
  // bumps both that slot's seq and the global latest_seq_ to a new value.
  // The reader (kernel poll thread) loads latest_seq_, locates the slot
  // whose seq matches, copies it out, then re-checks the slot seq to detect
  // mid-copy overwrites — seqlock-style. With 30 Hz producer and ~150 KB
  // depth / 1.2 MB color memcpy on the reader path, mid-copy overwrites are
  // vanishingly rare and the verification loop catches them.
  //
  // kSlotCount = 3 ensures that the writer always has at least one slot
  // that is neither the latest-published nor the one a reader could be
  // copying.
  // ---------------------------------------------------------------------------
  static constexpr int kSlotCount = 3;

  template <typename Frame>
  struct TripleBuffer {
    std::array<Frame, kSlotCount> slots{};
    std::array<std::atomic<uint64_t>, kSlotCount> slot_seq{};
    std::atomic<uint64_t> latest_seq{0};
    // Reader-only state. Tracks the seq we've already returned so we never
    // hand the same frame out twice — INuiBackend contract.
    uint64_t last_polled_seq = 0;
  };

  TripleBuffer<DepthFrame> depth_buffer_;
  TripleBuffer<ColorFrame> color_buffer_;

  // Push helpers used by callbacks; declared here so the implementation
  // can stay in the .cc file without relying on template visibility.
  uint64_t PushDepthFrame(DepthFrame&& frame);
  uint64_t PushColorFrame(ColorFrame&& frame);

  // Pop helper shared by PollDepth/PollColor. Returns true and writes into
  // `out` if a frame newer than `last_polled` is available; returns false
  // otherwise. Implemented with seqlock-style verification.
  template <typename Frame>
  bool PopFrame(TripleBuffer<Frame>& buf, Frame* out);

  // ---------------------------------------------------------------------------
  // libfreenect handles + reader thread state.
  // ---------------------------------------------------------------------------
  freenect_context* ctx_ = nullptr;
  freenect_device* dev_ = nullptr;
  std::thread reader_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};

  // Frame geometry, captured from libfreenect after set_*_mode.
  uint16_t depth_width_ = 0;
  uint16_t depth_height_ = 0;
  uint16_t color_width_ = 0;
  uint16_t color_height_ = 0;

  // Reusable RGB→BGRA staging buffer used inside OnVideoFrame so we don't
  // allocate on the USB callback path. Sized once during Initialize().
  std::vector<uint8_t> bgra_scratch_;

  // Steady-clock zero-point. Frame timestamps are reported relative to this
  // so that NuiManager::HostUsToGuestTimestamp can subtract its own epoch
  // without negative values.
  uint64_t start_us_ = 0;

  // ---------------------------------------------------------------------------
  // Stage 4 M4: fake T-pose skeleton emission state.
  //
  // PollSkeleton(0) hands out a hardcoded T-pose every ~33 ms (i.e. a
  // simulated 30 Hz tracker). last_skeleton_emit_us_ holds the host time
  // of the most recent emission so the rate limiter can decide when to
  // produce the next frame; skeleton_emit_count_ is used only to throttle
  // the diagnostic XELOGD that confirms the path is alive.
  // ---------------------------------------------------------------------------
  uint64_t last_skeleton_emit_us_ = 0;
  uint64_t skeleton_emit_count_ = 0;
};

}  // namespace freenect
}  // namespace nui
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_NUI_FREENECT_FREENECT_BACKEND_H_
