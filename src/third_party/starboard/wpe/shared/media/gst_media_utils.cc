#include <memory>
#include <type_traits>

#include <glib.h>
#include <gst/gst.h>

#include "base/logging.h"
#include "third_party/starboard/wpe/shared/media/gst_media_utils.h"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace media {
namespace {

struct FeatureListDeleter {
  void operator()(GList* p) { gst_plugin_feature_list_free(p); }
};

struct CapsDeleter {
  void operator()(GstCaps* p) { gst_caps_unref(p); }
};

using UniqueFeatureList = std::unique_ptr<GList, FeatureListDeleter>;
using UniqueCaps = std::unique_ptr<GstCaps, CapsDeleter>;

UniqueFeatureList GetFactoryForCaps(GList* elements,
                                    UniqueCaps&& caps,
                                    GstPadDirection direction) {
  DLOG(INFO) << __FUNCTION__ << ": " << gst_caps_to_string(caps.get());
  DCHECK(direction != GST_PAD_UNKNOWN);
  UniqueFeatureList candidates{
      gst_element_factory_list_filter(elements, caps.get(), direction, false)};
  return (candidates);
}

template <typename C>
bool GstRegistryHasElementForCodec(C codec) {
  static_assert(std::is_same<C, SbMediaVideoCodec>::value ||
                std::is_same<C, SbMediaAudioCodec>::value, "Invalid codec");
  auto type = std::is_same<C, SbMediaVideoCodec>::value
                  ? GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO
                  : GST_ELEMENT_FACTORY_TYPE_MEDIA_AUDIO;
  UniqueFeatureList parser_factories{gst_element_factory_list_get_elements(
      GST_ELEMENT_FACTORY_TYPE_PARSER | type, GST_RANK_MARGINAL)};
  UniqueFeatureList decoder_factories{gst_element_factory_list_get_elements(
      GST_ELEMENT_FACTORY_TYPE_DECODER | type, GST_RANK_MARGINAL)};
  UniqueFeatureList demuxer_factories{gst_element_factory_list_get_elements(
      GST_ELEMENT_FACTORY_TYPE_DEMUXER, GST_RANK_MARGINAL)};

  UniqueFeatureList elements;
  std::vector<std::string> caps;

  caps = CodecToGstCaps(codec);
  if (caps.empty()) {
    DLOG(INFO) << "No caps for codec " << codec;
    return false;
  }

  for (auto single_caps : caps) {
    UniqueCaps gst_caps{gst_caps_from_string(single_caps.c_str())};
    elements = std::move(GetFactoryForCaps(decoder_factories.get(),
                                           std::move(gst_caps), GST_PAD_SINK));
    if (elements) {
      DLOG(INFO) << "Found decoder for " << single_caps;
      break;
    }
  }

  if (elements) {
    // Decoder is there.
    return true;
  }

  DLOG(INFO) << "No decoder for codec " << codec << ". Falling back to parsers.";
  // No decoder. Check if there's a parser and a decoder accepting its caps.
  for (auto single_caps : caps) {
    UniqueCaps gst_caps{gst_caps_from_string(single_caps.c_str())};
    elements = std::move(GetFactoryForCaps(parser_factories.get(),
                                           std::move(gst_caps), GST_PAD_SINK));
    if (elements) {
      for (GList* iter = elements.get(); iter; iter = iter->next) {
        GstElementFactory* gst_element_factory =
            static_cast<GstElementFactory*>(iter->data);
        const GList* pad_templates =
            gst_element_factory_get_static_pad_templates(gst_element_factory);
        for (const GList* pad_templates_iter = pad_templates;
             pad_templates_iter;
             pad_templates_iter = pad_templates_iter->next) {
          GstStaticPadTemplate* pad_template =
              static_cast<GstStaticPadTemplate*>(pad_templates_iter->data);
          if (pad_template->direction == GST_PAD_SRC) {
            UniqueCaps pad_caps{gst_static_pad_template_get_caps(pad_template)};
            if (GetFactoryForCaps(decoder_factories.get(), std::move(pad_caps),
                                  GST_PAD_SINK)) {
              DLOG(INFO) << "Found parser for " << single_caps
                        << " and decoder"
                           " accepting parser's src caps.";
              return true;
            }
          }
        }
      }
    }
  }

  LOG(WARNING) << "Can not play codec " << codec;
  return false;
}

inline bool IsLittleEndian() {
  uint8_t number = 0x1;
  uint8_t* numPtr = static_cast<uint8_t*>(&number);

  return numPtr[0] == 1;
}

template <typename T>
inline T SwapByteOrder(T val) {
  T result;
  std::array<uint8_t, sizeof(T)> valArray;

  std::memcpy(valArray.data(), &val, sizeof(T));
  for (std::size_t i = 0; i < sizeof(T) / 2; ++i) {
    std::swap(valArray[sizeof(T) - 1 - i], valArray[i]);
  }
  std::memcpy(&result, valArray.data(), sizeof(T));
  return result;
}

template <typename T>
inline std::string IntToHex(T val, bool asBigEndian = false) {
  std::stringstream result;
  std::size_t width = sizeof(T) * 2;

  result << std::hex << std::setfill('0') << std::setw(width);

  if (IsLittleEndian()) {
    if (asBigEndian) {
      result << SwapByteOrder<T>(val | 0);
    } else {
      result << (val | 0);
    }
  } else {
    if (asBigEndian) {
      result << (val | 0);
    } else {
      result << SwapByteOrder<T>(val | 0);
    }
  }

  return result.str();
}

std::string StringToHex(const std::string& string) {
  std::ostringstream result;

  for (std::size_t i = 0; i < string.length(); ++i) {
    result << std::hex << std::setfill('0') << static_cast<int>(string[i]);
  }

  return result.str();
}

}  // namespace

