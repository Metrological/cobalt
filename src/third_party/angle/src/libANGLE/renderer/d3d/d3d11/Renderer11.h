//
// Copyright (c) 2012-2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Renderer11.h: Defines a back-end specific class for the D3D11 renderer.

#ifndef LIBANGLE_RENDERER_D3D_D3D11_RENDERER11_H_
#define LIBANGLE_RENDERER_D3D_D3D11_RENDERER11_H_

#include "common/angleutils.h"
#include "common/mathutil.h"
#include "libANGLE/AttributeMap.h"
#include "libANGLE/angletypes.h"
#include "libANGLE/renderer/d3d/HLSLCompiler.h"
#include "libANGLE/renderer/d3d/ProgramD3D.h"
#include "libANGLE/renderer/d3d/RenderTargetD3D.h"
#include "libANGLE/renderer/d3d/RendererD3D.h"
#include "libANGLE/renderer/d3d/d3d11/DebugAnnotator11.h"
#include "libANGLE/renderer/d3d/d3d11/InputLayoutCache.h"
#include "libANGLE/renderer/d3d/d3d11/RenderStateCache.h"
#include "libANGLE/renderer/d3d/d3d11/ResourceManager11.h"
#include "libANGLE/renderer/d3d/d3d11/StateManager11.h"
#include "libANGLE/renderer/d3d/d3d11/renderer11_utils.h"

namespace gl
{
class FramebufferAttachment;
struct ImageIndex;
}

namespace rx
{
class Blit11;
class Buffer11;
class Clear11;
class Context11;
class IndexDataManager;
struct PackPixelsParams;
class PixelTransfer11;
class RenderTarget11;
class StreamingIndexBufferInterface;
class Trim11;
class VertexDataManager;

struct Renderer11DeviceCaps
{
    D3D_FEATURE_LEVEL featureLevel;
    bool supportsDXGI1_2;                // Support for DXGI 1.2
    bool supportsClearView;              // Support for ID3D11DeviceContext1::ClearView
    bool supportsConstantBufferOffsets;  // Support for Constant buffer offset
    UINT B5G6R5support;    // Bitfield of D3D11_FORMAT_SUPPORT values for DXGI_FORMAT_B5G6R5_UNORM
    UINT B4G4R4A4support;  // Bitfield of D3D11_FORMAT_SUPPORT values for DXGI_FORMAT_B4G4R4A4_UNORM
    UINT B5G5R5A1support;  // Bitfield of D3D11_FORMAT_SUPPORT values for DXGI_FORMAT_B5G5R5A1_UNORM
    Optional<LARGE_INTEGER> driverVersion;  // Four-part driver version number.
};

enum
{
    MAX_VERTEX_UNIFORM_VECTORS_D3D11   = 1024,
    MAX_FRAGMENT_UNIFORM_VECTORS_D3D11 = 1024
};

// Possible reasons RendererD3D initialize can fail
enum D3D11InitError
{
    // The renderer loaded successfully
    D3D11_INIT_SUCCESS = 0,
    // Failed to load the ANGLE & D3D compiler libraries
    D3D11_INIT_COMPILER_ERROR,
    // Failed to load a necessary DLL (non-compiler)
    D3D11_INIT_MISSING_DEP,
    // CreateDevice returned E_INVALIDARG
    D3D11_INIT_CREATEDEVICE_INVALIDARG,
    // CreateDevice failed with an error other than invalid arg
    D3D11_INIT_CREATEDEVICE_ERROR,
    // DXGI 1.2 required but not found
    D3D11_INIT_INCOMPATIBLE_DXGI,
    // Other initialization error
    D3D11_INIT_OTHER_ERROR,
    // CreateDevice returned E_FAIL
    D3D11_INIT_CREATEDEVICE_FAIL,
    // CreateDevice returned E_NOTIMPL
    D3D11_INIT_CREATEDEVICE_NOTIMPL,
    // CreateDevice returned E_OUTOFMEMORY
    D3D11_INIT_CREATEDEVICE_OUTOFMEMORY,
    // CreateDevice returned DXGI_ERROR_INVALID_CALL
    D3D11_INIT_CREATEDEVICE_INVALIDCALL,
    // CreateDevice returned DXGI_ERROR_SDK_COMPONENT_MISSING
    D3D11_INIT_CREATEDEVICE_COMPONENTMISSING,
    // CreateDevice returned DXGI_ERROR_WAS_STILL_DRAWING
    D3D11_INIT_CREATEDEVICE_WASSTILLDRAWING,
    // CreateDevice returned DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
    D3D11_INIT_CREATEDEVICE_NOTAVAILABLE,
    // CreateDevice returned DXGI_ERROR_DEVICE_HUNG
    D3D11_INIT_CREATEDEVICE_DEVICEHUNG,
    // CreateDevice returned NULL
    D3D11_INIT_CREATEDEVICE_NULL,
    NUM_D3D11_INIT_ERRORS
};

class Renderer11 : public RendererD3D
{
  public:
    explicit Renderer11(egl::Display *display);
    virtual ~Renderer11();

