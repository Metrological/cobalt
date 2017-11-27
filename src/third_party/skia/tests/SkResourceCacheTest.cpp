/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Test.h"
#include "SkBitmapCache.h"
#include "SkCanvas.h"
#include "SkDiscardableMemoryPool.h"
#include "SkGraphics.h"
#include "SkResourceCache.h"

static const int kCanvasSize = 1;
static const int kBitmapSize = 16;
static const int kScale = 8;

static bool is_in_scaled_image_cache(const SkBitmap& orig,
                                     SkScalar xScale,
                                     SkScalar yScale) {
    SkBitmap scaled;
    float roundedImageWidth = SkScalarRoundToScalar(orig.width() * xScale);
    float roundedImageHeight = SkScalarRoundToScalar(orig.height() * xScale);
    return SkBitmapCache::Find(orig, roundedImageWidth, roundedImageHeight, &scaled);
}

// Draw a scaled bitmap, then return true iff it has been cached.
static bool test_scaled_image_cache_useage() {
    SkAutoTUnref<SkCanvas> canvas(
            SkCanvas::NewRasterN32(kCanvasSize, kCanvasSize));
    SkBitmap bitmap;
    bitmap.allocN32Pixels(kBitmapSize, kBitmapSize);
    bitmap.eraseColor(0xFFFFFFFF);
    SkScalar scale = SkIntToScalar(kScale);
    SkScalar scaledSize = SkIntToScalar(kBitmapSize) * scale;
    canvas->clipRect(SkRect::MakeLTRB(0, 0, scaledSize, scaledSize));
    SkPaint paint;
    paint.setFilterLevel(SkPaint::kHigh_FilterLevel);

    canvas->drawBitmapRect(bitmap,
                           SkRect::MakeLTRB(0, 0, scaledSize, scaledSize),
                           &paint);

    return is_in_scaled_image_cache(bitmap, scale, scale);
}

// http://crbug.com/389439
DEF_TEST(ResourceCache_SingleAllocationByteLimit, reporter) {
    size_t originalByteLimit = SkGraphics::GetResourceCacheTotalByteLimit();
    size_t originalAllocationLimit =
        SkGraphics::GetResourceCacheSingleAllocationByteLimit();

    size_t size = kBitmapSize * kScale * kBitmapSize * kScale
        * SkColorTypeBytesPerPixel(kN32_SkColorType);

    SkGraphics::SetResourceCacheTotalByteLimit(0);  // clear cache
    SkGraphics::SetResourceCacheTotalByteLimit(2 * size);
    SkGraphics::SetResourceCacheSingleAllocationByteLimit(0);  // No limit

    REPORTER_ASSERT(reporter, test_scaled_image_cache_useage());

    SkGraphics::SetResourceCacheTotalByteLimit(0);  // clear cache
    SkGraphics::SetResourceCacheTotalByteLimit(2 * size);
    SkGraphics::SetResourceCacheSingleAllocationByteLimit(size * 2);  // big enough

    REPORTER_ASSERT(reporter, test_scaled_image_cache_useage());

    SkGraphics::SetResourceCacheTotalByteLimit(0);  // clear cache
    SkGraphics::SetResourceCacheTotalByteLimit(2 * size);
    SkGraphics::SetResourceCacheSingleAllocationByteLimit(size / 2);  // too small

    REPORTER_ASSERT(reporter, !test_scaled_image_cache_useage());

    SkGraphics::SetResourceCacheSingleAllocationByteLimit(originalAllocationLimit);
    SkGraphics::SetResourceCacheTotalByteLimit(originalByteLimit);
}

////////////////////////////////////////////////////////////////////////////////////////

static void make_bitmap(SkBitmap* bitmap, const SkImageInfo& info, SkBitmap::Allocator* allocator) {
    if (allocator) {
        bitmap->setInfo(info);
        allocator->allocPixelRef(bitmap, 0);
    } else {
        bitmap->allocPixels(info);
    }
}

