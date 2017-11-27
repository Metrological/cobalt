/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Test.h"

#include "../include/core/SkCanvas.h"
#include "../include/core/SkPicture.h"
#include "../include/core/SkStream.h"
#include "../include/core/SkString.h"
#include "../include/record/SkRecording.h"
#include "../include/core/SkPictureRecorder.h"
#include <cstring>

// Verify that replay of a recording into a clipped canvas
// produces the correct bitmap.
// This arose from http://crbug.com/401593 which has
// https://code.google.com/p/skia/issues/detail?id=1291 as its root cause.


namespace {

class Drawer {
 public:
    explicit Drawer()
            : fImageInfo(SkImageInfo::MakeN32Premul(200,100))
    {
        fCircleBM.allocPixels( SkImageInfo::MakeN32Premul(100,100) );
        SkCanvas canvas(fCircleBM);
        canvas.clear(0xffffffff);
        SkPaint circlePaint;
        circlePaint.setColor(0xff000000);
        canvas.drawCircle(50,50,50,circlePaint);
    }

    const SkImageInfo& imageInfo() const { return fImageInfo; }

    void draw(SkCanvas* canvas, const SkRect& clipRect, SkXfermode::Mode mode) const {
        SkPaint greenPaint;
        greenPaint.setColor(0xff008000);
        SkPaint blackPaint;
        blackPaint.setColor(0xff000000);
        SkPaint whitePaint;
        whitePaint.setColor(0xffffffff);
        SkPaint layerPaint;
        layerPaint.setColor(0xff000000);
        layerPaint.setXfermodeMode(mode);
        SkRect canvasRect(SkRect::MakeWH(SkIntToScalar(fImageInfo.width()),SkIntToScalar(fImageInfo.height())));

        canvas->clipRect(clipRect);
        canvas->clear(0xff000000);

        canvas->saveLayer(NULL,&blackPaint);
            canvas->drawRect(canvasRect,greenPaint);
            canvas->saveLayer(NULL,&layerPaint);
                canvas->drawBitmapRect(fCircleBM,SkRect::MakeXYWH(20,20,60,60),&blackPaint);
            canvas->restore();
        canvas->restore();
    }

 private:
    const SkImageInfo fImageInfo;
    SkBitmap fCircleBM;
};

class RecordingStrategy {
 public:
    virtual ~RecordingStrategy() {}
    virtual void init(const SkImageInfo&) = 0;
    virtual const SkBitmap& recordAndReplay(const Drawer& drawer,
                                            const SkRect& intoClip,
                                            SkXfermode::Mode) = 0;
};

class BitmapBackedCanvasStrategy : public RecordingStrategy {
    // This version just draws into a bitmap-backed canvas.
 public:
    BitmapBackedCanvasStrategy() {}

    virtual void init(const SkImageInfo& imageInfo) {
        fBitmap.allocPixels(imageInfo);
    }

    virtual const SkBitmap& recordAndReplay(const Drawer& drawer,
                                            const SkRect& intoClip,
                                            SkXfermode::Mode mode) {
        SkCanvas canvas(fBitmap);
        canvas.clear(0xffffffff);
        // Note that the scene is drawn just into the clipped region!
        canvas.clipRect(intoClip);
        drawer.draw(&canvas, intoClip, mode); // Shouild be canvas-wide...
        return fBitmap;
    }

 private:
    SkBitmap fBitmap;
};

class DeprecatedRecorderStrategy : public RecordingStrategy {
    // This version draws the entire scene into an SkPictureRecorder,
    // using the deprecated recording backend.
    // Then it then replays the scene through a clip rectangle.
    // This backend proved to be buggy.
 public:
    DeprecatedRecorderStrategy() {}

    virtual void init(const SkImageInfo& imageInfo) {
        fBitmap.allocPixels(imageInfo);
        fWidth = imageInfo.width();
        fHeight= imageInfo.height();
    }

    virtual const SkBitmap& recordAndReplay(const Drawer& drawer,
                                            const SkRect& intoClip,
                                            SkXfermode::Mode mode) {
        SkTileGridFactory::TileGridInfo tileGridInfo = { {100,100}, {0,0}, {0,0} };
        SkTileGridFactory factory(tileGridInfo);
        SkPictureRecorder recorder;
        SkRect canvasRect(SkRect::MakeWH(SkIntToScalar(fWidth),SkIntToScalar(fHeight)));
        SkCanvas* canvas = recorder.DEPRECATED_beginRecording( SkIntToScalar(fWidth), SkIntToScalar(fHeight), &factory);
        drawer.draw(canvas, canvasRect, mode);
        SkAutoTDelete<SkPicture> picture(recorder.endRecording());

        SkCanvas replayCanvas(fBitmap);
        replayCanvas.clear(0xffffffff);
        replayCanvas.clipRect(intoClip);
        picture->playback(&replayCanvas);

        return fBitmap;
    }