    egl::Error initialize() override;
    bool resetDevice() override;

    egl::ConfigSet generateConfigs() override;
    void generateDisplayExtensions(egl::DisplayExtensions *outExtensions) const override;

    ContextImpl *createContext(const gl::ContextState &state) override;

    gl::Error flush();
    gl::Error finish();

    bool isValidNativeWindow(EGLNativeWindowType window) const override;
    NativeWindowD3D *createNativeWindow(EGLNativeWindowType window,
                                        const egl::Config *config,
                                        const egl::AttributeMap &attribs) const override;

    SwapChainD3D *createSwapChain(NativeWindowD3D *nativeWindow,
                                  HANDLE shareHandle,
                                  IUnknown *d3dTexture,
                                  GLenum backBufferFormat,
                                  GLenum depthBufferFormat,
                                  EGLint orientation,
                                  EGLint samples) override;
    egl::Error getD3DTextureInfo(const egl::Config *configuration,
                                 IUnknown *d3dTexture,
                                 EGLint *width,
                                 EGLint *height,
                                 GLenum *fboFormat) const override;
    egl::Error validateShareHandle(const egl::Config *config,
                                   HANDLE shareHandle,
                                   const egl::AttributeMap &attribs) const override;

    gl::Error setSamplerState(gl::SamplerType type,
                              int index,
                              gl::Texture *texture,
                              const gl::SamplerState &sampler) override;
    gl::Error setTexture(gl::SamplerType type, int index, gl::Texture *texture) override;

    gl::Error setUniformBuffers(const gl::ContextState &data,
                                const std::vector<GLint> &vertexUniformBuffers,
                                const std::vector<GLint> &fragmentUniformBuffers) override;

    gl::Error updateState(ContextImpl *contextImpl, GLenum drawMode);

    bool applyPrimitiveType(GLenum mode, GLsizei count, bool usesPointSize);
    gl::Error applyUniforms(const ProgramD3D &programD3D,
                            GLenum drawMode,
                            const std::vector<D3DUniform *> &uniformArray) override;
    gl::Error applyVertexBuffer(const gl::State &state,
                                GLenum mode,
                                GLint first,
                                GLsizei count,
                                GLsizei instances,
                                TranslatedIndexData *indexInfo);
    gl::Error applyIndexBuffer(const gl::ContextState &data,
                               const void *indices,
                               GLsizei count,
                               GLenum mode,
                               GLenum type,
                               TranslatedIndexData *indexInfo);
    gl::Error applyTransformFeedbackBuffers(const gl::ContextState &data);

    // lost device
    bool testDeviceLost() override;
    bool testDeviceResettable() override;

    std::string getRendererDescription() const;
    DeviceIdentifier getAdapterIdentifier() const override;

    unsigned int getReservedVertexUniformVectors() const;
    unsigned int getReservedFragmentUniformVectors() const;
    unsigned int getReservedVertexUniformBuffers() const override;
    unsigned int getReservedFragmentUniformBuffers() const override;

    bool getShareHandleSupport() const;

    bool getNV12TextureSupport() const;

    int getMajorShaderModel() const override;
    int getMinorShaderModel() const override;
    std::string getShaderModelSuffix() const override;

    // Pixel operations
    gl::Error copyImage2D(const gl::Framebuffer *framebuffer,
                          const gl::Rectangle &sourceRect,
                          GLenum destFormat,
                          const gl::Offset &destOffset,
                          TextureStorage *storage,
                          GLint level) override;
    gl::Error copyImageCube(const gl::Framebuffer *framebuffer,
                            const gl::Rectangle &sourceRect,
                            GLenum destFormat,
                            const gl::Offset &destOffset,
                            TextureStorage *storage,
                            GLenum target,
                            GLint level) override;
    gl::Error copyImage3D(const gl::Framebuffer *framebuffer,
                          const gl::Rectangle &sourceRect,
                          GLenum destFormat,
                          const gl::Offset &destOffset,
                          TextureStorage *storage,
                          GLint level) override;
    gl::Error copyImage2DArray(const gl::Framebuffer *framebuffer,
                               const gl::Rectangle &sourceRect,
                               GLenum destFormat,
                               const gl::Offset &destOffset,
                               TextureStorage *storage,
                               GLint level) override;

