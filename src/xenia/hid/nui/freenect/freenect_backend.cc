/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Kinectix Contributors. All rights reserved.                 *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/nui/freenect/freenect_backend.h"

#include <chrono>
#include <cstring>
#include <utility>

#include <libfreenect.h>

#include "xenia/base/logging.h"

namespace xe {
namespace hid {
namespace nui {
namespace freenect {

namespace {

// ---------------------------------------------------------------------------
// Stage 4 M4: fake T-pose skeleton.
//
// All values are sensor-relative meters using the standard NUI axis
// convention from nui_backend.h: +x to the player's right, +y up, +z
// forward (away from sensor). The pose places a ~1.78 m subject standing
// 2.5 m in front of the sensor with arms held horizontally — the canonical
// calibration pose Kinect titles use as a "ready" gesture.
//
// Joint heights are anatomical proportions of an average adult male.
// Producing exact NuiSkeletonHeader::FloorClipPlane data is not in scope
// for M4; the floor is implicitly y=0 here, which matches the default a
// guest title sees from a freshly-initialized sensor.
//
// Order matches the SkeletonPosition enum in nui_constants.h (do NOT
// reorder — guest consumers index by raw enum value).
// ---------------------------------------------------------------------------
constexpr float kFakeTposeStandDistanceM = 2.5f;  // distance from sensor

struct TposeJointInit {
  SkeletonPosition slot;
  float x;
  float y;
};

constexpr std::array<TposeJointInit, kSkeletonPositionCount> kFakeTposeXY = {{
    {kSkeletonPositionHipCenter, 0.00f, 0.95f},
    {kSkeletonPositionSpine, 0.00f, 1.20f},
    {kSkeletonPositionShoulderCenter, 0.00f, 1.45f},
    {kSkeletonPositionHead, 0.00f, 1.65f},
    {kSkeletonPositionShoulderLeft, -0.20f, 1.45f},
    {kSkeletonPositionElbowLeft, -0.45f, 1.45f},
    {kSkeletonPositionWristLeft, -0.70f, 1.45f},
    {kSkeletonPositionHandLeft, -0.78f, 1.45f},
    {kSkeletonPositionShoulderRight, 0.20f, 1.45f},
    {kSkeletonPositionElbowRight, 0.45f, 1.45f},
    {kSkeletonPositionWristRight, 0.70f, 1.45f},
    {kSkeletonPositionHandRight, 0.78f, 1.45f},
    {kSkeletonPositionHipLeft, -0.10f, 0.92f},
    {kSkeletonPositionKneeLeft, -0.10f, 0.50f},
    {kSkeletonPositionAnkleLeft, -0.10f, 0.10f},
    {kSkeletonPositionFootLeft, -0.10f, 0.02f},
    {kSkeletonPositionHipRight, 0.10f, 0.92f},
    {kSkeletonPositionKneeRight, 0.10f, 0.50f},
    {kSkeletonPositionAnkleRight, 0.10f, 0.10f},
    {kSkeletonPositionFootRight, 0.10f, 0.02f},
}};

void FillFakeTposeFrame(SkeletonFrame* frame, uint64_t host_timestamp_us) {
  frame->skeleton_index = 0;
  frame->state = kSkeletonTracked;
  frame->host_timestamp_us = host_timestamp_us;
  for (const auto& init : kFakeTposeXY) {
    auto& j = frame->joints[init.slot];
    j.x = init.x;
    j.y = init.y;
    j.z = kFakeTposeStandDistanceM;
    j.tracking_state = kSkeletonPositionTracked;
  }
}

// Cap the fake tracker at ~30 Hz to match a real Kinect skeleton stream.
// Going faster is pointless — guest titles poll once per render frame and
// we'd just waste CPU producing identical data.
constexpr uint64_t kFakeSkeletonPeriodUs = 1'000'000ull / 30ull;

uint64_t MonotonicNowUs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::microseconds>(
             clock::now().time_since_epoch())
      .count();
}

const char* FreenectLogLevelToString(freenect_loglevel level) {
  switch (level) {
    case FREENECT_LOG_FATAL:
      return "FATAL";
    case FREENECT_LOG_ERROR:
      return "ERROR";
    case FREENECT_LOG_WARNING:
      return "WARN ";
    case FREENECT_LOG_NOTICE:
      return "NOTE ";
    case FREENECT_LOG_INFO:
      return "INFO ";
    default:
      return "DEBUG";
  }
}

void FreenectLogTrampoline(freenect_context* /*ctx*/, freenect_loglevel level,
                           const char* msg) {
  // libfreenect log lines arrive with a trailing newline; XELOG* adds its
  // own, so route through the appropriate severity bucket and let xenia
  // logging take care of the formatting.
  if (level <= FREENECT_LOG_ERROR) {
    XELOGE("freenect[{}]: {}", FreenectLogLevelToString(level), msg);
  } else if (level == FREENECT_LOG_WARNING) {
    XELOGW("freenect[{}]: {}", FreenectLogLevelToString(level), msg);
  } else if (level <= FREENECT_LOG_INFO) {
    XELOGI("freenect[{}]: {}", FreenectLogLevelToString(level), msg);
  } else {
    XELOGD("freenect[{}]: {}", FreenectLogLevelToString(level), msg);
  }
}

}  // namespace

FreenectBackend::FreenectBackend() = default;

FreenectBackend::~FreenectBackend() {
  // Defensive: if the owner forgot to call Shutdown(), do it here so we
  // don't leak the libfreenect context or leave the reader thread alive.
  if (running_.load(std::memory_order_acquire) || ctx_ != nullptr) {
    Shutdown();
  }
}

bool FreenectBackend::Initialize() {
  XELOGI("freenect: initializing...");
  start_us_ = MonotonicNowUs();

  if (freenect_init(&ctx_, nullptr) < 0) {
    XELOGE("freenect_init() failed.");
    ctx_ = nullptr;
    return false;
  }

  freenect_set_log_level(ctx_, FREENECT_LOG_NOTICE);
  freenect_set_log_callback(ctx_, &FreenectLogTrampoline);
  freenect_select_subdevices(ctx_, FREENECT_DEVICE_CAMERA);

  const int num_devices = freenect_num_devices(ctx_);
  if (num_devices < 1) {
    XELOGW("freenect: no Kinect devices detected (num_devices={}).",
           num_devices);
    freenect_shutdown(ctx_);
    ctx_ = nullptr;
    return false;
  }
  XELOGI("freenect: {} device(s) visible; opening index 0.", num_devices);

  if (freenect_open_device(ctx_, &dev_, 0) < 0) {
    XELOGE("freenect_open_device(0) failed.");
    freenect_shutdown(ctx_);
    ctx_ = nullptr;
    dev_ = nullptr;
    return false;
  }
  freenect_set_user(dev_, this);

  // ---- Depth: MEDIUM 11-bit (640x480, one uint16_t per pixel). ----
  freenect_frame_mode depth_mode = freenect_find_depth_mode(
      FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_11BIT);
  if (!depth_mode.is_valid) {
    XELOGE("freenect: no MEDIUM/11BIT depth mode available.");
    freenect_close_device(dev_);
    freenect_shutdown(ctx_);
    dev_ = nullptr;
    ctx_ = nullptr;
    return false;
  }
  if (freenect_set_depth_mode(dev_, depth_mode) < 0) {
    XELOGE("freenect_set_depth_mode() failed.");
    freenect_close_device(dev_);
    freenect_shutdown(ctx_);
    dev_ = nullptr;
    ctx_ = nullptr;
    return false;
  }
  depth_width_ = static_cast<uint16_t>(depth_mode.width);
  depth_height_ = static_cast<uint16_t>(depth_mode.height);
  XELOGI("freenect: depth mode {}x{}, {} bytes/frame.", depth_mode.width,
         depth_mode.height, depth_mode.bytes);

  // ---- Color: MEDIUM RGB (640x480, 24bpp packed). ----
  freenect_frame_mode video_mode =
      freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_VIDEO_RGB);
  if (!video_mode.is_valid) {
    XELOGE("freenect: no MEDIUM/RGB video mode available.");
    freenect_close_device(dev_);
    freenect_shutdown(ctx_);
    dev_ = nullptr;
    ctx_ = nullptr;
    return false;
  }
  if (freenect_set_video_mode(dev_, video_mode) < 0) {
    XELOGE("freenect_set_video_mode() failed.");
    freenect_close_device(dev_);
    freenect_shutdown(ctx_);
    dev_ = nullptr;
    ctx_ = nullptr;
    return false;
  }
  color_width_ = static_cast<uint16_t>(video_mode.width);
  color_height_ = static_cast<uint16_t>(video_mode.height);
  XELOGI("freenect: video mode {}x{}, {} bytes/frame.", video_mode.width,
         video_mode.height, video_mode.bytes);

