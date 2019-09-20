// Copyright 2016 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef STARBOARD_RASPI_SHARED_DISPMANX_UTIL_H_
#define STARBOARD_RASPI_SHARED_DISPMANX_UTIL_H_

#include <bcm_host.h>

#include <functional>

#include "starboard/common/log.h"
#include "starboard/common/scoped_ptr.h"
#include "starboard/configuration.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/player/filter/video_frame_internal.h"
#include "starboard/types.h"

#ifdef USE_COMPOSITOR
#include <WPEFramework/compositor/Client.h>
#include <chrono>
#endif

namespace starboard {
namespace raspi {
namespace shared {

class DispmanxRect : public VC_RECT_T {
 public:
  DispmanxRect() { vc_dispmanx_rect_set(this, 0, 0, 0, 0); }
  DispmanxRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    vc_dispmanx_rect_set(this, x, y, width, height);
  }
};

class DispmanxDisplay {
 public:
  DispmanxDisplay() {
#ifdef USE_COMPOSITOR
    const char* callsign(std::getenv("CLIENT_IDENTIFIER"));
    if (callsign == nullptr) {
        display_name_ = "CobaltBrowser" + std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
    }
    else {
        const char* delimiter = nullptr;
        if ((delimiter = strchr(callsign, ',')) == nullptr) {
            display_name_ = callsign;
        }
        else {
            display_name_ = std::string(callsign, (delimiter - callsign));
        }
    }
    idisplay_ = WPEFramework::Compositor::IDisplay::Instance(display_name_);
#else
    bcm_host_init();
    handle_ = vc_dispmanx_display_open(0);
    SB_DCHECK(handle_ != DISPMANX_NO_HANDLE);
#endif
  }
  ~DispmanxDisplay() {
#ifdef USE_COMPOSITOR
    idisplay_->Release();
#else
    int result = vc_dispmanx_display_close(handle_);
    SB_DCHECK(result == 0);
    bcm_host_deinit();
#endif
  }

#ifdef USE_COMPOSITOR
  WPEFramework::Compositor::IDisplay *handle() const { return idisplay_; }
  std::string DisplayName() const { return display_name_; }
#else
  DISPMANX_DISPLAY_HANDLE_T handle() const { return handle_; }
#endif

 private:
#ifdef USE_COMPOSITOR
  WPEFramework::Compositor::IDisplay *idisplay_;
  std::string display_name_;
#else
  DISPMANX_DISPLAY_HANDLE_T handle_;
#endif

  SB_DISALLOW_COPY_AND_ASSIGN(DispmanxDisplay);
};

class DispmanxResource {
 public:
  DispmanxResource() : handle_(DISPMANX_NO_HANDLE) {}

  virtual ~DispmanxResource() {
    if (handle_ != DISPMANX_NO_HANDLE) {
      int32_t result = vc_dispmanx_resource_delete(handle_);
      SB_DCHECK(result == 0) << " result=" << result;
    }
  }

  DISPMANX_RESOURCE_HANDLE_T handle() const { return handle_; }
  uint32_t visible_width() const { return visible_width_; }
  uint32_t visible_height() const { return visible_height_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

 protected:
  DispmanxResource(VC_IMAGE_TYPE_T image_type,
                   uint32_t width,
                   uint32_t height,
                   uint32_t visible_width,
                   uint32_t visible_height);

 private:
  DISPMANX_RESOURCE_HANDLE_T handle_;
  uint32_t width_;
  uint32_t height_;
  uint32_t visible_width_;
  uint32_t visible_height_;

  SB_DISALLOW_COPY_AND_ASSIGN(DispmanxResource);
};

class DispmanxYUV420Resource : public DispmanxResource {
 public:
  DispmanxYUV420Resource(uint32_t width,
                         uint32_t height,
                         uint32_t visible_width,
                         uint32_t visible_height)
      : DispmanxResource(VC_IMAGE_YUV420,
                         width,
                         height,
                         visible_width,
                         visible_height) {}

  void WriteData(const void* data);
  void ClearWithBlack();
};

class DispmanxRGB565Resource : public DispmanxResource {
 public:
  DispmanxRGB565Resource(uint32_t width,
                         uint32_t height,
                         uint32_t visible_width,
                         uint32_t visible_height)
      : DispmanxResource(VC_IMAGE_RGB565,
                         width,
                         height,
                         visible_width,
                         visible_height) {}

  void WriteData(const void* data);
  void ClearWithBlack();
};

class DispmanxElement {
 public:
  DispmanxElement(const DispmanxDisplay& display,
                  int32_t layer,
                  const DispmanxRect& dest_rect,
                  const DispmanxResource& src,
                  const DispmanxRect& src_rect);
  ~DispmanxElement();

  DISPMANX_ELEMENT_HANDLE_T handle() const { return handle_; }
  void ChangeSource(const DispmanxResource& new_src);
#ifdef USE_COMPOSITOR
  WPEFramework::Compositor::IDisplay::ISurface* GetSurface() { return isurface_; }
#endif

 private:
  DISPMANX_ELEMENT_HANDLE_T handle_;
#ifdef USE_COMPOSITOR
  WPEFramework::Compositor::IDisplay::ISurface* isurface_;
#endif

  SB_DISALLOW_COPY_AND_ASSIGN(DispmanxElement);
};

class DispmanxVideoFrame
    : public starboard::shared::starboard::player::filter::VideoFrame {
 public:
  DispmanxVideoFrame(SbTime time,
                     DispmanxYUV420Resource* resource,
                     std::function<void(DispmanxYUV420Resource*)> release_cb)
      : VideoFrame(time), resource_(resource), release_cb_(release_cb) {
    SB_DCHECK(resource_);
    SB_DCHECK(release_cb_);
  }
  ~DispmanxVideoFrame() { release_cb_(resource_); }

  DispmanxYUV420Resource* resource() const { return resource_; }

 private:
  DispmanxYUV420Resource* resource_;
  std::function<void(DispmanxYUV420Resource*)> release_cb_;
};

class DispmanxVideoRenderer {
 public:
  typedef starboard::shared::starboard::player::filter::VideoFrame VideoFrame;

  DispmanxVideoRenderer(const DispmanxDisplay& display, int32_t layer);

  void Update(const scoped_refptr<VideoFrame>& video_frame);

 private:
  scoped_ptr<DispmanxElement> element_;
  scoped_refptr<VideoFrame> frame_;

  // Used to fill the background with black if no video is playing so that the
  // console does not show through.
  DispmanxRGB565Resource black_frame_;

  SB_DISALLOW_COPY_AND_ASSIGN(DispmanxVideoRenderer);
};

}  // namespace shared
}  // namespace raspi
}  // namespace starboard

#endif  // STARBOARD_RASPI_SHARED_DISPMANX_UTIL_H_
