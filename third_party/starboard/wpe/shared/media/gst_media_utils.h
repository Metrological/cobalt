#ifndef THIRD_PARTY_STARBOARD_WPE_SHARED_MEDIA_GST_MEDIA_UTILS_H_
#define THIRD_PARTY_STARBOARD_WPE_SHARED_MEDIA_GST_MEDIA_UTILS_H_

#include <string>
#include <vector>

#include "starboard/media.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace media {

bool GstRegistryHasElementForMediaType(SbMediaVideoCodec codec);
bool GstRegistryHasElementForMediaType(SbMediaAudioCodec codec);
std::vector<std::string> CodecToGstCaps(
    SbMediaAudioCodec codec,
    const SbMediaAudioSampleInfo* info = nullptr);
std::vector<std::string> CodecToGstCaps(SbMediaVideoCodec codec);

}  // namespace media
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party

#endif  // THIRD_PARTY_STARBOARD_WPE_SHARED_MEDIA_GST_MEDIA_UTILS_H_