  // Pre-size every triple-buffer slot so the USB callback path never
  // allocates. Depth: uint16/pixel. Color: BGRA after RGB→BGRA conversion.
  const size_t depth_pixels = static_cast<size_t>(depth_width_) * depth_height_;
  for (auto& slot : depth_buffer_.slots) {
    slot.width = depth_width_;
    slot.height = depth_height_;
    slot.pixels.assign(depth_pixels, 0);
  }
  for (auto& seq : depth_buffer_.slot_seq) {
    seq.store(0, std::memory_order_relaxed);
  }
  depth_buffer_.latest_seq.store(0, std::memory_order_relaxed);
  depth_buffer_.last_polled_seq = 0;

  const size_t color_pixels = static_cast<size_t>(color_width_) * color_height_;
  for (auto& slot : color_buffer_.slots) {
    slot.width = color_width_;
    slot.height = color_height_;
    slot.pixels.assign(color_pixels * 4, 0);
  }
  for (auto& seq : color_buffer_.slot_seq) {
    seq.store(0, std::memory_order_relaxed);
  }
  color_buffer_.latest_seq.store(0, std::memory_order_relaxed);
  color_buffer_.last_polled_seq = 0;

  bgra_scratch_.assign(color_pixels * 4, 0);

  freenect_set_depth_callback(dev_, &FreenectBackend::DepthCallbackTrampoline);
  freenect_set_video_callback(dev_, &FreenectBackend::VideoCallbackTrampoline);

