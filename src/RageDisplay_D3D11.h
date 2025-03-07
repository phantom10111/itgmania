/* RageDisplay_D3D11 - Direct3D11 renderer. */

#ifndef RAGE_DISPLAY_D3D11_H
#define RAGE_DISPLAY_D3D11_H

#include "RageDisplay.h"

#include <wrl/client.h>
#include <d3d11_1.h>
#include <DirectXMath.h>

#include <cstdint>

// TODO how many lights?
#define D3D11_MAX_LIGHTS 8u
#define D3D11_MAX_TEXTURES 4u

class RageDisplay_D3D11: public RageDisplay
{
public:
	RageDisplay_D3D11();
	virtual ~RageDisplay_D3D11();
	virtual RString Init( const VideoModeParams &p, bool bAllowUnacceleratedRenderer );

	virtual RString GetApiDescription() const { return "D3D11"; }
	virtual void GetDisplaySpecs( DisplaySpecs &out ) const;
	void ResolutionChanged();
	const RagePixelFormatDesc *GetPixelFormatDesc(RagePixelFormat pf) const;

	bool BeginFrame();
	void EndFrame();
	ActualVideoModeParams GetActualVideoModeParams() const;
	void SetBlendMode( BlendMode mode );
	bool SupportsTextureFormat( RagePixelFormat pixfmt, bool realtime=false );
	bool SupportsThreadedRendering();
	bool SupportsPerVertexMatrixScale() { return false; }
	std::uintptr_t CreateTexture(
		RagePixelFormat pixfmt,
		RageSurface* img,
		bool bGenerateMipMaps );
	void UpdateTexture(
		std::uintptr_t iTexHandle,
		RageSurface* img,
		int xoffset, int yoffset, int width, int height
		);
	void DeleteTexture( std::uintptr_t iTexHandle );
	RageTextureLock* CreateTextureLock();
	void ClearAllTextures();
	int GetNumTextureUnits();
	void SetTexture( TextureUnit tu, std::uintptr_t iTexture );
	void SetTextureMode( TextureUnit tu, TextureMode tm );
	void SetTextureWrapping( TextureUnit tu, bool b );
	int GetMaxTextureSize() const;
	void SetTextureFiltering( TextureUnit tu, bool b );
	void SetEffectMode( EffectMode effect );
	bool IsEffectModeSupported( EffectMode effect );
	bool SupportsRenderToTexture() const { return true; }
	bool SupportsFullscreenBorderlessWindow() const { return true; }
	std::uintptr_t CreateRenderTarget( const RenderTargetParam& param, int& iTextureWidthOut, int& iTextureHeightOut );
	std::uintptr_t GetRenderTarget();
	void SetRenderTarget( std::uintptr_t iHandle, bool bPreserveTexture );
	bool IsZWriteEnabled() const;
	bool IsZTestEnabled() const;
	void SetZWrite( bool b );
	void SetZBias( float f );
	void SetZTestMode( ZTestMode mode );
	void ClearZBuffer();
	void SetCullMode( CullMode mode );
	void SetAlphaTest( bool b );
	void SetMaterial(
		const RageColor &emissive,
		const RageColor &ambient,
		const RageColor &diffuse,
		const RageColor &specular,
		float shininess
		);
	void SetLighting( bool b );
	void SetLightOff( int index );
	void SetLightDirectional(
		int index,
		const RageColor &ambient,
		const RageColor &diffuse,
		const RageColor &specular,
		const RageVector3 &dir );

	void SetSphereEnvironmentMapping( TextureUnit tu, bool b );
	void SetCelShaded( int stage );

	RageCompiledGeometry* CreateCompiledGeometry();
	void DeleteCompiledGeometry( RageCompiledGeometry* p );

protected:
	void DrawQuadsInternal( const RageSpriteVertex v[], int iNumVerts );
	void DrawQuadStripInternal( const RageSpriteVertex v[], int iNumVerts );
	void DrawFanInternal( const RageSpriteVertex v[], int iNumVerts );
	void DrawStripInternal( const RageSpriteVertex v[], int iNumVerts );
	void DrawTrianglesInternal( const RageSpriteVertex v[], int iNumVerts );
	void DrawSymmetricQuadStripInternal( const RageSpriteVertex v[], int iNumVerts );
	void DrawCompiledGeometryInternal( const RageCompiledGeometry *p, int iMeshIndex );
	void DrawLineStripInternal( const RageSpriteVertex v[], int iNumVerts, float LineWidth );

