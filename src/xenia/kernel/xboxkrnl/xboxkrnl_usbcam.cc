/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t XUsbcamCreate_entry(dword_t buffer,
                                   dword_t buffer_size,  // 0x4B000 640x480?
                                   lpdword_t handle_out) {
  // This function should return success.
  // It looks like it only allocates space for usbcam support.
  // returning error code might cause games to initialize incorrectly.
  // "Carcassonne" initalization function checks for result from this
  // function. If value is different than 0 instead of loading
  // rest of the game it returns from initalization function and tries
  // to run game normally which causes crash, due to uninitialized data.
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XUsbcamCreate, kNone, kStub);

dword_result_t XUsbcamGetState_entry() {
  // 0 = not connected.
  // 1 = initialized
  // 2 = connected
  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(XUsbcamGetState, kNone, kStub);

// ---------------------------------------------------------------------------
// Kinectix Stage 4 M5 -- kernel stubs for the NUI hardware probe helpers
// that XamNuiGetDeviceStatus calls under the hood. All three are
// referenced in xboxkrnl_table.inc but had no `_entry` shim, so xenia's
// kernel resolver left their trampolines as nullptr and Kinect Adventures
// (TitleID 4D5308ED) -- which imports PsCamDeviceRequest directly via
// xboxkrnl ordinal 0x30D -- got undefined behavior. The title's NUI
// bootstrap chain then concluded the camera/microphone stack was not
// present and fell through to XamShowNuiHardwareRequiredUI before any
// skeleton API call.
//
// Returning X_ERROR_SUCCESS (0) here mirrors the "happy path" the
// reverse-engineered XAM build 0.0.13599.32 takes when a healthy sensor
// is enumerated. We do not yet emulate the buffer payload -- the launch-
// era runtime appears to tolerate empty/zero buffers as long as the
// status code is success.
// ---------------------------------------------------------------------------

dword_result_t PsCamDeviceRequest_entry(unknown_t /*unk*/) {
  // KA pulls this in from xboxkrnl ordinal 0x30D. Used inside
  // XamNuiGetDeviceStatus to probe the Natal camera subsystem; a negative
  // return is treated as "no camera", success is required to advance to
  // the McaDeviceRequest probe.
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(PsCamDeviceRequest, kNone, kStub);

dword_result_t McaDeviceRequest_entry(unknown_t /*unk*/) {
  // Microphone Array probe. Sibling of PsCamDeviceRequest -- XAM checks
  // both before flagging the sensor as "ready". KA does not import this
  // ordinal directly but other Kinect titles built against newer SDKs
  // do; stubbing it now keeps a consistent NUI surface area.
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(McaDeviceRequest, kNone, kStub);

dword_result_t DetroitDeviceRequest_entry(unknown_t /*unk*/) {
  // "Detroit" is the Natal camera's internal codename in the launch-era
  // NUI XDK. Mentioned in the XamNuiGetDeviceStatus reverse-engineering
  // notes (xam_nui.cc) as a step that can taint status_ptr->unk1 with
  // an HRESULT when the device is missing. Success keeps the unk fields
  // zeroed, which matches what we want.
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(DetroitDeviceRequest, kNone, kStub);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Usbcam);
