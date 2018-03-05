#ifndef STARBOARD_WPE_SHARED_GSTREAMER_VIDEO_DECODER_H_
#define STARBOARD_WPE_SHARED_GSTREAMER_VIDEO_DECODER_H_

#include "starboard/wpe/shared/open_max/dispmanx_resource_pool.h"
#include "starboard/shared/starboard/player/filter/video_decoder_internal.h"

namespace starboard {
namespace wpe {
namespace shared {
namespace gstreamer {

class DecodedVideo : public RefCountedThreadSafe<DecodedVideo> {
public:
    DecodedVideo(int size, SbMediaTime pts,
            int width, int height, int stride, int slice_height) {
        data_=SbMemoryAllocate(size);
        size_= size;
        pts_=pts;
        width_=width;
        height_=height;
        stride_=stride;
        slice_height_=slice_height;
    }
    ~DecodedVideo() {
        SbMemoryFree(data_);
    }

    void* data() const { return data_; }
    int size() const { return size_; }
    SbMediaTime pts() const { return pts_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    int slice_height() const { return slice_height_; }

private:
    void* data_;
    int size_;
    SbMediaTime pts_;
    int width_;
    int height_;
    int stride_;
    int slice_height_;
};

class VideoDecoder:public starboard::shared::starboard::player::filter::HostedVideoDecoder {
public:
    typedef starboard::shared::starboard::player::InputBuffer InputBuffer;
    typedef starboard::shared::starboard::player::VideoFrame VideoFrame;
    typedef ::starboard::shared::starboard::player::JobQueue JobQueue;
    typedef ::starboard::shared::starboard::player::Closure Closure;

    VideoDecoder(
            SbMediaVideoCodec video_codec, JobQueue* job_queue);
    ~VideoDecoder() SB_OVERRIDE;

    void SetHost(Host* host) SB_OVERRIDE;
    void WriteInputBuffer(const scoped_refptr
            <InputBuffer>& input_buffer) SB_OVERRIDE;
    scoped_refptr<InputBuffer> GetInputBuffer();
    bool PushOutputBuffer(
            uint8_t *buffer, int64_t size, int64_t pts,
            int32_t width, int32_t height,
            int32_t stride, int32_t slice_height);
    bool IfEndOfStream();
    void WriteEndOfStream() SB_OVERRIDE;
    void Reset() SB_OVERRIDE;

private:
    bool TryToDeliverOneFrame();
    scoped_refptr<VideoFrame> CreateFrame(
            const uint8_t* buffer, int64_t size, SbMediaTime pts,
            int32_t width, int32_t height,
            int32_t stride, int32_t slice_height);
    void Update();

    scoped_refptr<DispmanxResourcePool> resource_pool_;
    Host* host_;
    bool eos_written_;
    void *video_context;
    std::queue<scoped_refptr<InputBuffer> > input_buffers_;
    JobQueue* job_queue_;
    Closure update_closure_;
};

}  // namespace gstreamer
}  // namespace shared
}  // namespace wpe
}  // namespace starboard

#endif  // STARBOARD_WPE_SHARED_GSTREAMER_VIDEO_DECODER_H_