// http://skbug.com/2894
DEF_TEST(BitmapCache_add_rect, reporter) {
    SkResourceCache::DiscardableFactory factory = SkResourceCache::GetDiscardableFactory();
    SkBitmap::Allocator* allocator = SkBitmapCache::GetAllocator();

    SkAutoTDelete<SkResourceCache> cache;
    if (factory) {
        cache.reset(SkNEW_ARGS(SkResourceCache, (factory)));
    } else {
        const size_t byteLimit = 100 * 1024;
        cache.reset(SkNEW_ARGS(SkResourceCache, (byteLimit)));
    }
    SkBitmap cachedBitmap;
    make_bitmap(&cachedBitmap, SkImageInfo::MakeN32Premul(5, 5), allocator);
    cachedBitmap.setImmutable();

    SkBitmap bm;
    SkIRect rect = SkIRect::MakeWH(5, 5);

    // Wrong subset size
    REPORTER_ASSERT(reporter, !SkBitmapCache::Add(cachedBitmap.getGenerationID(), SkIRect::MakeWH(4, 6), cachedBitmap, cache));
    REPORTER_ASSERT(reporter, !SkBitmapCache::Find(cachedBitmap.getGenerationID(), rect, &bm, cache));
    // Wrong offset value
    REPORTER_ASSERT(reporter, !SkBitmapCache::Add(cachedBitmap.getGenerationID(), SkIRect::MakeXYWH(-1, 0, 5, 5), cachedBitmap, cache));
    REPORTER_ASSERT(reporter, !SkBitmapCache::Find(cachedBitmap.getGenerationID(), rect, &bm, cache));

    // Should not be in the cache
    REPORTER_ASSERT(reporter, !SkBitmapCache::Find(cachedBitmap.getGenerationID(), rect, &bm, cache));

    REPORTER_ASSERT(reporter, SkBitmapCache::Add(cachedBitmap.getGenerationID(), rect, cachedBitmap, cache));
    // Should be in the cache, we just added it
    REPORTER_ASSERT(reporter, SkBitmapCache::Find(cachedBitmap.getGenerationID(), rect, &bm, cache));
}

DEF_TEST(BitmapCache_discarded_bitmap, reporter) {
    SkResourceCache::DiscardableFactory factory = SkResourceCache::GetDiscardableFactory();
    SkBitmap::Allocator* allocator = SkBitmapCache::GetAllocator();
    
    SkAutoTDelete<SkResourceCache> cache;
    if (factory) {
        cache.reset(SkNEW_ARGS(SkResourceCache, (factory)));
    } else {
        const size_t byteLimit = 100 * 1024;
        cache.reset(SkNEW_ARGS(SkResourceCache, (byteLimit)));
    }
    SkBitmap cachedBitmap;
    make_bitmap(&cachedBitmap, SkImageInfo::MakeN32Premul(5, 5), allocator);
    cachedBitmap.setImmutable();
    cachedBitmap.unlockPixels();

    SkBitmap bm;
    SkIRect rect = SkIRect::MakeWH(5, 5);

    // Add a bitmap to the cache.
    REPORTER_ASSERT(reporter, SkBitmapCache::Add(cachedBitmap.getGenerationID(), rect, cachedBitmap, cache));
    REPORTER_ASSERT(reporter, SkBitmapCache::Find(cachedBitmap.getGenerationID(), rect, &bm, cache));

    // Finding more than once works fine.
    REPORTER_ASSERT(reporter, SkBitmapCache::Find(cachedBitmap.getGenerationID(), rect, &bm, cache));
    bm.unlockPixels();

    // Drop the pixels in the bitmap.
    if (factory) {
        REPORTER_ASSERT(reporter, SkGetGlobalDiscardableMemoryPool()->getRAMUsed() > 0);
        SkGetGlobalDiscardableMemoryPool()->dumpPool();
        REPORTER_ASSERT(reporter, SkGetGlobalDiscardableMemoryPool()->getRAMUsed() == 0);

        // The bitmap is not in the cache since it has been dropped.
        REPORTER_ASSERT(reporter, !SkBitmapCache::Find(cachedBitmap.getGenerationID(), rect, &bm, cache));
    }

    make_bitmap(&cachedBitmap, SkImageInfo::MakeN32Premul(5, 5), allocator);
    cachedBitmap.setImmutable();
    cachedBitmap.unlockPixels();

    // We can add the bitmap back to the cache and find it again.
    REPORTER_ASSERT(reporter, SkBitmapCache::Add(cachedBitmap.getGenerationID(), rect, cachedBitmap, cache));
    REPORTER_ASSERT(reporter, SkBitmapCache::Find(cachedBitmap.getGenerationID(), rect, &bm, cache));
}
