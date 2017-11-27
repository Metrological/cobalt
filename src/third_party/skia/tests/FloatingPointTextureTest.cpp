/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is a straightforward test of floating point textures, which are
 * supported on some platforms.  As of right now, this test only supports
 * 32 bit floating point textures, and indeed floating point test values
 * have been selected to require 32 bits of precision and full IEEE conformance
 */
#if SK_SUPPORT_GPU
#include <float.h>
#include "Test.h"
#include "GrContext.h"
#include "GrTexture.h"
#include "GrContextFactory.h"
#include "SkGpuDevice.h"

static const int DEV_W = 100, DEV_H = 100;
static const int FP_CONTROL_ARRAY_SIZE = DEV_W * DEV_H * sizeof(float);
static const float kMaxIntegerRepresentableInSPFloatingPoint = 16777216;  // 2 ^ 24

static const SkIRect DEV_RECT = SkIRect::MakeWH(DEV_W, DEV_H);

DEF_GPUTEST(FloatingPointTextureTest, reporter, factory) {
    float controlPixelData[FP_CONTROL_ARRAY_SIZE];
    float readBuffer[FP_CONTROL_ARRAY_SIZE];
    for (int i = 0; i < FP_CONTROL_ARRAY_SIZE; i += 4) {
        controlPixelData[i] = FLT_MIN;
        controlPixelData[i + 1] = FLT_MAX;
        controlPixelData[i + 2] = FLT_EPSILON;
        controlPixelData[i + 3] = kMaxIntegerRepresentableInSPFloatingPoint;
    }

    for (int origin = 0; origin < 2; ++origin) {
        int glCtxTypeCnt = 1;
        glCtxTypeCnt = GrContextFactory::kGLContextTypeCnt;
        for (int glCtxType = 0; glCtxType < glCtxTypeCnt; ++glCtxType) {
            GrTextureDesc desc;
            desc.fFlags = kRenderTarget_GrTextureFlagBit;
            desc.fWidth = DEV_W;
            desc.fHeight = DEV_H;
            desc.fConfig = kRGBA_float_GrPixelConfig;
            desc.fOrigin = 0 == origin ?
                kTopLeft_GrSurfaceOrigin : kBottomLeft_GrSurfaceOrigin;

            GrContext* context = NULL;
            GrContextFactory::GLContextType type =
                    static_cast<GrContextFactory::GLContextType>(glCtxType);
            if (!GrContextFactory::IsRenderingGLContext(type)) {
                continue;
            }
            context = factory->get(type);
            if (NULL == context){
                continue;
            }

            SkAutoTUnref<GrTexture> fpTexture(context->createUncachedTexture(desc,
                                                                             NULL,
                                                                             0));

            // Floating point textures are NOT supported everywhere
            if (NULL == fpTexture) {
                continue;
            }

            // write square
            context->writeTexturePixels(fpTexture, 0, 0, DEV_W, DEV_H, desc.fConfig,
                    controlPixelData, 0);
            context->readTexturePixels(fpTexture, 0, 0, DEV_W, DEV_H, desc.fConfig, readBuffer, 0);
            for (int j = 0; j < FP_CONTROL_ARRAY_SIZE; ++j) {
                REPORTER_ASSERT(reporter, readBuffer[j] == controlPixelData[j]);
            }
        }
    }
}

#endif