    gl::Error copyTexture(const gl::Texture *source,
                          GLint sourceLevel,
                          const gl::Rectangle &sourceRect,
                          GLenum destFormat,
                          const gl::Offset &destOffset,
                          TextureStorage *storage,
                          GLenum destTarget,
                          GLint destLevel,
                          bool unpackFlipY,
                          bool unpackPremultiplyAlpha,
                          bool unpackUnmultiplyAlpha) override;
    gl::Error copyCompressedTexture(const gl::Texture *source,
                                    GLint sourceLevel,
                                    TextureStorage *storage,
                                    GLint destLevel) override;

    // RenderTarget creation
    gl::Error createRenderTarget(int width,
                                 int height,
                                 GLenum format,
                                 GLsizei samples,
                                 RenderTargetD3D **outRT) override;
    gl::Error createRenderTargetCopy(RenderTargetD3D *source, RenderTargetD3D **outRT) override;

    // Shader operations
    gl::Error loadExecutable(const void *function,
                             size_t length,
                             ShaderType type,
                             const std::vector<D3DVarying> &streamOutVaryings,
                             bool separatedOutputBuffers,
                             ShaderExecutableD3D **outExecutable) override;
    gl::Error compileToExecutable(gl::InfoLog &infoLog,
                                  const std::string &shaderHLSL,
                                  ShaderType type,
                                  const std::vector<D3DVarying> &streamOutVaryings,
                                  bool separatedOutputBuffers,
                                  const angle::CompilerWorkaroundsD3D &workarounds,
                                  ShaderExecutableD3D **outExectuable) override;
    gl::Error ensureHLSLCompilerInitialized() override;

    UniformStorageD3D *createUniformStorage(size_t storageSize) override;

    // Image operations
    ImageD3D *createImage() override;
    gl::Error generateMipmap(ImageD3D *dest, ImageD3D *source) override;
    gl::Error generateMipmapUsingD3D(TextureStorage *storage,
                                     const gl::TextureState &textureState) override;
    TextureStorage *createTextureStorage2D(SwapChainD3D *swapChain) override;
    TextureStorage *createTextureStorage2D(IUnknown *texture,
                                           bool bindChroma,
                                           IUnknown *dxgiBuffer) override;
    TextureStorage *createTextureStorageEGLImage(EGLImageD3D *eglImage,
                                                 RenderTargetD3D *renderTargetD3D) override;
    TextureStorage *createTextureStorageExternal(
        egl::Stream *stream,
        const egl::Stream::GLTextureDescription &desc) override;
    TextureStorage *createTextureStorage2D(GLenum internalformat,
                                           bool renderTarget,
                                           GLsizei width,
                                           GLsizei height,
                                           int levels,
                                           bool hintLevelZeroOnly) override;
    TextureStorage *createTextureStorageCube(GLenum internalformat,
                                             bool renderTarget,
                                             int size,
                                             int levels,
                                             bool hintLevelZeroOnly) override;
    TextureStorage *createTextureStorage3D(GLenum internalformat,
                                           bool renderTarget,
                                           GLsizei width,
                                           GLsizei height,
                                           GLsizei depth,
                                           int levels) override;
    TextureStorage *createTextureStorage2DArray(GLenum internalformat,
                                                bool renderTarget,
                                                GLsizei width,
                                                GLsizei height,
                                                GLsizei depth,
                                                int levels) override;

    VertexBuffer *createVertexBuffer() override;
    IndexBuffer *createIndexBuffer() override;

    // Stream Creation
    StreamProducerImpl *createStreamProducerD3DTextureNV12(
        egl::Stream::ConsumerType consumerType,
        const egl::AttributeMap &attribs) override;

    // D3D11-renderer specific methods
    ID3D11Device *getDevice() { return mDevice; }
    void *getD3DDevice() override;
    ID3D11DeviceContext *getDeviceContext() { return mDeviceContext; };
    ID3D11DeviceContext1 *getDeviceContext1IfSupported() { return mDeviceContext1; };
    IDXGIFactory *getDxgiFactory() { return mDxgiFactory; };

    RenderStateCache &getStateCache() { return mStateCache; }

