//
// Copyright (c) 2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Clear11.h: Framebuffer clear utility class.

#ifndef LIBANGLE_RENDERER_D3D_D3D11_CLEAR11_H_
#define LIBANGLE_RENDERER_D3D_D3D11_CLEAR11_H_

#include <map>
#include <vector>

#include "libANGLE/angletypes.h"
#include "libANGLE/Error.h"
#include "libANGLE/Framebuffer.h"
#include "libANGLE/renderer/d3d/d3d11/renderer11_utils.h"

namespace rx
{
class Renderer11;
class RenderTarget11;
struct ClearParameters;

template <typename T>
struct RtvDsvClearInfo
{
    T r, g, b, a;
    float z;
    float c1padding[3];
};

class Clear11 : angle::NonCopyable
{
  public:
    explicit Clear11(Renderer11 *renderer);
    ~Clear11();

    // Clears the framebuffer with the supplied clear parameters, assumes that the framebuffer is currently applied.
    gl::Error clearFramebuffer(const ClearParameters &clearParams,
                               const gl::FramebufferState &fboData);

  private:
    class ShaderManager final : angle::NonCopyable
    {
      public:
        ShaderManager();
        ~ShaderManager();
        void getShadersAndLayout(ID3D11Device *device,
                                 D3D_FEATURE_LEVEL featureLevel,
                                 const INT clearType,
                                 ID3D11InputLayout **il,
                                 ID3D11VertexShader **vs,
                                 ID3D11PixelShader **ps);

      private:
        angle::ComPtr<ID3D11InputLayout> mIl9;
        d3d11::LazyShader<ID3D11VertexShader> mVs9;
        d3d11::LazyShader<ID3D11PixelShader> mPsFloat9;
        d3d11::LazyShader<ID3D11VertexShader> mVs;
        d3d11::LazyShader<ID3D11PixelShader> mPsFloat;
        d3d11::LazyShader<ID3D11PixelShader> mPsUInt;
        d3d11::LazyShader<ID3D11PixelShader> mPsSInt;
    };

    Renderer11 *mRenderer;

    // States
    angle::ComPtr<ID3D11RasterizerState> mScissorEnabledRasterizerState;
    angle::ComPtr<ID3D11RasterizerState> mScissorDisabledRasterizerState;
    gl::DepthStencilState mDepthStencilStateKey;
    d3d11::BlendStateKey mBlendStateKey;

    // Shaders and shader resources
    ShaderManager mShaderManager;
    angle::ComPtr<ID3D11Buffer> mConstantBuffer;
    angle::ComPtr<ID3D11Buffer> mVertexBuffer;

    // Buffer data and draw parameters
    RtvDsvClearInfo<float> mShaderData;
};

}

#endif // LIBANGLE_RENDERER_D3D_D3D11_CLEAR11_H_
