#include "DMWriteTask.h"

#include "DMUtil.h"
#include "SkColorPriv.h"
#include "SkCommonFlags.h"
#include "SkData.h"
#include "SkImageEncoder.h"
#include "SkMD5.h"
#include "SkMallocPixelRef.h"
#include "SkOSFile.h"
#include "SkStream.h"
#include "SkString.h"

DEFINE_bool(nameByHash, false, "If true, write .../hash.png instead of .../mode/config/name.png");

namespace DM {

// Splits off the last N suffixes of name (splitting on _) and appends them to out.
// Returns the total number of characters consumed.
static int split_suffixes(int N, const char* name, SkTArray<SkString>* out) {
    SkTArray<SkString> split;
    SkStrSplit(name, "_", &split);
    int consumed = 0;
    for (int i = 0; i < N; i++) {
        // We're splitting off suffixes from the back to front.
        out->push_back(split[split.count()-i-1]);
        consumed += out->back().size() + 1;  // Add one for the _.
    }
    return consumed;
}

inline static SkString find_base_name(const Task& parent, SkTArray<SkString>* suffixList) {
    const int suffixes = parent.depth() + 1;
    const SkString& name = parent.name();
    const int totalSuffixLength = split_suffixes(suffixes, name.c_str(), suffixList);
    return SkString(name.c_str(), name.size() - totalSuffixLength);
}

WriteTask::WriteTask(const Task& parent, const char* sourceType, SkBitmap bitmap)
    : CpuTask(parent)
    , fBaseName(find_base_name(parent, &fSuffixes))
    , fSourceType(sourceType)
    , fBitmap(bitmap)
    , fData(NULL)
    , fExtension(".png") {
}

WriteTask::WriteTask(const Task& parent,
                     const char* sourceType,
                     SkStreamAsset *data,
                     const char* ext)
    : CpuTask(parent)
    , fBaseName(find_base_name(parent, &fSuffixes))
    , fSourceType(sourceType)
    , fData(data)
    , fExtension(ext) {
    SkASSERT(fData.get());
    SkASSERT(fData->unique());
}

void WriteTask::makeDirOrFail(SkString dir) {
    if (!sk_mkdir(dir.c_str())) {
        this->fail();
    }
}

static SkString get_md5(const void* ptr, size_t len) {
    SkMD5 hasher;
    hasher.write(ptr, len);
    SkMD5::Digest digest;
    hasher.finish(digest);

    SkString md5;
    for (int i = 0; i < 16; i++) {
        md5.appendf("%02x", digest.data[i]);
    }
    return md5;
}

struct JsonData {
    SkString name;            // E.g. "ninepatch-stretch", "desk-gws_skp"
    SkString config;          //      "gpu", "8888"
    SkString mode;            //      "direct", "default-tilegrid", "pipe"
    SkString sourceType;      //      "GM", "SKP"
    SkString md5;             // In ASCII, so 32 bytes long.
};
SkTArray<JsonData> gJsonData;
SK_DECLARE_STATIC_MUTEX(gJsonDataLock);

void WriteTask::draw() {
    SkString md5;
    {
        SkAutoLockPixels lock(fBitmap);
        md5 = fData ? get_md5(fData->getMemoryBase(), fData->getLength())
                    : get_md5(fBitmap.getPixels(), fBitmap.getSize());
    }

    SkASSERT(fSuffixes.count() > 0);
    SkString config = fSuffixes.back();
    SkString mode("direct");
    if (fSuffixes.count() > 1) {
        mode = fSuffixes.fromBack(1);
    }

    JsonData entry = { fBaseName, config, mode, fSourceType, md5 };
    {
        SkAutoMutexAcquire lock(&gJsonDataLock);
        gJsonData.push_back(entry);
    }

    SkString dir(FLAGS_writePath[0]);
#if SK_BUILD_FOR_IOS
    if (dir.equals("@")) {
        dir.set(FLAGS_resourcePath[0]);
    }
#endif
    this->makeDirOrFail(dir);

    SkString path;
    if (FLAGS_nameByHash) {
        // Flat directory of hash-named files.
        path = SkOSPath::Join(dir.c_str(), md5.c_str());
        path.append(fExtension);
        // We're content-addressed, so it's possible two threads race to write
        // this file.  We let the first one win.  This also means we won't
        // overwrite identical files from previous runs.
        if (sk_exists(path.c_str())) {
            return;
        }
    } else {
        // Nested by mode, config, etc.
        for (int i = 0; i < fSuffixes.count(); i++) {
            dir = SkOSPath::Join(dir.c_str(), fSuffixes[i].c_str());
            this->makeDirOrFail(dir);
        }
        path = SkOSPath::Join(dir.c_str(), fBaseName.c_str());
        path.append(fExtension);
        // The path is unique, so two threads can't both write to the same file.
        // If already present we overwrite here, since the content may have changed.
    }

    SkFILEWStream file(path.c_str());
    if (!file.isValid()) {
        return this->fail("Can't open file.");
    }

    bool ok = fData ? file.writeStream(fData, fData->getLength())
                    : SkImageEncoder::EncodeStream(&file, fBitmap, SkImageEncoder::kPNG_Type, 100);
    if (!ok) {
        return this->fail("Can't write to file.");
    }
}

SkString WriteTask::name() const {
    SkString name("writing ");
    for (int i = 0; i < fSuffixes.count(); i++) {
        name.appendf("%s/", fSuffixes[i].c_str());
    }
    name.append(fBaseName.c_str());
    return name;
}

bool WriteTask::shouldSkip() const {
    return FLAGS_writePath.isEmpty();
}

void WriteTask::DumpJson() {
    if (FLAGS_writePath.isEmpty()) {
        return;
    }

    Json::Value root;

    for (int i = 1; i < FLAGS_properties.count(); i += 2) {
        root[FLAGS_properties[i-1]] = FLAGS_properties[i];
    }
    for (int i = 1; i < FLAGS_key.count(); i += 2) {
        root["key"][FLAGS_key[i-1]] = FLAGS_key[i];
    }

    {
        SkAutoMutexAcquire lock(&gJsonDataLock);
        for (int i = 0; i < gJsonData.count(); i++) {
            Json::Value result;
            result["key"]["name"]            = gJsonData[i].name.c_str();
            result["key"]["config"]          = gJsonData[i].config.c_str();
            result["key"]["mode"]            = gJsonData[i].mode.c_str();
            result["options"]["source_type"] = gJsonData[i].sourceType.c_str();
            result["md5"]                    = gJsonData[i].md5.c_str();

            root["results"].append(result);
        }
    }

    SkString path = SkOSPath::Join(FLAGS_writePath[0], "dm.json");
    SkFILEWStream stream(path.c_str());
    stream.writeText(Json::StyledWriter().write(root).c_str());
    stream.flush();
}

}  // namespace DM