 private:
    SkBitmap fBitmap;
    int fWidth;
    int fHeight;
};

class NewRecordingStrategy : public RecordingStrategy {
    // This version draws the entire scene into an SkPictureRecorder,
    // using the new recording backend.
    // Then it then replays the scene through a clip rectangle.
    // This backend proved to be buggy.
 public:
    NewRecordingStrategy() {}

    virtual void init(const SkImageInfo& imageInfo) {
        fBitmap.allocPixels(imageInfo);
        fWidth = imageInfo.width();
        fHeight= imageInfo.height();
    }

    virtual const SkBitmap& recordAndReplay(const Drawer& drawer,
                                            const SkRect& intoClip,
                                            SkXfermode::Mode mode) {
        SkTileGridFactory::TileGridInfo tileGridInfo = { {100,100}, {0,0}, {0,0} };
        SkTileGridFactory factory(tileGridInfo);
        SkPictureRecorder recorder;
        SkRect canvasRect(SkRect::MakeWH(SkIntToScalar(fWidth),SkIntToScalar(fHeight)));
        SkCanvas* canvas = recorder.EXPERIMENTAL_beginRecording( SkIntToScalar(fWidth), SkIntToScalar(fHeight), &factory);

        drawer.draw(canvas, canvasRect, mode);
        SkAutoTDelete<SkPicture> picture(recorder.endRecording());

        SkCanvas replayCanvas(fBitmap);
        replayCanvas.clear(0xffffffff);
        replayCanvas.clipRect(intoClip);
        picture->playback(&replayCanvas);
        return fBitmap;
    }

 private:
    SkBitmap fBitmap;
    int fWidth;
    int fHeight;
};

}



DEF_TEST(SkRecordingAccuracyXfermode, reporter) {
#define FINEGRAIN 0

    const Drawer drawer;

    BitmapBackedCanvasStrategy golden; // This is the expected result.
    DeprecatedRecorderStrategy deprecatedRecording;
    NewRecordingStrategy newRecording;

    golden.init(drawer.imageInfo());
    deprecatedRecording.init(drawer.imageInfo());
    newRecording.init(drawer.imageInfo());

#if !FINEGRAIN
    unsigned numErrors = 0;
    SkString errors;
#endif

    for (int iMode = 0; iMode < int(SkXfermode::kLastMode) ; iMode++ ) {
        const SkRect& clip = SkRect::MakeXYWH(100,0,100,100);
        SkXfermode::Mode mode = SkXfermode::Mode(iMode);

        const SkBitmap& goldenBM = golden.recordAndReplay(drawer, clip, mode);
        const SkBitmap& deprecatedBM = deprecatedRecording.recordAndReplay(drawer, clip, mode);
        const SkBitmap& newRecordingBM = newRecording.recordAndReplay(drawer, clip, mode);

        size_t pixelsSize = goldenBM.getSize();
        REPORTER_ASSERT( reporter, pixelsSize == deprecatedBM.getSize() );
        REPORTER_ASSERT( reporter, pixelsSize == newRecordingBM.getSize() );

        // The pixel arrays should match.
#if FINEGRAIN
        REPORTER_ASSERT_MESSAGE( reporter,
                                 0==memcmp( goldenBM.getPixels(), deprecatedBM.getPixels(), pixelsSize ),
                                 "Tiled bitmap is wrong");
        REPORTER_ASSERT_MESSAGE( reporter,
                                 0==memcmp( goldenBM.getPixels(), recordingBM.getPixels(), pixelsSize ),
                                 "SkRecorder bitmap is wrong");
#else
        if ( memcmp( goldenBM.getPixels(), deprecatedBM.getPixels(), pixelsSize ) ) {
            numErrors++;
            SkString str;
            str.printf("For SkXfermode %d %s:    Deprecated recorder bitmap is wrong\n", iMode, SkXfermode::ModeName(mode));
            errors.append(str);
        }
        if ( memcmp( goldenBM.getPixels(), newRecordingBM.getPixels(), pixelsSize ) ) {
            numErrors++;
            SkString str;
            str.printf("For SkXfermode %d %s:    SkPictureRecorder bitmap is wrong\n", iMode, SkXfermode::ModeName(mode));
            errors.append(str);
        }
#endif
    }
#if !FINEGRAIN
    REPORTER_ASSERT_MESSAGE( reporter, 0==numErrors, errors.c_str() );
#endif
}