	RString TryVideoMode( const VideoModeParams &p, bool &bNewDeviceOut );
	RageSurface* CreateScreenshot();
	RageMatrix GetOrthoMatrix( float l, float r, float b, float t, float zn, float zf );
	RageMatrix GetFrustumMatrix( float l, float r, float b, float t, float zn, float zf );

	void UpdateTransforms();
	void BindRenderingState();
	void BindVertexBuffers( const RageSpriteVertex v[], int iNumVerts );

	bool m_bAllowTearing = false;

	HMODULE m_dxgiDebugModule = nullptr;
	Microsoft::WRL::ComPtr<IDXGIFactory2> m_pDxgiFactory;
	Microsoft::WRL::ComPtr<IDXGIAdapter1> m_pDxgiAdapter;
	Microsoft::WRL::ComPtr<ID3D11Device> m_pDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_pDeviceContext;
	Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> m_pUserDefinedAnnotation;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> m_pSwapchain;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pRenderTarget;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_pRenderTargetView;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_pDepthStencilView;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_pBoundDepthStencilView;
	// TODO delegate this to a separate pipeline object
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_pModelVertexShader;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_pModelTextureMatrixScaleVertexShader;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_pSpriteVertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pBuiltinPixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_pModelInputLayout;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_pModelTextureMatrixScaleInputLayout;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_pSpriteInputLayout;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pConstantBufferVS;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pConstantBufferPS;

	UINT m_iRenderTargetHeight;
	UINT m_iRenderTargetWidth;

	struct RageTexture_D3D11* m_pCurrentRenderTarget = nullptr;
	D3D11_VIEWPORT m_Viewport;

	// TODO suboptimal shit, proof of concept for now
	// fixing requires pretty big refactor of how Actor::DrawPrimitives() work
	bool m_bRasterizerStateChanged = false;
	D3D11_RASTERIZER_DESC m_RasterizerDesc = CD3D11_RASTERIZER_DESC(CD3D11_DEFAULT{});

	BlendMode m_CurrentBlendMode = BLEND_COPY_SRC;
	bool m_bBlendStateChanged = false;
	D3D11_BLEND_DESC m_BlendDesc = CD3D11_BLEND_DESC(CD3D11_DEFAULT{});

	bool m_bDepthStateChanged = false;
	D3D11_DEPTH_STENCIL_DESC m_DepthStencilDesc = CD3D11_DEPTH_STENCIL_DESC(CD3D11_DEFAULT{});

	struct LightData
	{
		DirectX::XMFLOAT4A ambient;
		DirectX::XMFLOAT4A diffuse;
		DirectX::XMFLOAT4A specular;
		DirectX::XMFLOAT3A direction;
	};

	bool m_bLightsChanged = false;
	bool m_bLightingEnabled = false;
	bool m_bLightsEnabled[D3D11_MAX_LIGHTS];
	LightData m_Lights[D3D11_MAX_LIGHTS];

	bool m_bConstantBufferVSChanged = true; // Changed because no buffer is bound by default
	struct alignas(16) ConstantsVS
	{
		DirectX::XMFLOAT4X4A vertexProjectionTransform;
		DirectX::XMFLOAT4X4A vertexEyeTransform;
		// 4th column of normalTransform is not used but it's here to get correct alignment
		DirectX::XMFLOAT3X4A normalTransform;
		DirectX::XMFLOAT4X4A texcoordTransform;
		std::uint32_t numLights;
		float materialShininess;
		DirectX::XMFLOAT4A materialAmbient;
		DirectX::XMFLOAT4A materialDiffuse;
		DirectX::XMFLOAT4A materialSpecular;
		DirectX::XMFLOAT4A materialEmission;
		DirectX::XMFLOAT4A defaultVertexColor;
		LightData lights[D3D11_MAX_LIGHTS];
	} m_ConstantBufferVS;

	bool m_bTexturesChanged = false;
	bool m_bSamplerStateChanged[D3D11_MAX_TEXTURES];
	D3D11_SAMPLER_DESC m_SamplerStates[D3D11_MAX_TEXTURES];
	Microsoft::WRL::ComPtr<ID3D11SamplerState> m_pSamplerStates[D3D11_MAX_TEXTURES];
	std::uintptr_t m_iTextures[D3D11_MAX_TEXTURES];
	TextureMode m_TextureModes[D3D11_MAX_TEXTURES];
	bool m_bSphereMapping[D3D11_MAX_TEXTURES];

	bool m_bConstantBufferPSChanged = true; // Changed because no buffer is bound by default
	struct alignas(16) ConstantsPS
	{
		std::uint32_t numTextures;
		std::uint32_t textureModes;
		bool bAlphaTestEnabled;
	} m_ConstantBufferPS;
};

#endif

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
