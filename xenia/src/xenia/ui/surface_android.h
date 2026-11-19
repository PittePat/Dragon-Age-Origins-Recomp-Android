/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_SURFACE_ANDROID_H_
#define XENIA_UI_SURFACE_ANDROID_H_

#include <android/native_window.h>

#include "xenia/ui/surface.h"

namespace xe {
namespace ui {

class AndroidNativeWindowSurface : public Surface {
 public:
  // The surface holds its own reference to the window: the presenter keeps the
  // surface and may create a Vulkan surface from it again later (after a
  // swapchain failure), when SDL may already have released the window on
  // surfaceDestroyed. Without the reference that was a use after free in the
  // driver's vkCreateAndroidSurfaceKHR (incStrong on the window: a crash on a
  // relaunch, Sony Xperia XZ3); with it the window object stays valid and an
  // abandoned one only makes surface or swapchain creation fail.
  explicit AndroidNativeWindowSurface(ANativeWindow* window)
      : window_(window) {
    if (window_) {
      ANativeWindow_acquire(window_);
    }
  }
  ~AndroidNativeWindowSurface() override {
    if (window_) {
      ANativeWindow_release(window_);
    }
  }
  AndroidNativeWindowSurface(const AndroidNativeWindowSurface&) = delete;
  AndroidNativeWindowSurface& operator=(const AndroidNativeWindowSurface&) =
      delete;
  TypeIndex GetType() const override { return kTypeIndex_AndroidNativeWindow; }
  ANativeWindow* window() const { return window_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  ANativeWindow* window_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_SURFACE_ANDROID_H_
