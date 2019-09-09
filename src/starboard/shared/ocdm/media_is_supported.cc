#include "starboard/shared/ocdm/drm_system_ocdm.h"

SB_EXPORT bool SbMediaIsSupported(SbMediaVideoCodec video_codec,
        SbMediaAudioCodec audio_codec, const char *key_system) {
    using starboard::shared::ocdm::DrmSystemOcdm;

    SB_UNREFERENCED_PARAMETER(video_codec);
    SB_UNREFERENCED_PARAMETER(audio_codec);

    return DrmSystemOcdm::IsKeySystemSupported(key_system);
    return false;
}