  if (freenect_start_depth(dev_) < 0) {
    XELOGE("freenect_start_depth() failed.");
    freenect_close_device(dev_);
    freenect_shutdown(ctx_);
    dev_ = nullptr;
    ctx_ = nullptr;
    return false;
  }
  if (freenect_start_video(dev_) < 0) {
    XELOGE("freenect_start_video() failed.");
    freenect_stop_depth(dev_);
    freenect_close_device(dev_);
    freenect_shutdown(ctx_);
    dev_ = nullptr;
    ctx_ = nullptr;
    return false;
  }

  connected_.store(true, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  reader_thread_ = std::thread(&FreenectBackend::ReaderThreadMain, this);

  XELOGI("freenect: backend ready (depth+color streaming).");
  return true;
}

void FreenectBackend::Shutdown() {
  XELOGI("freenect: shutting down...");
  running_.store(false, std::memory_order_release);

  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }

  if (dev_) {
    freenect_stop_depth(dev_);
    freenect_stop_video(dev_);
    freenect_close_device(dev_);
    dev_ = nullptr;
  }
  if (ctx_) {
    freenect_shutdown(ctx_);
    ctx_ = nullptr;
  }
  connected_.store(false, std::memory_order_release);
  XELOGI("freenect: shutdown complete.");
}

bool FreenectBackend::IsConnected() const {
  return connected_.load(std::memory_order_acquire);
}

uint32_t FreenectBackend::Capabilities() const {
  // Depth + color come straight from libfreenect; the skeleton "capability"
  // is currently fulfilled by the fake T-pose emitter in PollSkeleton (M4).
  // Stage 5 will replace the fake skeleton with a real tracker but the cap
  // bit stays the same — guest software cannot tell which source produced
  // the joints.
  return kCapabilityDepth | kCapabilityColor | kCapabilitySkeleton;
}

void FreenectBackend::ReaderThreadMain() {
  // Use the timeout variant so a quiescent USB bus can't pin Shutdown() —
  // the loop wakes every 100 ms and re-checks running_.
  struct timeval tv;
  while (running_.load(std::memory_order_acquire)) {
    tv.tv_sec = 0;
    tv.tv_usec = 100 * 1000;
    int rc = freenect_process_events_timeout(ctx_, &tv);
    if (rc < 0) {
      XELOGE("freenect_process_events returned {}; reader thread exiting.", rc);
      break;
    }
  }
}

uint64_t FreenectBackend::HostNowUs() const {
  uint64_t now = MonotonicNowUs();
  return now >= start_us_ ? (now - start_us_) : 0;
}

// --------------------------------------------------------------------------
// Triple-buffer push: writer-side path. Picks the slot with the lowest seq
// (i.e. the slot a reader cannot currently be holding, since the reader
// always latches the slot whose seq matches latest_seq_) and publishes the
// new frame by bumping latest_seq_ and the slot's seq atomically.
// --------------------------------------------------------------------------

uint64_t FreenectBackend::PushDepthFrame(DepthFrame&& frame) {
  int oldest = 0;
  uint64_t oldest_seq =
      depth_buffer_.slot_seq[0].load(std::memory_order_acquire);
  for (int i = 1; i < kSlotCount; ++i) {
    uint64_t s = depth_buffer_.slot_seq[i].load(std::memory_order_acquire);
    if (s < oldest_seq) {
      oldest_seq = s;
      oldest = i;
    }
  }
  uint64_t new_seq =
      depth_buffer_.latest_seq.fetch_add(1, std::memory_order_acq_rel) + 1;
  depth_buffer_.slots[oldest] = std::move(frame);
  depth_buffer_.slot_seq[oldest].store(new_seq, std::memory_order_release);
  return new_seq;
}

uint64_t FreenectBackend::PushColorFrame(ColorFrame&& frame) {
  int oldest = 0;
  uint64_t oldest_seq =
      color_buffer_.slot_seq[0].load(std::memory_order_acquire);
  for (int i = 1; i < kSlotCount; ++i) {
    uint64_t s = color_buffer_.slot_seq[i].load(std::memory_order_acquire);
    if (s < oldest_seq) {
      oldest_seq = s;
      oldest = i;
    }
  }
  uint64_t new_seq =
      color_buffer_.latest_seq.fetch_add(1, std::memory_order_acq_rel) + 1;
  color_buffer_.slots[oldest] = std::move(frame);
  color_buffer_.slot_seq[oldest].store(new_seq, std::memory_order_release);
  return new_seq;
}

template <typename Frame>
bool FreenectBackend::PopFrame(TripleBuffer<Frame>& buf, Frame* out) {
  uint64_t cur_latest = buf.latest_seq.load(std::memory_order_acquire);
  if (cur_latest == buf.last_polled_seq) {
    return false;
  }
  // Up to 3 retry passes to handle a writer that publishes during our copy.
  // In practice a single pass succeeds — frames arrive at 30 Hz while a
  // depth+color memcpy completes well under a millisecond.
  for (int attempt = 0; attempt < 3; ++attempt) {
    for (int i = 0; i < kSlotCount; ++i) {
      uint64_t pre = buf.slot_seq[i].load(std::memory_order_acquire);
      if (pre != cur_latest) {
        continue;
      }
      *out = buf.slots[i];
      uint64_t post = buf.slot_seq[i].load(std::memory_order_acquire);
      if (post == pre) {
        buf.last_polled_seq = cur_latest;
        return true;
      }
      // Lost the race; re-read latest_seq and try again.
      break;
    }
    cur_latest = buf.latest_seq.load(std::memory_order_acquire);
    if (cur_latest == buf.last_polled_seq) {
      return false;
    }
  }
  return false;
}

// --------------------------------------------------------------------------
// libfreenect callback trampolines + per-frame ingest.
// --------------------------------------------------------------------------

void FreenectBackend::DepthCallbackTrampoline(freenect_device* dev,
                                              void* depth_buf,
                                              uint32_t timestamp) {
  auto* self = static_cast<FreenectBackend*>(freenect_get_user(dev));
  if (!self) {
    return;
  }
  self->OnDepthFrame(static_cast<const uint16_t*>(depth_buf), timestamp);
}

void FreenectBackend::VideoCallbackTrampoline(freenect_device* dev,
                                              void* video_buf,
                                              uint32_t timestamp) {
  auto* self = static_cast<FreenectBackend*>(freenect_get_user(dev));
  if (!self) {
    return;
  }
  self->OnVideoFrame(static_cast<const uint8_t*>(video_buf), timestamp);
}

void FreenectBackend::OnDepthFrame(const uint16_t* depth_pixels,
                                   uint32_t /*timestamp*/) {
  const size_t pixel_count = static_cast<size_t>(depth_width_) * depth_height_;
  DepthFrame frame;
  frame.width = depth_width_;
  frame.height = depth_height_;
  frame.host_timestamp_us = HostNowUs();
  frame.pixels.resize(pixel_count);
  std::memcpy(frame.pixels.data(), depth_pixels,
              pixel_count * sizeof(uint16_t));
  PushDepthFrame(std::move(frame));
}

void FreenectBackend::OnVideoFrame(const uint8_t* rgb_pixels,
                                   uint32_t /*timestamp*/) {
  const size_t pixel_count = static_cast<size_t>(color_width_) * color_height_;

  // Convert RGB (3 bytes/pixel) → BGRA (4 bytes/pixel, alpha = 255) into a
  // pre-allocated scratch buffer to avoid allocator traffic on the USB
  // callback path. The scratch is then copied into a fresh ColorFrame whose
  // vector is moved into the published slot.
  if (bgra_scratch_.size() != pixel_count * 4) {
    bgra_scratch_.resize(pixel_count * 4);
  }
  for (size_t i = 0; i < pixel_count; ++i) {
    const uint8_t r = rgb_pixels[3 * i + 0];
    const uint8_t g = rgb_pixels[3 * i + 1];
    const uint8_t b = rgb_pixels[3 * i + 2];
    bgra_scratch_[4 * i + 0] = b;
    bgra_scratch_[4 * i + 1] = g;
    bgra_scratch_[4 * i + 2] = r;
    bgra_scratch_[4 * i + 3] = 255;
  }

  ColorFrame frame;
  frame.width = color_width_;
  frame.height = color_height_;
  frame.host_timestamp_us = HostNowUs();
  frame.pixels = bgra_scratch_;
  PushColorFrame(std::move(frame));
}

// --------------------------------------------------------------------------
// Reader-side polling.
// --------------------------------------------------------------------------

std::optional<SkeletonFrame> FreenectBackend::PollSkeleton(uint32_t index) {
  // Stage 4 M4 — synthetic T-pose tracker.
  //
  // We expose a single slot (index 0). Every other slot returns nullopt,
  // matching how a real Kinect would advertise unused skeleton slots when
  // only one player is in frame. M5/M6 will refine this once we know
  // exactly which guest call sites query non-zero indices.
  //
  // The emitter is rate-limited to ~30 Hz against the host steady clock.
  // INuiBackend's contract says we must not return the same frame twice;
  // returning nullopt between emissions is the cheapest way to honor that
  // and lets guest pollers spin without amplifying our log volume.
  if (index != 0) {
    return std::nullopt;
  }
  if (!connected_.load(std::memory_order_acquire)) {
    return std::nullopt;
  }

  const uint64_t now = HostNowUs();
  if (last_skeleton_emit_us_ != 0 &&
      now - last_skeleton_emit_us_ < kFakeSkeletonPeriodUs) {
    return std::nullopt;
  }
  last_skeleton_emit_us_ = now;
  ++skeleton_emit_count_;

  SkeletonFrame frame;
  FillFakeTposeFrame(&frame, now);

  // Diagnostic — first emission and then once per second. XELOGD is gated
  // by --log_level=3, so this is silent under the default info level and
  // doesn't pollute production logs once Stage 5 lands.
  if (skeleton_emit_count_ == 1 || (skeleton_emit_count_ % 30) == 0) {
    const auto& head = frame.joints[kSkeletonPositionHead];
    XELOGD(
        "freenect: fake T-pose emit #{} head=({:.2f}, {:.2f}, {:.2f}) "
        "state=tracked",
        skeleton_emit_count_, head.x, head.y, head.z);
  }

  return frame;
}

std::optional<DepthFrame> FreenectBackend::PollDepth() {
  DepthFrame frame;
  if (PopFrame(depth_buffer_, &frame)) {
    return frame;
  }
  return std::nullopt;
}

std::optional<ColorFrame> FreenectBackend::PollColor() {
  ColorFrame frame;
  if (PopFrame(color_buffer_, &frame)) {
    return frame;
  }
  return std::nullopt;
}

}  // namespace freenect
}  // namespace nui
}  // namespace hid
}  // namespace xe
