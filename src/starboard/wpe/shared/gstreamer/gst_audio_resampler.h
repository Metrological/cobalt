#ifndef STARBOARD_SHARED_GSTREAMER_AUDIO_RESAMPLER_H_
#define STARBOARD_SHARED_GSTREAMER_AUDIO_RESAMPLER_H_

#include <queue>

#include "starboard/shared/starboard/player/filter/audio_resampler.h"

namespace starboard {
namespace shared {
namespace gstreamer {

class AudioResampler : public starboard::player::filter::AudioResampler {
public:
    AudioResampler(SbMediaAudioSampleType source_sample_type,
            SbMediaAudioFrameStorageType source_storage_type,
            int source_sample_rate,
            SbMediaAudioSampleType destination_sample_type,
            SbMediaAudioFrameStorageType destination_storage_type,
            int destination_sample_rate,
            int channels);
    ~AudioResampler() SB_OVERRIDE;

    scoped_refptr<DecodedAudio> Resample(
            const scoped_refptr<DecodedAudio>& audio_data) SB_OVERRIDE;
    scoped_refptr<DecodedAudio> WriteEndOfStream() SB_OVERRIDE;

private:
    SbMediaAudioSampleType source_sample_type_;
    SbMediaAudioFrameStorageType source_storage_type_;
    SbMediaAudioSampleType destination_sample_type_;
    SbMediaAudioFrameStorageType destination_storage_type_;
    int destination_sample_rate_;
    int channels_;
};

}  // namespace gstreamer
}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_SHARED_GSTREAMER_AUDIO_RESAMPLER_H_
