#include "DMQuiltTask.h"
#include "DMUtil.h"
#include "DMWriteTask.h"

#include "SkBBHFactory.h"
#include "SkCommandLineFlags.h"
#include "SkPicture.h"
#include "SkTaskGroup.h"

DEFINE_bool(quilt, true, "If true, draw GM via a picture into a quilt of small tiles and compare.");
DEFINE_int32(quiltTile, 256, "Dimension of (square) quilt tile.");

namespace DM {

static SkString suffix(QuiltTask::Backend backend, QuiltTask::BBH bbh) {
    static const char* kBackends[] = { "default", "skrecord" };
    static const char* kBBHs[]     = { "nobbh", "rtree", "tilegrid" };
    return SkStringPrintf("%s-%s", kBackends[backend], kBBHs[bbh]);
}

QuiltTask::QuiltTask(const Task& parent, skiagm::GM* gm, SkBitmap reference,
                     QuiltTask::BBH bbh, QuiltTask::Backend backend)
    : CpuTask(parent)
    , fBBH(bbh)
    , fBackend(backend)
    , fName(UnderJoin(parent.name().c_str(), suffix(backend, bbh).c_str()))
    , fGM(gm)
    , fReference(reference)
    {}

static int tiles_needed(int fullDimension, int tileDimension) {
    return (fullDimension + tileDimension - 1) / tileDimension;
}

class Tile : public SkRunnable {
public:
    Tile(int x, int y, const SkPicture& picture, SkBitmap* quilt)
        : fX(x * FLAGS_quiltTile)
        , fY(y * FLAGS_quiltTile)
        , fPicture(picture)
        , fQuilt(quilt) {}

    virtual void run() SK_OVERRIDE {
        SkBitmap tile;
        fQuilt->extractSubset(&tile, SkIRect::MakeXYWH(fX, fY, FLAGS_quiltTile, FLAGS_quiltTile));
        SkCanvas tileCanvas(tile);

        tileCanvas.translate(SkIntToScalar(-fX), SkIntToScalar(-fY));
        fPicture.playback(&tileCanvas);
        tileCanvas.flush();

        delete this;
    }

private:
    const int fX, fY;
    const SkPicture& fPicture;
    SkBitmap* fQuilt;
};

void QuiltTask::draw() {
    SkAutoTDelete<SkBBHFactory> factory;
    switch (fBBH) {
        case kNone_BBH: break;
        case kRTree_BBH:
            factory.reset(SkNEW(SkRTreeFactory));
            break;
        case kTileGrid_BBH: {
            const SkTileGridFactory::TileGridInfo tiles = {
                { FLAGS_quiltTile, FLAGS_quiltTile },
                /*overlap: */{0, 0},
                /*offset:  */{0, 0},
            };
            factory.reset(SkNEW_ARGS(SkTileGridFactory, (tiles)));
            break;
        }
    }

    // A couple GMs draw wrong when using a bounding box hierarchy.
    // This almost certainly means we have a bug to fix, but for now
    // just draw without a bounding box hierarchy.
    if (fGM->getFlags() & skiagm::GM::kNoBBH_Flag) {
        factory.reset(NULL);
    }

    SkAutoTUnref<const SkPicture> recorded(
            RecordPicture(fGM.get(), factory.get(), kSkRecord_Backend == fBackend));

    SkBitmap full;
    AllocatePixels(fReference, &full);

    if (fGM->getFlags() & skiagm::GM::kSkipTiled_Flag) {
        // Some GMs don't draw exactly the same when tiled.  Draw them in one go.
        SkCanvas canvas(full);
        recorded->playback(&canvas);
        canvas.flush();
    } else {
        // Draw tiles in parallel into the same bitmap, simulating aggressive impl-side painting.
        SkTaskGroup tg;
        for (int y = 0; y < tiles_needed(full.height(), FLAGS_quiltTile); y++) {
            for (int x = 0; x < tiles_needed(full.width(), FLAGS_quiltTile); x++) {
                // Deletes itself when done.
                tg.add(new Tile(x, y, *recorded, &full));
            }
        }
    }

    if (!BitmapsEqual(full, fReference)) {
        this->fail();
        this->spawnChild(SkNEW_ARGS(WriteTask, (*this, "GM", full)));
    }
}

bool QuiltTask::shouldSkip() const {
    if (fGM->getFlags() & skiagm::GM::kSkipPicture_Flag) {
        return true;
    }
    return !FLAGS_quilt;
}

}  // namespace DM