bool GstRegistryHasElementForMediaType(SbMediaVideoCodec codec) {
  return GstRegistryHasElementForCodec(codec);
}

bool GstRegistryHasElementForMediaType(SbMediaAudioCodec codec) {
  return GstRegistryHasElementForCodec(codec);
}

std::vector<std::string> CodecToGstCaps(SbMediaVideoCodec codec) {
  switch (codec) {
    default:
    case kSbMediaVideoCodecNone:
      return {};

    case kSbMediaVideoCodecH264:
      return {{"video/x-h264, stream-format=byte-stream, alignment=nal"}};

    case kSbMediaVideoCodecH265:
      return {
          {"video/x-h265"},
      };

    case kSbMediaVideoCodecMpeg2:
      return {{"video/mpeg, mpegversion=(int) 2"}};

    case kSbMediaVideoCodecTheora:
      return {{"video/x-theora"}};

    case kSbMediaVideoCodecVc1:
      return {{"video/x-vc1"}};

#if SB_API_VERSION < 11
    case kSbMediaVideoCodecVp10:
      return {{"video/x-vp10"}};
#else   // SB_API_VERSION < 11
    case kSbMediaVideoCodecAv1:
      return {};
#endif  // SB_API_VERSION < 11

    case kSbMediaVideoCodecVp8:
      return {{"video/x-vp8"}};

    case kSbMediaVideoCodecVp9:
      return {{"video/x-vp9"}};
  }
}

std::vector<std::string> CodecToGstCaps(SbMediaAudioCodec codec,
                                        const SbMediaAudioSampleInfo* info) {
  switch (codec) {
    default:
    case kSbMediaAudioCodecNone:
      return {};

    case kSbMediaAudioCodecAac: {
      std::string primary_caps = "audio/mpeg, mpegversion=4";
      if (info) {
        primary_caps +=
            ", channels=" + std::to_string(info->number_of_channels);
        primary_caps += ", rate=" + std::to_string(info->samples_per_second);
        LOG(INFO) << "Adding audio caps data from sample info.";
      }
      return {{primary_caps}, {"audio/aac"}};
    }

#if SB_HAS(AC3_AUDIO)
    case kSbMediaAudioCodecAc3:
    case kSbMediaAudioCodecEac3:
      return {{"audio/x-eac3"}};
#endif  // SB_HAS(AC3_AUDIO)
    case kSbMediaAudioCodecOpus:{ 
      std::string caps = "audio/x-opus, channel-mapping-family=0, streamheader=(buffer)<";
      
      //if value larger than 1 byte, use big endianess for proper reading from buffer on the gstreamer side
      caps += StringToHex("OpusHead");                                                         // OpusHeader
      caps += IntToHex(static_cast<uint8_t>(1));                                               // version, always 1
      caps += IntToHex(static_cast<uint8_t>(info ? info->number_of_channels : 1));             // channel count
      caps += IntToHex(static_cast<uint16_t>(0), true);                                        // preskip
      caps += IntToHex(static_cast<uint32_t>(info ? info->samples_per_second : 48000), true);  // inputSampleRate
      caps += IntToHex(static_cast<uint16_t>(0), true);                                        // outputgain
      caps += IntToHex(static_cast<uint8_t>(0));                                               // mappingFamily
      caps += IntToHex(static_cast<uint8_t>(0));                                               // padding
      caps += ",";
      caps += StringToHex("OpusTags");                                                         // Additional OpusTags, unused parameter
      caps += ">;";
      
      return {caps};
      
    }

    case kSbMediaAudioCodecVorbis:{
      std::string caps = "audio/x-vorbis"; 
      if (info) {
        caps += ", channels=" + std::to_string(info->number_of_channels);
        caps += ", rate=" + std::to_string(info->samples_per_second);
        LOG(INFO) << "Adding audio caps data from sample info.";
      }
      else{
        caps += ", channels=1";
        caps += ", rate=48000";
      }
      return {caps};
    }
  }
}

}  // namespace media
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
