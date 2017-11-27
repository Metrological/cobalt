#include "starboard/wpe/shared/gstreamer/gst_audio_resampler.h"

namespace starboard {
namespace shared {
namespace gstreamer {

AudioResampler::AudioResampler(
        SbMediaAudioSampleType source_sample_type,
        SbMediaAudioFrameStorageType source_storage_type,
        int source_sample_rate,
        SbMediaAudioSampleType destination_sample_type,
        SbMediaAudioFrameStorageType destination_storage_type,
        int destination_sample_rate,
        int channels)
: source_sample_type_(source_sample_type),
  source_storage_type_(source_storage_type),
  destination_sample_type_(destination_sample_type),
  destination_storage_type_(destination_storage_type),
  destination_sample_rate_(destination_sample_rate),
  channels_(channels)
{
}

AudioResampler::~AudioResampler()
{
}

scoped_refptr<AudioResampler::DecodedAudio> AudioResampler::Resample(
        const scoped_refptr<DecodedAudio>& audio_data) SB_OVERRIDE {
    SB_DCHECK(audio_data->sample_type() == source_sample_type_);
    SB_DCHECK(audio_data->storage_type() == source_storage_type_);
    SB_DCHECK(audio_data->channels() == channels_);
    audio_data->SwitchFormatTo(destination_sample_type_,
            destination_storage_type_);
    return audio_data;
}

scoped_refptr<AudioResampler::DecodedAudio> AudioResampler::WriteEndOfStream() SB_OVERRIDE {
    return new DecodedAudio;
}

} // namespace gstreamer

namespace starboard {
namespace player {
namespace filter {

// static
scoped_ptr<AudioResampler> AudioResampler::Create(
        SbMediaAudioSampleType source_sample_type,
        SbMediaAudioFrameStorageType source_storage_type,
        int source_sample_rate,
        SbMediaAudioSampleType destination_sample_type,
        SbMediaAudioFrameStorageType destination_storage_type,
        int destination_sample_rate,
        int channels) {
    return scoped_ptr<AudioResampler>(new gstreamer::AudioResampler(
            source_sample_type, source_storage_type, source_sample_rate,
            destination_sample_type, destination_storage_type,
            destination_sample_rate, channels));
}

}  // namespace filter
}  // namespace player
}  // namespace starboard

}  // namespace shared
}  // namespace starboard
