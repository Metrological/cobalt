// Copyright 2014 Google Inc. All Rights Reserved.
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

#include "cobalt/renderer/rasterizer/skia/hardware_rasterizer.h"

#include <algorithm>

#include "base/containers/linked_hash_map.h"
#include "base/debug/trace_event.h"
#include "cobalt/renderer/backend/egl/graphics_context.h"
#include "cobalt/renderer/backend/egl/graphics_system.h"
#include "cobalt/renderer/backend/egl/texture.h"
#include "cobalt/renderer/backend/egl/utils.h"
#include "cobalt/renderer/rasterizer/common/surface_cache.h"
#include "cobalt/renderer/rasterizer/egl/textured_mesh_renderer.h"
#include "cobalt/renderer/rasterizer/skia/cobalt_skia_type_conversions.h"
#include "cobalt/renderer/rasterizer/skia/hardware_mesh.h"
#include "cobalt/renderer/rasterizer/skia/hardware_resource_provider.h"
#include "cobalt/renderer/rasterizer/skia/render_tree_node_visitor.h"
#include "cobalt/renderer/rasterizer/skia/scratch_surface_cache.h"
#include "cobalt/renderer/rasterizer/skia/surface_cache_delegate.h"
#include "cobalt/renderer/rasterizer/skia/vertex_buffer_object.h"
#include "third_party/glm/glm/gtc/matrix_inverse.hpp"
#include "third_party/glm/glm/gtx/transform.hpp"
#include "third_party/glm/glm/mat3x3.hpp"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/gpu/GrContext.h"
#include "third_party/skia/include/gpu/GrTexture.h"
#include "third_party/skia/include/gpu/SkGrPixelRef.h"
#include "third_party/skia/src/gpu/SkGpuDevice.h"

namespace {
  // Some clients call Submit() multiple times with up to 2 different render
  // targets each frame, so the max must be at least 2 to avoid constantly
  // generating new surfaces.
  static const size_t kMaxSkSurfaceCount = 2;
}

namespace cobalt {
namespace renderer {
namespace rasterizer {
namespace skia {

class HardwareRasterizer::Impl {
 public:
  Impl(backend::GraphicsContext* graphics_context, int skia_atlas_width,
       int skia_atlas_height, int skia_cache_size_in_bytes,
       int scratch_surface_cache_size_in_bytes, int surface_cache_size_in_bytes,
       bool purge_skia_font_caches_on_destruction);
  ~Impl();

  void AdvanceFrame();

  void Submit(const scoped_refptr<render_tree::Node>& render_tree,
              const scoped_refptr<backend::RenderTarget>& render_target,
              const Options& options);

  void SubmitOffscreen(const scoped_refptr<render_tree::Node>& render_tree,
                       SkCanvas* canvas);

  void SubmitOffscreenToRenderTarget(
      const scoped_refptr<render_tree::Node>& render_tree,
      const scoped_refptr<backend::RenderTarget>& render_target);

  SkCanvas* GetCanvasFromRenderTarget(
      const scoped_refptr<backend::RenderTarget>& render_target);

  render_tree::ResourceProvider* GetResourceProvider();
  GrContext* GetGrContext();

  void MakeCurrent();

 private:
  // Note: We cannot store a SkAutoTUnref<SkSurface> in the map because it is
  // not copyable; so we must manually manage our references when adding /
  // removing SkSurfaces from it.
  typedef base::linked_hash_map<int32_t, SkSurface*> SkSurfaceMap;
  class CachedScratchSurfaceHolder
      : public RenderTreeNodeVisitor::ScratchSurface {
   public:
    CachedScratchSurfaceHolder(ScratchSurfaceCache* cache,
                               const math::Size& size)
        : cached_scratch_surface_(cache, size) {}
    SkSurface* GetSurface() OVERRIDE {
      return cached_scratch_surface_.GetSurface();
    }

   private:
    CachedScratchSurface cached_scratch_surface_;
  };

  SkSurface* CreateSkSurface(const math::Size& size);
  scoped_ptr<RenderTreeNodeVisitor::ScratchSurface> CreateScratchSurface(
      const math::Size& size);

  void RasterizeRenderTreeToCanvas(
      const scoped_refptr<render_tree::Node>& render_tree, SkCanvas* canvas,
      GrSurfaceOrigin origin);

  void ResetSkiaState();

  void RenderTextureEGL(const render_tree::ImageNode* image_node,
                        RenderTreeNodeVisitorDrawState* draw_state);
  void RenderTextureWithMeshFilterEGL(
      const render_tree::ImageNode* image_node,
      const render_tree::MapToMeshFilter& mesh_filter,
      RenderTreeNodeVisitorDrawState* draw_state);

  base::ThreadChecker thread_checker_;

  backend::GraphicsContextEGL* graphics_context_;
  scoped_ptr<render_tree::ResourceProvider> resource_provider_;