    Blit11 *getBlitter() { return mBlit; }
    Clear11 *getClearer() { return mClear; }
    gl::DebugAnnotator *getAnnotator();

    // Buffer-to-texture and Texture-to-buffer copies
    bool supportsFastCopyBufferToTexture(GLenum internalFormat) const override;
    gl::Error fastCopyBufferToTexture(const gl::PixelUnpackState &unpack,
                                      unsigned int offset,
                                      RenderTargetD3D *destRenderTarget,
                                      GLenum destinationFormat,
                                      GLenum sourcePixelsType,
                                      const gl::Box &destArea) override;

    void markAllStateDirty();
    gl::Error packPixels(const TextureHelper11 &textureHelper,
                         const PackPixelsParams &params,
                         uint8_t *pixelsOut);

    bool getLUID(LUID *adapterLuid) const override;
    VertexConversionType getVertexConversionType(
        gl::VertexFormatType vertexFormatType) const override;
    GLenum getVertexComponentType(gl::VertexFormatType vertexFormatType) const override;

    // Warning: you should ensure binding really matches attrib.bindingIndex before using this
    // function.
    gl::ErrorOrResult<unsigned int> getVertexSpaceRequired(const gl::VertexAttribute &attrib,
                                                           const gl::VertexBinding &binding,
                                                           GLsizei count,
                                                           GLsizei instances) const override;

    gl::Error readFromAttachment(const gl::FramebufferAttachment &srcAttachment,
                                 const gl::Rectangle &sourceArea,
                                 GLenum format,
                                 GLenum type,
                                 GLuint outputPitch,
                                 const gl::PixelPackState &pack,
                                 uint8_t *pixels);

    gl::Error blitRenderbufferRect(const gl::Rectangle &readRect,
                                   const gl::Rectangle &drawRect,
                                   RenderTargetD3D *readRenderTarget,
                                   RenderTargetD3D *drawRenderTarget,
                                   GLenum filter,
                                   const gl::Rectangle *scissor,
                                   bool colorBlit,
                                   bool depthBlit,
                                   bool stencilBlit);

    bool isES3Capable() const;
    const Renderer11DeviceCaps &getRenderer11DeviceCaps() const { return mRenderer11DeviceCaps; };

    RendererClass getRendererClass() const override { return RENDERER_D3D11; }
    InputLayoutCache *getInputLayoutCache() { return &mInputLayoutCache; }
    StateManager11 *getStateManager() { return &mStateManager; }

    void onSwap();
    void onBufferCreate(const Buffer11 *created);
    void onBufferDelete(const Buffer11 *deleted);

    egl::Error getEGLDevice(DeviceImpl **device) override;

    gl::Error genericDrawArrays(Context11 *context,
                                GLenum mode,
                                GLint first,
                                GLsizei count,
                                GLsizei instances);

    gl::Error genericDrawElements(Context11 *context,
                                  GLenum mode,
                                  GLsizei count,
                                  GLenum type,
                                  const void *indices,
                                  GLsizei instances,
                                  const gl::IndexRange &indexRange);

    gl::Error genericDrawIndirect(Context11 *context,
                                  GLenum mode,
                                  GLenum type,
                                  const void *indirect);

    // Necessary hack for default framebuffers in D3D.
    FramebufferImpl *createDefaultFramebuffer(const gl::FramebufferState &state) override;

    gl::Error getScratchMemoryBuffer(size_t requestedSize, angle::MemoryBuffer **bufferOut);

    gl::Version getMaxSupportedESVersion() const override;

    gl::Error dispatchCompute(Context11 *context,
                              GLuint numGroupsX,
                              GLuint numGroupsY,
                              GLuint numGroupsZ);
    gl::Error applyComputeUniforms(const ProgramD3D &programD3D,
                                   const std::vector<D3DUniform *> &uniformArray) override;
    gl::Error applyComputeShader(const gl::ContextState &data);

    template <typename DescT, typename InitDataT, typename ResourceT>
    gl::Error allocateResource(const DescT &desc, InitDataT *initData, ResourceT *resourceOut)
    {
        return mResourceManager11.allocate(this, &desc, initData, resourceOut);
    }

    template <typename InitDataT, typename ResourceT>
    gl::Error allocateResourceNoDesc(InitDataT *initData, ResourceT *resourceOut)
    {
        return mResourceManager11.allocate(this, nullptr, initData, resourceOut);
    }

  protected:
    gl::Error clearTextures(gl::SamplerType samplerType,
                            size_t rangeStart,
                            size_t rangeEnd) override;

