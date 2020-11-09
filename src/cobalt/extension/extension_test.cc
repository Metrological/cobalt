// Copyright 2019 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cmath>

#include "cobalt/extension/configuration.h"
#include "cobalt/extension/graphics.h"
#include "cobalt/extension/installation_manager.h"
#include "cobalt/extension/platform_service.h"
#include "starboard/system.h"
#include "testing/gtest/include/gtest/gtest.h"

#if SB_API_VERSION >= 11
namespace cobalt {
namespace extension {

TEST(ExtensionTest, PlatformService) {
  typedef CobaltExtensionPlatformServiceApi ExtensionApi;
  const char* kExtensionName = kCobaltExtensionPlatformServiceName;

  const ExtensionApi* extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  if (!extension_api) {
    return;
  }

  EXPECT_STREQ(extension_api->name, kExtensionName);
  EXPECT_GE(extension_api->version, 1u);
  EXPECT_LE(extension_api->version, 3u);
  EXPECT_NE(extension_api->Has, nullptr);
  EXPECT_NE(extension_api->Open, nullptr);
  EXPECT_NE(extension_api->Close, nullptr);
  EXPECT_NE(extension_api->Send, nullptr);

  const ExtensionApi* second_extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  EXPECT_EQ(second_extension_api, extension_api)
      << "Extension struct should be a singleton";
}

TEST(ExtensionTest, Graphics) {
  typedef CobaltExtensionGraphicsApi ExtensionApi;
  const char* kExtensionName = kCobaltExtensionGraphicsName;

  const ExtensionApi* extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  if (!extension_api) {
    return;
  }

  EXPECT_STREQ(extension_api->name, kExtensionName);
  EXPECT_GE(extension_api->version, 1u);
  EXPECT_LE(extension_api->version, 4u);

  EXPECT_NE(extension_api->GetMaximumFrameIntervalInMilliseconds, nullptr);
  float maximum_frame_interval =
      extension_api->GetMaximumFrameIntervalInMilliseconds();
  EXPECT_FALSE(std::isnan(maximum_frame_interval));

  if (extension_api->version >= 2) {
    EXPECT_NE(extension_api->GetMinimumFrameIntervalInMilliseconds, nullptr);
    float minimum_frame_interval =
        extension_api->GetMinimumFrameIntervalInMilliseconds();
    EXPECT_GT(minimum_frame_interval, 0);
  }

  if (extension_api->version >= 3) {
    EXPECT_NE(extension_api->IsMapToMeshEnabled, nullptr);
  }

  if (extension_api->version >= 4) {
    EXPECT_NE(extension_api->ShouldClearFrameOnShutdown, nullptr);
    float clear_color_r, clear_color_g, clear_color_b, clear_color_a;
    if (extension_api->ShouldClearFrameOnShutdown(
            &clear_color_r, &clear_color_g, &clear_color_b, &clear_color_a)) {
      EXPECT_GE(clear_color_r, 0.0f);
      EXPECT_LE(clear_color_r, 1.0f);
      EXPECT_GE(clear_color_g, 0.0f);
      EXPECT_LE(clear_color_g, 1.0f);
      EXPECT_GE(clear_color_b, 0.0f);
      EXPECT_LE(clear_color_b, 1.0f);
      EXPECT_GE(clear_color_a, 0.0f);
      EXPECT_LE(clear_color_a, 1.0f);
    }
  }

  const ExtensionApi* second_extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  EXPECT_EQ(second_extension_api, extension_api)
      << "Extension struct should be a singleton";
}

TEST(ExtensionTest, InstallationManager) {
  typedef CobaltExtensionInstallationManagerApi ExtensionApi;
  const char* kExtensionName = kCobaltExtensionInstallationManagerName;

  const ExtensionApi* extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  if (!extension_api) {
    return;
  }

  EXPECT_STREQ(extension_api->name, kExtensionName);
  EXPECT_GE(extension_api->version, 1u);
  EXPECT_LE(extension_api->version, 3u);
  EXPECT_NE(extension_api->GetCurrentInstallationIndex, nullptr);
  EXPECT_NE(extension_api->MarkInstallationSuccessful, nullptr);
  EXPECT_NE(extension_api->RequestRollForwardToInstallation, nullptr);
  EXPECT_NE(extension_api->GetInstallationPath, nullptr);
  EXPECT_NE(extension_api->SelectNewInstallationIndex, nullptr);
  EXPECT_NE(extension_api->GetAppKey, nullptr);
  EXPECT_NE(extension_api->GetMaxNumberInstallations, nullptr);
  EXPECT_NE(extension_api->ResetInstallation, nullptr);
  EXPECT_NE(extension_api->Reset, nullptr);
  const ExtensionApi* second_extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  EXPECT_EQ(second_extension_api, extension_api)
      << "Extension struct should be a singleton";
}

TEST(ExtensionTest, Configuration) {
  typedef CobaltExtensionConfigurationApi ExtensionApi;
  const char* kExtensionName = kCobaltExtensionConfigurationName;

  const ExtensionApi* extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  if (!extension_api) {
    return;
  }

  EXPECT_STREQ(extension_api->name, kExtensionName);
  EXPECT_GE(extension_api->version, 1u);
  EXPECT_LE(extension_api->version, 2u);
  EXPECT_NE(extension_api->CobaltUserOnExitStrategy, nullptr);
  EXPECT_NE(extension_api->CobaltRenderDirtyRegionOnly, nullptr);
  EXPECT_NE(extension_api->CobaltEglSwapInterval, nullptr);
  EXPECT_NE(extension_api->CobaltFallbackSplashScreenUrl, nullptr);
  EXPECT_NE(extension_api->CobaltEnableQuic, nullptr);
  EXPECT_NE(extension_api->CobaltSkiaCacheSizeInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltOffscreenTargetCacheSizeInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltEncodedImageCacheSizeInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltImageCacheSizeInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltLocalTypefaceCacheSizeInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltRemoteTypefaceCacheSizeInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltMeshCacheSizeInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltSoftwareSurfaceCacheSizeInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltImageCacheCapacityMultiplierWhenPlayingVideo,
            nullptr);
  EXPECT_NE(extension_api->CobaltSkiaGlyphAtlasWidth, nullptr);
  EXPECT_NE(extension_api->CobaltSkiaGlyphAtlasHeight, nullptr);
  EXPECT_NE(extension_api->CobaltJsGarbageCollectionThresholdInBytes, nullptr);
  EXPECT_NE(extension_api->CobaltReduceCpuMemoryBy, nullptr);
  EXPECT_NE(extension_api->CobaltReduceGpuMemoryBy, nullptr);
  EXPECT_NE(extension_api->CobaltGcZeal, nullptr);
  if (extension_api->version >= 2) {
    EXPECT_NE(extension_api->CobaltFallbackSplashScreenTopics, nullptr);
  }

  const ExtensionApi* second_extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  EXPECT_EQ(second_extension_api, extension_api)
      << "Extension struct should be a singleton";
}
}  // namespace extension
}  // namespace cobalt
#endif  // SB_API_VERSION >= 11