  SkAutoTUnref<GrContext> gr_context_;

  SkSurfaceMap sk_output_surface_map_;

  base::optional<ScratchSurfaceCache> scratch_surface_cache_;

  base::optional<SurfaceCacheDelegate> surface_cache_delegate_;
  base::optional<common::SurfaceCache> surface_cache_;

  base::optional<egl::TexturedMeshRenderer> textured_mesh_renderer_;

  // Valid only for the duration of a call to RasterizeRenderTreeToCanvas().
  // Useful for directing textured_mesh_renderer_ on whether to flip its y-axis
  // or not since Skia does not let us pull that information out of the
  // SkCanvas object (which Skia would internally use to get this information).
  base::optional<GrSurfaceOrigin> current_surface_origin_;
};

namespace {

SkSurfaceProps GetRenderTargetSurfaceProps() {
  return SkSurfaceProps(SkSurfaceProps::kUseDistanceFieldFonts_Flag,
                        SkSurfaceProps::kLegacyFontHost_InitType);
}

SkSurface* CreateSkiaRenderTargetSurface(GrRenderTarget* render_target) {
  SkSurfaceProps surface_props = GetRenderTargetSurfaceProps();
  return SkSurface::NewRenderTargetDirect(render_target, &surface_props);
}

// Takes meta-data from a Cobalt RenderTarget object and uses it to fill out
// a Skia backend render target descriptor.  Additionally, it also references
// the actual render target object as well so that Skia can then recover
// the Cobalt render target object.
GrBackendRenderTargetDesc CobaltRenderTargetToSkiaBackendRenderTargetDesc(
    cobalt::renderer::backend::RenderTarget* cobalt_render_target) {
  const math::Size& size = cobalt_render_target->GetSize();

  GrBackendRenderTargetDesc skia_desc;
  skia_desc.fWidth = size.width();
  skia_desc.fHeight = size.height();
  skia_desc.fConfig = kRGBA_8888_GrPixelConfig;
  skia_desc.fOrigin = kBottomLeft_GrSurfaceOrigin;
  skia_desc.fSampleCnt = 0;
  skia_desc.fStencilBits = 0;
  skia_desc.fRenderTargetHandle =
      static_cast<GrBackendObject>(cobalt_render_target->GetPlatformHandle());

  return skia_desc;
}

glm::mat4 ModelViewMatrixSurfaceOriginAdjustment(
    GrSurfaceOrigin origin) {
  if (origin == kTopLeft_GrSurfaceOrigin) {
    return glm::scale(glm::vec3(1.0f, -1.0f, 1.0f));
  } else {
    return glm::mat4(1.0f);
  }
}

glm::mat4 GetFallbackTextureModelViewProjectionMatrix(
    GrSurfaceOrigin origin,
    const SkISize& canvas_size, const SkMatrix& total_matrix,
    const math::RectF& destination_rect) {
  // We define a transformation from GLES normalized device coordinates (e.g.
  // [-1.0, 1.0]) into Skia coordinates (e.g. [0, canvas_size.width()]).  This
  // lets us apply Skia's transform inside of Skia's coordinate space.
  glm::mat4 gl_norm_coords_to_skia_canvas_coords(
      canvas_size.width() * 0.5f, 0, 0, 0, 0, -canvas_size.height() * 0.5f, 0,
      0, 0, 0, 1, 0, canvas_size.width() * 0.5f, canvas_size.height() * 0.5f, 0,
      1);

  // Convert Skia's current transform from the 3x3 row-major Skia matrix to a
  // 4x4 column-major GLSL matrix.  This is in Skia's coordinate system.
  glm::mat4 skia_transform_matrix(
      total_matrix[0], total_matrix[3], 0, total_matrix[6], total_matrix[1],
      total_matrix[4], 0, total_matrix[7], 0, 0, 1, 0, total_matrix[2],
      total_matrix[5], 0, total_matrix[8]);

  // Finally construct a matrix to map from full screen coordinates into the
  // destination rectangle.  This is in Skia's coordinate system.
  glm::mat4 dest_rect_matrix(
      destination_rect.width() / canvas_size.width(), 0, 0, 0, 0,
      destination_rect.height() / canvas_size.height(), 0, 0, 0, 0, 1, 0,
      destination_rect.x(), destination_rect.y(), 0, 1);

  // Since these matrices are applied in LIFO order, read the followin inlined
  // comments in reverse order.
  glm::mat4 result =
      // Flip the y axis depending on the destination surface's origin.
      ModelViewMatrixSurfaceOriginAdjustment(origin) *
      // Finally transform back into normalized device coordinates so that
      // GL can digest the results.
      glm::affineInverse(gl_norm_coords_to_skia_canvas_coords) *
      // Apply Skia's transformation matrix to the resulting coordinates.
      skia_transform_matrix *
      // Apply a matrix to transform from a quad that maps to the entire screen
      // into a quad that maps to the destination rectangle.
      dest_rect_matrix *
      // First transform from normalized device coordinates which the VBO
      // referenced by the RenderQuad() function will have its positions defined
      // within (e.g. [-1, 1]).
      gl_norm_coords_to_skia_canvas_coords;

  return result;
}

// Accommodate for the fact that for some image formats, like UYVY, our texture
// pixel width is actually half the size specified because there are two Y
// values in each pixel.
math::Rect AdjustContentRegionForImageType(
    const base::optional<AlternateRgbaFormat>& alternate_rgba_format,
    const math::Rect& content_region) {
  if (!alternate_rgba_format) {
    return content_region;
  }

  switch (*alternate_rgba_format) {
    case AlternateRgbaFormat_UYVY: {
      math::Rect adjusted_content_region = content_region;
      adjusted_content_region.set_width(content_region.width() / 2);
      return adjusted_content_region;
    } break;
    default: {
      NOTREACHED();
      return content_region;
    }
  }
}

// For stereoscopic video, the actual video is split (either horizontally or
// vertically) in two, one video for the left eye and one for the right eye.
// This function will adjust the content region rectangle to match only the
// left eye's video region, since we are ultimately presenting to a monoscopic
// display.
math::Rect AdjustContentRegionForStereoMode(render_tree::StereoMode stereo_mode,
                                            const math::Rect& content_region) {
  switch (stereo_mode) {
    case render_tree::kLeftRight: {
      // Use the left half (left eye) of the video only.
      math::Rect adjusted_content_region(content_region);
      adjusted_content_region.set_width(content_region.width() / 2);
      return adjusted_content_region;
    }

    case render_tree::kTopBottom: {
      // Use the top half (left eye) of the video only.
      math::Rect adjusted_content_region(content_region);
      adjusted_content_region.set_height(content_region.height() / 2);
      return adjusted_content_region;
    }

    case render_tree::kMono:
    case render_tree::kLeftRightUnadjustedTextureCoords:
      // No modifications needed here, pass content region through unchanged.
      return content_region;
  }

  NOTREACHED();
  return content_region;
}

egl::TexturedMeshRenderer::Image::Texture GetTextureFromHardwareFrontendImage(
    HardwareFrontendImage* image, render_tree::StereoMode stereo_mode) {
  egl::TexturedMeshRenderer::Image::Texture result;

  if (image->GetContentRegion()) {
    result.content_region = *image->GetContentRegion();
  } else {
    // If no content region is explicitly provided, we take this to mean that
    // the image was created from render_tree::ImageData in which case image
    // data is defined to be specified top-to-bottom, and so we must flip the
    // y-axis before passing it on to a GL renderer.
    math::Size image_size(image->GetSize());
    result.content_region = math::Rect(
        0, image_size.height(), image_size.width(), -image_size.height());
  }

  result.content_region = AdjustContentRegionForStereoMode(
      stereo_mode, AdjustContentRegionForImageType(
                       image->alternate_rgba_format(), result.content_region));

  result.texture = image->GetTextureEGL();

  return result;
}

egl::TexturedMeshRenderer::Image SkiaImageToTexturedMeshRendererImage(
    Image* image, render_tree::StereoMode stereo_mode) {
  egl::TexturedMeshRenderer::Image result;

  if (image->GetTypeId() == base::GetTypeId<SinglePlaneImage>()) {
    HardwareFrontendImage* hardware_image =
        base::polymorphic_downcast<HardwareFrontendImage*>(image);

    if (!hardware_image->alternate_rgba_format()) {
      result.type = egl::TexturedMeshRenderer::Image::RGBA;
    } else {
      switch (*hardware_image->alternate_rgba_format()) {
        case AlternateRgbaFormat_UYVY: {
          result.type = egl::TexturedMeshRenderer::Image::YUV_UYVY_422_BT709;
        } break;
        default: { NOTREACHED(); }
      }
    }

    result.textures[0] =
        GetTextureFromHardwareFrontendImage(hardware_image, stereo_mode);
  } else if (image->GetTypeId() == base::GetTypeId<MultiPlaneImage>()) {
    HardwareMultiPlaneImage* hardware_image =
        base::polymorphic_downcast<HardwareMultiPlaneImage*>(image);
    if (hardware_image->GetFormat() ==
        render_tree::kMultiPlaneImageFormatYUV2PlaneBT709) {
      result.type = egl::TexturedMeshRenderer::Image::YUV_2PLANE_BT709;
      result.textures[0] = GetTextureFromHardwareFrontendImage(
          hardware_image->GetHardwareFrontendImage(0), stereo_mode);
      result.textures[1] = GetTextureFromHardwareFrontendImage(
          hardware_image->GetHardwareFrontendImage(1), stereo_mode);
    } else if (hardware_image->GetFormat() ==
               render_tree::kMultiPlaneImageFormatYUV3PlaneBT709) {
      result.type = egl::TexturedMeshRenderer::Image::YUV_3PLANE_BT709;
      result.textures[0] = GetTextureFromHardwareFrontendImage(
          hardware_image->GetHardwareFrontendImage(0), stereo_mode);
      result.textures[1] = GetTextureFromHardwareFrontendImage(
          hardware_image->GetHardwareFrontendImage(1), stereo_mode);
      result.textures[2] = GetTextureFromHardwareFrontendImage(
          hardware_image->GetHardwareFrontendImage(2), stereo_mode);
    }
  } else {
    NOTREACHED();
  }

  return result;
}

enum FaceOrientation {
  FaceOrientation_Ccw,
  FaceOrientation_Cw,
};

void SetupGLStateForImageRender(Image* image,
                                FaceOrientation face_orientation) {
  if (image->IsOpaque()) {
    GL_CALL(glDisable(GL_BLEND));
  } else {
    GL_CALL(glEnable(GL_BLEND));
  }
  GL_CALL(glDisable(GL_DEPTH_TEST));
  GL_CALL(glDisable(GL_STENCIL_TEST));
  GL_CALL(glEnable(GL_SCISSOR_TEST));
  GL_CALL(glCullFace(GL_BACK));
  GL_CALL(glFrontFace(GL_CCW));
  if (face_orientation == FaceOrientation_Ccw) {
    GL_CALL(glEnable(GL_CULL_FACE));
  } else {
    // Unfortunately, some GLES implementations (like software Mesa) have a
    // problem with flipping glCullFrace() from GL_BACK to GL_FRONT, they seem
    // to ignore it.  We need to render back faces though if the face
    // orientation is flipped, so the only compatible solution is to disable
    // back-face culling.
    GL_CALL(glDisable(GL_CULL_FACE));
  }
}

void SetupGLTextureParameters(const egl::TexturedMeshRenderer::Image& image,
                              uint32 texture_wrap_s, uint32 texture_wrap_t) {
  for (int i = 0; i < image.num_textures(); ++i) {
    const backend::TextureEGL* texture = image.textures[i].texture;
    GL_CALL(glBindTexture(texture->GetTarget(), texture->gl_handle()));
    GL_CALL(glTexParameteri(texture->GetTarget(), GL_TEXTURE_MAG_FILTER,
                            GL_LINEAR));
    GL_CALL(glTexParameteri(texture->GetTarget(), GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR));
    GL_CALL(glTexParameteri(texture->GetTarget(), GL_TEXTURE_WRAP_S,
                            texture_wrap_s));
    GL_CALL(glTexParameteri(texture->GetTarget(), GL_TEXTURE_WRAP_T,
                            texture_wrap_t));
    GL_CALL(glBindTexture(texture->GetTarget(), 0));
  }
}

FaceOrientation GetFaceOrientationFromModelViewProjectionMatrix(
    const glm::mat4& model_view_projection_matrix) {
  return glm::determinant(model_view_projection_matrix) >= 0 ?
             FaceOrientation_Ccw :
             FaceOrientation_Cw;
}

}  // namespace

void HardwareRasterizer::Impl::RenderTextureEGL(
    const render_tree::ImageNode* image_node,
    RenderTreeNodeVisitorDrawState* draw_state) {
  Image* image =
      base::polymorphic_downcast<Image*>(image_node->data().source.get());
  if (!image) {
    return;
  }
  image->EnsureInitialized();

  // Flush the Skia draw state to ensure that all previously issued Skia calls
  // are rendered so that the following draw command will appear in the correct
  // order.
  draw_state->render_target->flush();

  SkISize canvas_size = draw_state->render_target->getBaseLayerSize();
  GL_CALL(glViewport(0, 0, canvas_size.width(), canvas_size.height()));

  SkIRect canvas_boundsi;
  draw_state->render_target->getClipDeviceBounds(&canvas_boundsi);
  // We need to translate from Skia's top-left corner origin to GL's bottom-left
  // corner origin.
  GL_CALL(glScissor(
      canvas_boundsi.x(),
      *current_surface_origin_ == kBottomLeft_GrSurfaceOrigin ?
          canvas_size.height() - canvas_boundsi.height() - canvas_boundsi.y() :
          canvas_boundsi.y(),
      canvas_boundsi.width(), canvas_boundsi.height()));

  glm::mat4 model_view_projection_matrix =
      GetFallbackTextureModelViewProjectionMatrix(
          *current_surface_origin_,
          canvas_size, draw_state->render_target->getTotalMatrix(),
          image_node->data().destination_rect);

  SetupGLStateForImageRender(
      image,
      GetFaceOrientationFromModelViewProjectionMatrix(
          model_view_projection_matrix));

  if (!textured_mesh_renderer_) {
    textured_mesh_renderer_.emplace(graphics_context_);
  }

  // Convert our image into a format digestable by TexturedMeshRenderer.
  egl::TexturedMeshRenderer::Image textured_mesh_renderer_image =
      SkiaImageToTexturedMeshRendererImage(image, render_tree::kMono);

  SetupGLTextureParameters(textured_mesh_renderer_image,
                           GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);

  // Invoke our TexturedMeshRenderer to actually perform the draw call.
  textured_mesh_renderer_->RenderQuad(
      textured_mesh_renderer_image, model_view_projection_matrix);

  // Let Skia know that we've modified GL state.
  uint32_t untouched_states =
      kMSAAEnable_GrGLBackendState | kStencil_GrGLBackendState |
      kPixelStore_GrGLBackendState | kFixedFunction_GrGLBackendState |
      kPathRendering_GrGLBackendState;
  gr_context_->resetContext(~untouched_states & kAll_GrBackendState);
}

void HardwareRasterizer::Impl::RenderTextureWithMeshFilterEGL(
    const render_tree::ImageNode* image_node,
    const render_tree::MapToMeshFilter& mesh_filter,
    RenderTreeNodeVisitorDrawState* draw_state) {
  Image* image =
      base::polymorphic_downcast<Image*>(image_node->data().source.get());
  if (!image) {
    return;
  }
  image->EnsureInitialized();

  SkISize canvas_size = draw_state->render_target->getBaseLayerSize();

  // Flush the Skia draw state to ensure that all previously issued Skia calls
  // are rendered so that the following draw command will appear in the correct
  // order.
  draw_state->render_target->flush();

  // We setup our viewport to fill the entire canvas.
  GL_CALL(glViewport(0, 0, canvas_size.width(), canvas_size.height()));
  GL_CALL(glScissor(0, 0, canvas_size.width(), canvas_size.height()));

  glm::mat4 model_view_projection_matrix =
      ModelViewMatrixSurfaceOriginAdjustment(*current_surface_origin_) *
      draw_state->transform_3d;

  SetupGLStateForImageRender(
      image,
      GetFaceOrientationFromModelViewProjectionMatrix(
          model_view_projection_matrix));

  if (!textured_mesh_renderer_) {
    textured_mesh_renderer_.emplace(graphics_context_);
  }

  const VertexBufferObject* mono_vbo =
      base::polymorphic_downcast<HardwareMesh*>(
          mesh_filter.mono_mesh(image->GetSize()).get())
          ->GetVBO();

  // Convert our image into a format digestable by TexturedMeshRenderer.
  egl::TexturedMeshRenderer::Image textured_mesh_renderer_image =
      SkiaImageToTexturedMeshRendererImage(image, mesh_filter.stereo_mode());

  SetupGLTextureParameters(textured_mesh_renderer_image,
                           GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);

  // Invoke out TexturedMeshRenderer to actually perform the draw call.
  textured_mesh_renderer_->RenderVBO(
      mono_vbo->GetHandle(), mono_vbo->GetVertexCount(),
      mono_vbo->GetDrawMode(),
      textured_mesh_renderer_image,
      model_view_projection_matrix);

  // Let Skia know that we've modified GL state.
  gr_context_->resetContext();
}

HardwareRasterizer::Impl::Impl(backend::GraphicsContext* graphics_context,
                               int skia_atlas_width, int skia_atlas_height,
                               int skia_cache_size_in_bytes,
                               int scratch_surface_cache_size_in_bytes,
                               int surface_cache_size_in_bytes,
                               bool purge_skia_font_caches_on_destruction)
    : graphics_context_(
          base::polymorphic_downcast<backend::GraphicsContextEGL*>(
              graphics_context)) {
  TRACE_EVENT0("cobalt::renderer", "HardwareRasterizer::Impl::Impl()");

  DLOG(INFO) << "skia_cache_size_in_bytes: " << skia_cache_size_in_bytes;
  DLOG(INFO) << "scratch_surface_cache_size_in_bytes: "
             << scratch_surface_cache_size_in_bytes;
  DLOG(INFO) << "surface_cache_size_in_bytes: " << surface_cache_size_in_bytes;

  graphics_context_->MakeCurrent();
  // Create a GrContext object that wraps the passed in Cobalt GraphicsContext
  // object.
  gr_context_.reset(GrContext::Create(
      kCobalt_GrBackend, reinterpret_cast<GrBackendContext>(graphics_context_),
      skia_atlas_width, skia_atlas_height));

  DCHECK(gr_context_);
  // The GrContext manages a resource cache internally using GrResourceCache
  // which by default caches 96MB of resources.  This is used for helping with
  // rendering shadow effects, gradient effects, and software rendered paths.
  // As we have our own cache for most resources, set it to a much smaller value
  // so Skia doesn't use too much GPU memory.
  const int kSkiaCacheMaxResources = 128;
  gr_context_->setResourceCacheLimits(kSkiaCacheMaxResources,
                                      skia_cache_size_in_bytes);

  base::Callback<SkSurface*(const math::Size&)> create_sk_surface_function =
      base::Bind(&HardwareRasterizer::Impl::CreateSkSurface,
                 base::Unretained(this));

  scratch_surface_cache_.emplace(create_sk_surface_function,
                                 scratch_surface_cache_size_in_bytes);

  // Setup a resource provider for resources to be used with a hardware
  // accelerated Skia rasterizer.
  resource_provider_.reset(new HardwareResourceProvider(
      graphics_context_, gr_context_,
      base::Bind(&HardwareRasterizer::Impl::SubmitOffscreenToRenderTarget,
                 base::Unretained(this)),
      purge_skia_font_caches_on_destruction));

  graphics_context_->ReleaseCurrentContext();

  int max_surface_size = std::max(gr_context_->getMaxRenderTargetSize(),
                                  gr_context_->getMaxTextureSize());
  DLOG(INFO) << "Max renderer surface size: " << max_surface_size;

  if (surface_cache_size_in_bytes > 0) {
    surface_cache_delegate_.emplace(
        create_sk_surface_function,
        math::Size(max_surface_size, max_surface_size));

    surface_cache_.emplace(&surface_cache_delegate_.value(),
                           surface_cache_size_in_bytes);
  }
}

HardwareRasterizer::Impl::~Impl() {
  graphics_context_->MakeCurrent();
  textured_mesh_renderer_ = base::nullopt;

  for (SkSurfaceMap::iterator iter = sk_output_surface_map_.begin();
    iter != sk_output_surface_map_.end(); iter++) {
    iter->second->unref();
  }
  surface_cache_ = base::nullopt;
  surface_cache_delegate_ = base::nullopt;
  scratch_surface_cache_ = base::nullopt;
  gr_context_.reset(NULL);
  graphics_context_->ReleaseCurrentContext();
}

void HardwareRasterizer::Impl::AdvanceFrame() {
  // Update our surface cache to do per-frame calculations such as deciding
  // which render tree nodes are candidates for caching in this upcoming
  // frame.
  if (surface_cache_) {
    surface_cache_->Frame();
  }
}

void HardwareRasterizer::Impl::Submit(
    const scoped_refptr<render_tree::Node>& render_tree,
    const scoped_refptr<backend::RenderTarget>& render_target,
    const Options& options) {
  DCHECK(thread_checker_.CalledOnValidThread());

  scoped_refptr<backend::RenderTargetEGL> render_target_egl(
      base::polymorphic_downcast<backend::RenderTargetEGL*>(
          render_target.get()));

  // Skip rendering if we lost the surface. This can happen just before suspend
  // on Android, so now we're just waiting for the suspend to clean up.
  if (render_target_egl->is_surface_bad()) {
    return;
  }

  backend::GraphicsContextEGL::ScopedMakeCurrent scoped_make_current(
      graphics_context_, render_target_egl);

  // First reset the graphics context state for the pending render tree
  // draw calls, in case we have modified state in between.
  gr_context_->resetContext();

  AdvanceFrame();

  // Get a SkCanvas that outputs to our hardware render target.
  SkCanvas* canvas = GetCanvasFromRenderTarget(render_target);

  canvas->save();

  if (options.flags & Rasterizer::kSubmitFlags_Clear) {
    canvas->clear(SkColorSetARGB(0, 0, 0, 0));
  } else if (options.dirty) {
    // Only a portion of the display is dirty. Reuse the previous frame
    // if possible.
    if (render_target_egl->ContentWasPreservedAfterSwap()) {
      canvas->clipRect(CobaltRectFToSkiaRect(*options.dirty));
    }
  }

  // Rasterize the passed in render tree to our hardware render target.
  RasterizeRenderTreeToCanvas(render_tree, canvas, kBottomLeft_GrSurfaceOrigin);

  {
    TRACE_EVENT0("cobalt::renderer", "Skia Flush");
    canvas->flush();
  }

  graphics_context_->SwapBuffers(render_target_egl);
  canvas->restore();
}

void HardwareRasterizer::Impl::SubmitOffscreen(
    const scoped_refptr<render_tree::Node>& render_tree, SkCanvas* canvas) {
  DCHECK(thread_checker_.CalledOnValidThread());
  RasterizeRenderTreeToCanvas(render_tree, canvas, kBottomLeft_GrSurfaceOrigin);
}

void HardwareRasterizer::Impl::SubmitOffscreenToRenderTarget(
    const scoped_refptr<render_tree::Node>& render_tree,
    const scoped_refptr<backend::RenderTarget>& render_target) {
  DCHECK(thread_checker_.CalledOnValidThread());

  scoped_refptr<backend::RenderTargetEGL> render_target_egl(
      base::polymorphic_downcast<backend::RenderTargetEGL*>(
          render_target.get()));

  if (render_target_egl->is_surface_bad()) {
    return;
  }

  backend::GraphicsContextEGL::ScopedMakeCurrent scoped_make_current(
      graphics_context_, render_target_egl);

  // Create a canvas from the render target.
  GrBackendRenderTargetDesc skia_desc =
      CobaltRenderTargetToSkiaBackendRenderTargetDesc(render_target.get());
  skia_desc.fOrigin = kTopLeft_GrSurfaceOrigin;
  SkAutoTUnref<GrRenderTarget> skia_render_target(
      gr_context_->wrapBackendRenderTarget(skia_desc));
  SkSurface* sk_output_surface =
      CreateSkiaRenderTargetSurface(skia_render_target);
  SkCanvas* canvas = sk_output_surface->getCanvas();

  canvas->clear(SkColorSetARGB(0, 0, 0, 0));

  // Render to the canvas and clean up.
  RasterizeRenderTreeToCanvas(render_tree, canvas, kTopLeft_GrSurfaceOrigin);
  canvas->flush();
  sk_output_surface->unref();
}

render_tree::ResourceProvider* HardwareRasterizer::Impl::GetResourceProvider() {
  return resource_provider_.get();
}

GrContext* HardwareRasterizer::Impl::GetGrContext() {
  return gr_context_.get();
}

void HardwareRasterizer::Impl::MakeCurrent() {
  graphics_context_->MakeCurrent();
}

SkSurface* HardwareRasterizer::Impl::CreateSkSurface(const math::Size& size) {
  TRACE_EVENT2("cobalt::renderer", "HardwareRasterizer::CreateSkSurface()",
               "width", size.width(), "height", size.height());

  // Create a texture of the specified size.  Then convert it to a render
  // target and then convert that to a SkSurface which we return.
  GrTextureDesc target_surface_desc;
  target_surface_desc.fFlags =
      kRenderTarget_GrTextureFlagBit | kNoStencil_GrTextureFlagBit;
  target_surface_desc.fOrigin = kBottomLeft_GrSurfaceOrigin;
  target_surface_desc.fWidth = size.width();
  target_surface_desc.fHeight = size.height();
  target_surface_desc.fConfig = kRGBA_8888_GrPixelConfig;
  target_surface_desc.fSampleCnt = 0;

  SkAutoTUnref<GrTexture> skia_texture(
      gr_context_->createUncachedTexture(target_surface_desc, NULL, 0));
  if (!skia_texture) {
    // If we failed at creating a texture, try again using a different texture
    // format.
    target_surface_desc.fConfig = kBGRA_8888_GrPixelConfig;
    skia_texture.reset(
        gr_context_->createUncachedTexture(target_surface_desc, NULL, 0));
  }
  if (!skia_texture) {
    return NULL;
  }

  GrRenderTarget* skia_render_target = skia_texture->asRenderTarget();
  DCHECK(skia_render_target);

  SkSurfaceProps surface_props = GetRenderTargetSurfaceProps();
  return SkSurface::NewRenderTargetDirect(skia_render_target, &surface_props);
}

scoped_ptr<RenderTreeNodeVisitor::ScratchSurface>
HardwareRasterizer::Impl::CreateScratchSurface(const math::Size& size) {
  TRACE_EVENT2("cobalt::renderer", "HardwareRasterizer::CreateScratchImage()",
               "width", size.width(), "height", size.height());

  scoped_ptr<CachedScratchSurfaceHolder> scratch_surface(
      new CachedScratchSurfaceHolder(&scratch_surface_cache_.value(), size));
  if (scratch_surface->GetSurface()) {
    return scratch_surface.PassAs<RenderTreeNodeVisitor::ScratchSurface>();
  } else {
    return scoped_ptr<RenderTreeNodeVisitor::ScratchSurface>();
  }
}

SkCanvas* HardwareRasterizer::Impl::GetCanvasFromRenderTarget(
    const scoped_refptr<backend::RenderTarget>& render_target) {
  SkSurface* sk_output_surface;
  int32_t surface_map_key = render_target->GetSerialNumber();
  SkSurfaceMap::iterator iter = sk_output_surface_map_.find(surface_map_key);
  if (iter == sk_output_surface_map_.end()) {
    // Remove the least recently used SkSurface from the map if we exceed the
    // max allowed saved surfaces.
    if (sk_output_surface_map_.size() > kMaxSkSurfaceCount) {
      SkSurfaceMap::iterator iter = sk_output_surface_map_.begin();
      DLOG(WARNING)
          << "Erasing the SkSurface for RenderTarget " << iter->first
          << ". This may happen nominally during movement-triggered "
          << "replacement of SkSurfaces or else it may indicate the surface "
          << "map is thrashing because the total number of RenderTargets ("
          << kMaxSkSurfaceCount << ") has been exceeded.";
      iter->second->unref();
      sk_output_surface_map_.erase(iter);
    }
    // Setup a Skia render target that wraps the passed in Cobalt render target.
    SkAutoTUnref<GrRenderTarget> skia_render_target(
        gr_context_->wrapBackendRenderTarget(
            CobaltRenderTargetToSkiaBackendRenderTargetDesc(
                render_target.get())));

    // Create an SkSurface from the render target so that we can acquire a
    // SkCanvas object from it in Submit().
    sk_output_surface = CreateSkiaRenderTargetSurface(skia_render_target);
    sk_output_surface_map_[surface_map_key] = sk_output_surface;
  } else {
    sk_output_surface = sk_output_surface_map_[surface_map_key];
    // Mark this RenderTarget/SkCanvas pair as the most recently used by
    // popping it and re-adding it.
    sk_output_surface_map_.erase(iter);
    sk_output_surface_map_[surface_map_key] = sk_output_surface;
  }
  return sk_output_surface->getCanvas();
}

void HardwareRasterizer::Impl::RasterizeRenderTreeToCanvas(
    const scoped_refptr<render_tree::Node>& render_tree, SkCanvas* canvas,
    GrSurfaceOrigin origin) {
  TRACE_EVENT0("cobalt::renderer", "RasterizeRenderTreeToCanvas");
  // TODO: This trace uses the name in the current benchmark to keep it work as
  // expected. Remove after switching to webdriver benchmark.
  TRACE_EVENT0("cobalt::renderer", "VisitRenderTree");

  base::optional<GrSurfaceOrigin> old_origin = current_surface_origin_;
  current_surface_origin_.emplace(origin);

  RenderTreeNodeVisitor::CreateScratchSurfaceFunction
      create_scratch_surface_function =
          base::Bind(&HardwareRasterizer::Impl::CreateScratchSurface,
                     base::Unretained(this));
  RenderTreeNodeVisitor visitor(
      canvas, &create_scratch_surface_function,
      base::Bind(&HardwareRasterizer::Impl::ResetSkiaState,
                 base::Unretained(this)),
      base::Bind(&HardwareRasterizer::Impl::RenderTextureEGL,
                 base::Unretained(this)),
      base::Bind(&HardwareRasterizer::Impl::RenderTextureWithMeshFilterEGL,
                 base::Unretained(this)),
      surface_cache_delegate_ ? &surface_cache_delegate_.value() : NULL,
      surface_cache_ ? &surface_cache_.value() : NULL);
  DCHECK(render_tree);
  render_tree->Accept(&visitor);

  current_surface_origin_ = old_origin;
}

void HardwareRasterizer::Impl::ResetSkiaState() { gr_context_->resetContext(); }

HardwareRasterizer::HardwareRasterizer(
    backend::GraphicsContext* graphics_context, int skia_atlas_width,
    int skia_atlas_height, int skia_cache_size_in_bytes,
    int scratch_surface_cache_size_in_bytes, int surface_cache_size_in_bytes,
    bool purge_skia_font_caches_on_destruction)
    : impl_(new Impl(
          graphics_context, skia_atlas_width, skia_atlas_height,
          skia_cache_size_in_bytes, scratch_surface_cache_size_in_bytes,
          surface_cache_size_in_bytes, purge_skia_font_caches_on_destruction)) {
}

HardwareRasterizer::~HardwareRasterizer() {}

void HardwareRasterizer::Submit(
    const scoped_refptr<render_tree::Node>& render_tree,
    const scoped_refptr<backend::RenderTarget>& render_target,
    const Options& options) {
  TRACE_EVENT0("cobalt::renderer", "Rasterizer::Submit()");
  TRACE_EVENT0("cobalt::renderer", "HardwareRasterizer::Submit()");
  impl_->Submit(render_tree, render_target, options);
}

void HardwareRasterizer::SubmitOffscreen(
    const scoped_refptr<render_tree::Node>& render_tree, SkCanvas* canvas) {
  TRACE_EVENT0("cobalt::renderer", "HardwareRasterizer::SubmitOffscreen()");
  impl_->SubmitOffscreen(render_tree, canvas);
}

SkCanvas* HardwareRasterizer::GetCachedCanvas(
    const scoped_refptr<backend::RenderTarget>& render_target) {
  return impl_->GetCanvasFromRenderTarget(render_target);
}

void HardwareRasterizer::AdvanceFrame() {
  impl_->AdvanceFrame();
}

render_tree::ResourceProvider* HardwareRasterizer::GetResourceProvider() {
  return impl_->GetResourceProvider();
}

GrContext* HardwareRasterizer::GetGrContext() {
  return impl_->GetGrContext();
}

void HardwareRasterizer::MakeCurrent() { return impl_->MakeCurrent(); }

}  // namespace skia
}  // namespace rasterizer
}  // namespace renderer
}  // namespace cobalt