  private:
    gl::Error drawArraysImpl(const gl::ContextState &data,
                             GLenum mode,
                             GLint startVertex,
                             GLsizei count,
                             GLsizei instances);
    gl::Error drawElementsImpl(const gl::ContextState &data,
                               const TranslatedIndexData &indexInfo,
                               GLenum mode,
                               GLsizei count,
                               GLenum type,
                               const void *indices,
                               GLsizei instances);
    gl::Error drawArraysIndirectImpl(const gl::ContextState &data,
                                     GLenum mode,
                                     const void *indirect);
    gl::Error drawElementsIndirectImpl(const gl::ContextState &data,
                                       GLenum mode,
                                       GLenum type,
                                       const void *indirect);

    // Support directly using indirect draw buffer.
    bool supportsFastIndirectDraw(const gl::State &state, GLenum mode, GLenum type);

    void generateCaps(gl::Caps *outCaps,
                      gl::TextureCapsMap *outTextureCaps,
                      gl::Extensions *outExtensions,
                      gl::Limitations *outLimitations) const override;

    angle::WorkaroundsD3D generateWorkarounds() const override;

    gl::Error drawLineLoop(const gl::ContextState &data,
                           GLsizei count,
                           GLenum type,
                           const void *indices,
                           int baseVertex,
                           int instances);
    gl::Error drawTriangleFan(const gl::ContextState &data,
                              GLsizei count,
                              GLenum type,
                              const void *indices,
                              int baseVertex,
                              int instances);

    gl::Error applyShaders(const gl::ContextState &data, GLenum drawMode);
    gl::Error generateSwizzle(gl::Texture *texture);
    gl::Error generateSwizzles(const gl::ContextState &data, gl::SamplerType type);
    gl::Error generateSwizzles(const gl::ContextState &data);

    gl::ErrorOrResult<TextureHelper11> resolveMultisampledTexture(RenderTarget11 *renderTarget,
                                                                  bool depth,
                                                                  bool stencil);

    void populateRenderer11DeviceCaps();

    void updateHistograms();

    class SamplerMetadataD3D11 final : angle::NonCopyable
    {
      public:
        SamplerMetadataD3D11();
        ~SamplerMetadataD3D11();

        struct dx_SamplerMetadata
        {
            int baseLevel;
            int internalFormatBits;
            int wrapModes;
            int padding;  // This just pads the struct to 16 bytes
        };
        static_assert(sizeof(dx_SamplerMetadata) == 16u,
                      "Sampler metadata struct must be one 4-vec / 16 bytes.");

        void initData(unsigned int samplerCount);
        void update(unsigned int samplerIndex, const gl::Texture &texture);

        const dx_SamplerMetadata *getData() const;
        size_t sizeBytes() const;
        bool isDirty() const { return mDirty; }
        void markClean() { mDirty = false; }

      private:
        std::vector<dx_SamplerMetadata> mSamplerMetadata;
        bool mDirty;
    };

    template <class TShaderConstants>
    void applyDriverConstantsIfNeeded(TShaderConstants *appliedConstants,
                                      const TShaderConstants &constants,
                                      SamplerMetadataD3D11 *samplerMetadata,
                                      size_t samplerMetadataReferencedBytes,
                                      ID3D11Buffer *driverConstantBuffer);

    gl::Error copyImageInternal(const gl::Framebuffer *framebuffer,
                                const gl::Rectangle &sourceRect,
                                GLenum destFormat,
                                const gl::Offset &destOffset,
                                RenderTargetD3D *destRenderTarget);

    gl::SupportedSampleSet generateSampleSetFromCaps(
        const gl::TextureCaps &colorBufferFormatCaps,
        const gl::TextureCaps &depthStencilBufferFormatCaps) const;

    HMODULE mD3d11Module;
    HMODULE mDxgiModule;
    HMODULE mDCompModule;
    std::vector<D3D_FEATURE_LEVEL> mAvailableFeatureLevels;
    D3D_DRIVER_TYPE mRequestedDriverType;
    bool mCreatedWithDeviceEXT;
    DeviceD3D *mEGLDevice;

    HLSLCompiler mCompiler;

    egl::Error initializeD3DDevice();
    void initializeDevice();
    void releaseDeviceResources();
    void release();

    d3d11::ANGLED3D11DeviceType getDeviceType() const;

    RenderStateCache mStateCache;

    // Currently applied sampler states
    std::vector<bool> mForceSetVertexSamplerStates;
    std::vector<gl::SamplerState> mCurVertexSamplerStates;

    std::vector<bool> mForceSetPixelSamplerStates;
    std::vector<gl::SamplerState> mCurPixelSamplerStates;

    std::vector<bool> mForceSetComputeSamplerStates;
    std::vector<gl::SamplerState> mCurComputeSamplerStates;

    StateManager11 mStateManager;

    // Currently applied primitive topology
    D3D11_PRIMITIVE_TOPOLOGY mCurrentPrimitiveTopology;

    // Currently applied index buffer
    ID3D11Buffer *mAppliedIB;
    DXGI_FORMAT mAppliedIBFormat;
    unsigned int mAppliedIBOffset;
    bool mAppliedIBChanged;

    // Currently applied transform feedback buffers
    uintptr_t mAppliedTFObject;

    // Currently applied shaders
    uintptr_t mAppliedVertexShader;
    uintptr_t mAppliedGeometryShader;
    uintptr_t mAppliedPixelShader;
    uintptr_t mAppliedComputeShader;

    dx_VertexConstants11 mAppliedVertexConstants;
    ID3D11Buffer *mDriverConstantBufferVS;
    SamplerMetadataD3D11 mSamplerMetadataVS;
    ID3D11Buffer *mCurrentVertexConstantBuffer;
    unsigned int mCurrentConstantBufferVS[gl::IMPLEMENTATION_MAX_VERTEX_SHADER_UNIFORM_BUFFERS];
    GLintptr mCurrentConstantBufferVSOffset[gl::IMPLEMENTATION_MAX_VERTEX_SHADER_UNIFORM_BUFFERS];
    GLsizeiptr mCurrentConstantBufferVSSize[gl::IMPLEMENTATION_MAX_VERTEX_SHADER_UNIFORM_BUFFERS];

    dx_PixelConstants11 mAppliedPixelConstants;
    ID3D11Buffer *mDriverConstantBufferPS;
    SamplerMetadataD3D11 mSamplerMetadataPS;
    ID3D11Buffer *mCurrentPixelConstantBuffer;
    unsigned int mCurrentConstantBufferPS[gl::IMPLEMENTATION_MAX_FRAGMENT_SHADER_UNIFORM_BUFFERS];
    GLintptr mCurrentConstantBufferPSOffset[gl::IMPLEMENTATION_MAX_FRAGMENT_SHADER_UNIFORM_BUFFERS];
    GLsizeiptr mCurrentConstantBufferPSSize[gl::IMPLEMENTATION_MAX_FRAGMENT_SHADER_UNIFORM_BUFFERS];

    dx_ComputeConstants11 mAppliedComputeConstants;
    ID3D11Buffer *mDriverConstantBufferCS;
    SamplerMetadataD3D11 mSamplerMetadataCS;
    ID3D11Buffer *mCurrentComputeConstantBuffer;

    ID3D11Buffer *mCurrentGeometryConstantBuffer;

    // Vertex, index and input layouts
    VertexDataManager *mVertexDataManager;
    IndexDataManager *mIndexDataManager;
    InputLayoutCache mInputLayoutCache;

    StreamingIndexBufferInterface *mLineLoopIB;
    StreamingIndexBufferInterface *mTriangleFanIB;

    // Texture copy resources
    Blit11 *mBlit;
    PixelTransfer11 *mPixelTransfer;

    // Masked clear resources
    Clear11 *mClear;

    // Perform trim for D3D resources
    Trim11 *mTrim;

    // Sync query
    ID3D11Query *mSyncQuery;

    // Created objects state tracking
    std::set<const Buffer11 *> mAliveBuffers;

    double mLastHistogramUpdateTime;

    ID3D11Device *mDevice;
    Renderer11DeviceCaps mRenderer11DeviceCaps;
    ID3D11DeviceContext *mDeviceContext;
    ID3D11DeviceContext1 *mDeviceContext1;
    IDXGIAdapter *mDxgiAdapter;
    DXGI_ADAPTER_DESC mAdapterDescription;
    char mDescription[128];
    IDXGIFactory *mDxgiFactory;
    ID3D11Debug *mDebug;

    std::vector<GLuint> mScratchIndexDataBuffer;

    angle::ScratchBuffer mScratchMemoryBuffer;

    gl::DebugAnnotator *mAnnotator;

    mutable Optional<bool> mSupportsShareHandles;
    ResourceManager11 mResourceManager11;
};

}  // namespace rx
#endif  // LIBANGLE_RENDERER_D3D_D3D11_RENDERER11_H_
