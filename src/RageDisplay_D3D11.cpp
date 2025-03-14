#include "global.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageDisplay_D3D11.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageException.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "EnumHelper.h"
#include "DisplaySpec.h"
#include "LocalizedString.h"

#include "archutils/Win32/GraphicsWindow.h"

// Static libraries
#if defined(_MSC_VER)
	#pragma comment(lib, "d3d11.lib")
	#pragma comment(lib, "d3dcompiler.lib")
	#pragma comment(lib, "dxgi.lib")
#ifdef DEBUG
	#pragma comment(lib, "dxguid.lib")
#endif
#endif

#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>


static constexpr const char BUILTIN_SHADER[] = {
#include "RageDisplay_Builtin_Shaders.h"
};

// TODO: Instead of defining this here, enumerate the possible formats and select whatever one we want to use. This format should
// be fine for the uses of this application though.
const DXGI_FORMAT g_DefaultAdapterFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

static const RageDisplay::RagePixelFormatDesc PIXEL_FORMAT_DESC[NUM_RagePixelFormat] = {
	{
		/* R8G8B8A8 */
		32,
		{ 0x000000FF,
		  0x0000FF00,
		  0x00FF0000,
		  0xFF000000 }
	}, {
		/* B8G8R8A8 */
		32,
		{ 0x00FF0000,
		  0x0000FF00,
		  0x000000FF,
		  0xFF000000 }
	}, {
		/* B4G4R4A4 */
		16,
		{ 0x0F00,
		  0x00F0,
		  0x000F,
		  0xF000 }
	}, {
		/* B5G5R5A1 */
		16,
		{ 0x7C00,
		  0x03E0,
		  0x001F,
		  0x8000 }
	}, {
		/* RGB5 (N/A) */
		0, { 0,0,0,0 }
	}, {
		/* RGB8 (N/A) */
		0, { 0,0,0,0 }
	}, {
		/* Paletted (N/A) */
		0, { 0,0,0,0 }
	}, {
		/* B8G8R8X8 */
		32,
		{ 0x00FF0000,
		  0x0000FF00,
		  0x000000FF,
		  0x00000000 }
	}, {
		/* B5G5R5A1 */
		16,
		{ 0x7C00,
		  0x03E0,
		  0x001F,
		  0x8000 }
	}, {
		/* X1R5G5B5 (N/A) */
		0, { 0,0,0,0 }
	}
};

// TODO should we use TYPELESS format here? Or maybe SRGB
// TODO legacy formats 16 bit formats don't use srgb color. Maybe don't use them at all?
static DXGI_FORMAT DXGI_FORMATS[NUM_RagePixelFormat] =
{
	DXGI_FORMAT_R8G8B8A8_UNORM,
	DXGI_FORMAT_B8G8R8A8_UNORM,
	DXGI_FORMAT_B4G4R4A4_UNORM,
	DXGI_FORMAT_B5G5R5A1_UNORM,
	DXGI_FORMAT_UNKNOWN, // RGB5
	DXGI_FORMAT_UNKNOWN, // RGB8
	DXGI_FORMAT_UNKNOWN, // PAL
	DXGI_FORMAT_B8G8R8X8_UNORM,
	DXGI_FORMAT_B5G5R5A1_UNORM,
	DXGI_FORMAT_UNKNOWN, // X1R5G5B5
};

const RageDisplay::RagePixelFormatDesc *RageDisplay_D3D11::GetPixelFormatDesc(RagePixelFormat pf) const
{
	ASSERT( pf < NUM_RagePixelFormat );
	return &PIXEL_FORMAT_DESC[pf];
}


RageDisplay_D3D11::RageDisplay_D3D11()
{
	m_Viewport.TopLeftX = 0;
	m_Viewport.TopLeftY = 0;
	m_Viewport.MinDepth = 0.05f;
	m_Viewport.MaxDepth = 1.f;

	m_bRasterizerStateChanged = true;
	m_RasterizerDesc.AntialiasedLineEnable = TRUE;

	for (int i = 0; i < D3D11_MAX_LIGHTS; ++i)
		m_bLightsEnabled[i] = false;

	for (int i = 0; i < D3D11_MAX_TEXTURES; ++i)
	{
		m_bSamplerStateChanged[i] = false;
		m_SamplerStates[i] = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT{});

		m_iTextures[i] = 0;
		m_TextureModes[i] = TextureMode_Modulate;
		m_bSphereMapping[i] = false;
	}

	m_ConstantBufferVS.numLights = 0;
	m_ConstantBufferVS.materialShininess = 0.f;
	m_ConstantBufferVS.defaultVertexColor = DirectX::XMFLOAT4A{1.f, 1.f, 1.f, 1.f};
	m_ConstantBufferVS.materialAmbient = DirectX::XMFLOAT4A{0.f, 0.f, 0.f, 1.f};
	m_ConstantBufferVS.materialDiffuse = DirectX::XMFLOAT4A{1.f, 1.f, 1.f, 1.f};
	m_ConstantBufferVS.materialSpecular = DirectX::XMFLOAT4A{0.f, 0.f, 0.f, 1.f};
	m_ConstantBufferVS.materialEmission = DirectX::XMFLOAT4A{0.f, 0.f, 0.f, 1.f};

	m_ConstantBufferPS.numTextures = 0;
	m_ConstantBufferPS.bAlphaTestEnabled = true;
}

RString RageDisplay_D3D11::Init( const VideoModeParams &p, bool /* bAllowUnacceleratedRenderer */ )
{
	GraphicsWindow::Initialize( true );

	LOG->Trace( "RageDisplay_D3D11::RageDisplay_D3D11()" );
	LOG->MapLog("renderer", "Current renderer: Direct3D11");

#ifdef DEBUG
	static constexpr bool bDebugRenderer = true;
#else
	const bool bDebugRenderer = PREFSMAN->m_bDebugRenderer;
#endif

	if (bDebugRenderer && !m_dxgiDebugModule)
	{
		// Loading the library activates the DXGI Debug layer, no other calls are necessary
		m_dxgiDebugModule = LoadLibraryEx("DXGIDebug.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

		if (m_dxgiDebugModule)
		{
// Only setup breakpoints in debug configuration
#ifdef DEBUG
			const auto pDXGIGetDebugInterface = reinterpret_cast<decltype(&DXGIGetDebugInterface)>(GetProcAddress(m_dxgiDebugModule, "DXGIGetDebugInterface"));

			Microsoft::WRL::ComPtr<IDXGIInfoQueue> pDxgiInfoQueue;
			HRESULT hr = pDXGIGetDebugInterface(IID_PPV_ARGS(&pDxgiInfoQueue));
			ASSERT(SUCCEEDED(hr));

			pDxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			pDxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
			pDxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, TRUE);
#endif
		}
		else
		{
			LOG->Warn("RageDisplay_D3D11: Debug device requested but unable to load DXGIDebug.dll\nWindows SDK must be installed to take full advantage of various graphics debug layers");
		}
	}

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_pDxgiFactory));
	ASSERT(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDXGIFactory5> pDxgiFactory5;
	hr = m_pDxgiFactory.As(&pDxgiFactory5);
	if (hr != E_NOINTERFACE)
	{
		ASSERT(SUCCEEDED(hr));

		BOOL allowTearing;
		hr = pDxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
		ASSERT(SUCCEEDED(hr));

		// TODO add a preference for this
		m_bAllowTearing = allowTearing;
	}
	else
		m_bAllowTearing = false;

	Microsoft::WRL::ComPtr<IDXGIAdapter1> pDxgiAdapter;

	Microsoft::WRL::ComPtr<IDXGIFactory6> pDxgiFactory6;
	hr = m_pDxgiFactory.As(&pDxgiFactory6);
	if (hr != E_NOINTERFACE)
	{
		ASSERT(SUCCEEDED(hr));

		UINT i = 0;
		while(true)
		{
			// TODO add preference for user to specify intergrted graphics (DXGI_GPU_PREFERENCE_MINIMUM_POWER) here
			hr = pDxgiFactory6->EnumAdapterByGpuPreference(i++, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pDxgiAdapter));

			if (hr == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}

			// TODO the loop makes no sense if we're just always gonna use the first adapter
			ASSERT(SUCCEEDED(hr));
			break;
		}
	}

	Microsoft::WRL::ComPtr<ID3D11Device> pDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> pDeviceContext;

	// TODO D3D11_CREATE_DEVICE_DEBUG requires D3D11*SDKLayers.dll installed
	// TODO D3D11_CREATE_DEVICE_DEBUGGABLE requires requires D3D11_1SDKLayers.dll installed and feature level 11_1
	// const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | (bDebugRenderer ? D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_DEBUGGABLE : 0);
	const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | (bDebugRenderer ? D3D11_CREATE_DEVICE_DEBUG : 0);
	const D3D_DRIVER_TYPE driverType = pDxgiAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
	// TODO 11_1 or 11_0?
	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
	hr = D3D11CreateDevice(
		pDxgiAdapter.Get(),
		driverType,
		nullptr,
		flags,
		&featureLevel,
		1,
		D3D11_SDK_VERSION,
		&pDevice,
		nullptr,
		&pDeviceContext);

	if (!SUCCEEDED(hr)) {
		LOG->Trace("D3D11CreateDevice failed");
		return "D3D11CreateDevice failed";
	}

	hr = pDevice.As(&m_pDevice);
	ASSERT(SUCCEEDED(hr));

	hr = pDeviceContext.As(&m_pDeviceContext);
	ASSERT(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDXGIDevice> pDxgiDevice;
	hr = m_pDevice.As(&pDxgiDevice);
	ASSERT(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDXGIAdapter> pDxgiAdapterInUse;
	hr = pDxgiDevice->GetAdapter(&pDxgiAdapterInUse);
	ASSERT(SUCCEEDED(hr));

	hr = pDxgiAdapterInUse.As(&m_pDxgiAdapter);
	ASSERT(SUCCEEDED(hr));

	// Only setup breakpoints in debug configuration
#ifdef DEBUG
	Microsoft::WRL::ComPtr<ID3D11InfoQueue> pInfoQueue;
	hr = m_pDevice.As(&pInfoQueue);
	if (hr != E_NOINTERFACE)
	{
		ASSERT(SUCCEEDED(hr));

		pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, TRUE);
	}
#endif

	// TODO make use of this somehow - unused currently but potentially useful for debugging
	hr = m_pDeviceContext.As(&m_pUserDefinedAnnotation);
	ASSERT(SUCCEEDED(hr));

	DXGI_ADAPTER_DESC adapterDesc;
	hr = pDxgiAdapter->GetDesc(&adapterDesc);
	ASSERT(SUCCEEDED(hr));

	LOG->Trace(
		"Description: %ls\n"
		"VendorId: 0x%04X\n"
		"DeviceId: 0x%04X\n"
		"SubSysId: 0x%08X\n"
		"Revision: 0x%04X\n"
		"Dedicated video memory: %zu\n"
		"Dedicated system memory: %zu\n"
		"Shared system memory: %zu",
		adapterDesc.Description,
		adapterDesc.VendorId,
		adapterDesc.DeviceId,
		adapterDesc.SubSysId,
		adapterDesc.Revision);
#if 0
	// TODO this depends on format but we haven't chosen one yet so just don't print this? Or figure something out idk
	LOG->Trace( "This display adapter supports the following outputs and modes:" );
	for (UINT i = 0; ; ++i)
	{
		Microsoft::WRL::ComPtr<IDXGIOutput> pOutput;
		hr = pDxgiAdapter->EnumOutputs(i, &pOutput);

		if (hr == DXGI_ERROR_NOT_FOUND)
			break;
		ASSERT(SUCCEEDED(hr));

		DXGI_OUTPUT_DESC outputDesc;
		hr = pOutput->GetDesc(&outputDesc);
		ASSERT(SUCCEEDED(hr));

		LOG->Trace("  Output %u (%ls):", i, outputDesc.DeviceName);

		std::vector<DXGI_MODE_DESC> outputModes;
		UINT numModes = 0;
		do
		{
			hr = pOutput->GetDisplayModeList();
			TODO
		}
	}
#endif

#ifdef DEBUG
	static constexpr const bool bDebugShaders = true;
#else
	const bool bDebugShaders = PREFSMAN->m_bDebugShaders;
#endif

#define QUOTE(x) QUOTE2(x)
#define QUOTE2(x) #x
	D3D_SHADER_MACRO shaderDefinitions[] = { {"MAX_LIGHTS", QUOTE(D3D11_MAX_LIGHTS)}, {"MAX_TEXTURES", QUOTE(D3D11_MAX_TEXTURES)}, {"VERTEX_HAS_COLOR", "0"}, {"VERTEX_HAS_TEXTURE_MATRIX_SCALE", "0"}, {nullptr, nullptr} };
#undef QUOTE
#undef QUOTE2

	const UINT shaderCompileFlags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | (bDebugShaders ? (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION) : D3DCOMPILE_OPTIMIZATION_LEVEL3);

	Microsoft::WRL::ComPtr<ID3DBlob> pErrorMsgs;

#define COMPILE_SHADER(name, source, entryPoint, target, outBytecode) \
	do { \
		hr = D3DCompile(source, sizeof(source), name, shaderDefinitions, nullptr, entryPoint, target, shaderCompileFlags, 0, outBytecode, &pErrorMsgs); \
		if (!SUCCEEDED(hr)) \
		{ \
			char buffer[1024]; \
			ASSERT(FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), buffer, sizeof(buffer), nullptr)); \
			return ssprintf("Compiling shader \"%s\":D3DCompile failed %lu(%s): %s", name, hr, buffer, pErrorMsgs->GetBufferPointer()); \
		} \
	} while (false)

	Microsoft::WRL::ComPtr<ID3DBlob> pModelVSBytecode;
	COMPILE_SHADER("builtin model vs", BUILTIN_SHADER, "VSMain", "vs_5_0", &pModelVSBytecode);

	shaderDefinitions[3].Definition = "1"; // #define VERTEX_HAS_TEXTURE_MATRIX_SCALE 1
	Microsoft::WRL::ComPtr<ID3DBlob> pModelTextureMatrixScaleVSBytecode;
	COMPILE_SHADER("builtin model with texture matrix scale vs", BUILTIN_SHADER, "VSMain", "vs_5_0", &pModelTextureMatrixScaleVSBytecode);

	shaderDefinitions[2].Definition = "1"; // #define VERTEX_HAS_COLOR 1
	shaderDefinitions[3].Definition = "0"; // #define VERTEX_HAS_TEXTURE_MATRIX_SCALE 0
	Microsoft::WRL::ComPtr<ID3DBlob> pSpriteVSBytecode;
	COMPILE_SHADER("builtin sprite vs", BUILTIN_SHADER, "VSMain", "vs_5_0", &pSpriteVSBytecode);

	Microsoft::WRL::ComPtr<ID3DBlob> pBuiltinPSBytecode;
	COMPILE_SHADER("builtin ps", BUILTIN_SHADER, "PSMain", "ps_5_0", &pBuiltinPSBytecode);

#undef COMPILE_SHADER

	hr = m_pDevice->CreateVertexShader(pModelVSBytecode->GetBufferPointer(), pModelVSBytecode->GetBufferSize(), nullptr, &m_pModelVertexShader);
	ASSERT(SUCCEEDED(hr));

	hr = m_pDevice->CreateVertexShader(pModelTextureMatrixScaleVSBytecode->GetBufferPointer(), pModelTextureMatrixScaleVSBytecode->GetBufferSize(), nullptr, &m_pModelTextureMatrixScaleVertexShader);
	ASSERT(SUCCEEDED(hr));

	hr = m_pDevice->CreateVertexShader(pSpriteVSBytecode->GetBufferPointer(), pSpriteVSBytecode->GetBufferSize(), nullptr, &m_pSpriteVertexShader);
	ASSERT(SUCCEEDED(hr));

	hr = m_pDevice->CreatePixelShader(pBuiltinPSBytecode->GetBufferPointer(), pBuiltinPSBytecode->GetBufferSize(), nullptr, &m_pBuiltinPixelShader);
	ASSERT(SUCCEEDED(hr));

	static_assert(offsetof(RageModelVertex, p) == offsetof(RageSpriteVertex, p));
	static_assert(offsetof(RageModelVertex, n) == offsetof(RageSpriteVertex, n));
	D3D11_INPUT_ELEMENT_DESC inputElementDescs[4] = {
		{"SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(RageModelVertex, p), D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(RageModelVertex, n), D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(RageModelVertex, t), D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	hr = m_pDevice->CreateInputLayout(inputElementDescs, 3, pModelVSBytecode->GetBufferPointer(), pModelVSBytecode->GetBufferSize(), &m_pModelInputLayout);
	ASSERT(SUCCEEDED(hr));

	inputElementDescs[3] = {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
	hr = m_pDevice->CreateInputLayout(inputElementDescs, 4, pModelTextureMatrixScaleVSBytecode->GetBufferPointer(), pModelTextureMatrixScaleVSBytecode->GetBufferSize(), &m_pModelTextureMatrixScaleInputLayout);
	ASSERT(SUCCEEDED(hr));

	inputElementDescs[2] = {"COLOR", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(RageSpriteVertex, c), D3D11_INPUT_PER_VERTEX_DATA, 0};
	inputElementDescs[3] = {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(RageSpriteVertex, t), D3D11_INPUT_PER_VERTEX_DATA, 0};
	hr = m_pDevice->CreateInputLayout(inputElementDescs, 4, pSpriteVSBytecode->GetBufferPointer(), pSpriteVSBytecode->GetBufferSize(), &m_pSpriteInputLayout);
	ASSERT(SUCCEEDED(hr));

	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.ByteWidth = sizeof(m_ConstantBufferVS);
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	hr = m_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_pConstantBufferVS);
	ASSERT(SUCCEEDED(hr));

	bufferDesc.ByteWidth = sizeof(m_ConstantBufferPS);
	hr = m_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_pConstantBufferPS);
	ASSERT(SUCCEEDED(hr));

	if (bDebugRenderer)
	{
		// By default all samplers are bound to NULL, which is equivalent to the default configuration.
		// This is what we want but it generates a warning in debug configuration, so explicitly bind default samplers to silence the warning.
		const D3D11_SAMPLER_DESC defaultSamplerDesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT{});
		Microsoft::WRL::ComPtr<ID3D11SamplerState> defaultSamplerState;
		hr = m_pDevice->CreateSamplerState(&defaultSamplerDesc, &defaultSamplerState);
		ASSERT(SUCCEEDED(hr));

		for (unsigned i = 0; i < D3D11_MAX_TEXTURES; ++i)
			m_pSamplerStates[i] = defaultSamplerState.Get();

		m_pDeviceContext->PSSetSamplers(0, D3D11_MAX_TEXTURES, m_pSamplerStates[0].GetAddressOf());
	}

	//TODO fix comment
	/* Up until now, all we've done is set up g_pd3d and do some queries. Now,
	 * actually initialize the window. Do this after as many error conditions as
	 * possible, because if we have to shut it down again we'll flash a window briefly. */
	bool bIgnore = false;
	return SetVideoMode( p, bIgnore );
}

RageDisplay_D3D11::~RageDisplay_D3D11()
{
	LOG->Trace( "RageDisplay_D3D11::~RageDisplay()" );

	GraphicsWindow::Shutdown();

	// Apparently swapchain can't be released while fullscreen, so switch it to windowed mode
	if(m_pSwapchain)
		m_pSwapchain->SetFullscreenState(FALSE, nullptr);

	// TODO how to unload this module after all ComPtrs?
	if (m_dxgiDebugModule)
		FreeLibrary(m_dxgiDebugModule);
}

void RageDisplay_D3D11::GetDisplaySpecs( DisplaySpecs &out ) const
{
	UINT outputNum = 0;
	while (true)
	{
		Microsoft::WRL::ComPtr<IDXGIOutput> pDxgiOutput;
		HRESULT hr = m_pDxgiAdapter->EnumOutputs(outputNum++, &pDxgiOutput);
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;
		ASSERT(SUCCEEDED(hr));

		UINT numModes;
		hr = pDxgiOutput->GetDisplayModeList(g_DefaultAdapterFormat, DXGI_ENUM_MODES_INTERLACED | DXGI_ENUM_MODES_SCALING, &numModes, nullptr);
		ASSERT(SUCCEEDED(hr));

		std::unique_ptr<DXGI_MODE_DESC[]> pModes;
		do {
			// TODO does MSVC not support this?
			// pModes = std::make_unique_for_overwrite<DXGI_MODE_DESC[]>(numModes);
			pModes = std::make_unique<DXGI_MODE_DESC[]>(numModes);
			hr = pDxgiOutput->GetDisplayModeList(g_DefaultAdapterFormat, DXGI_ENUM_MODES_INTERLACED | DXGI_ENUM_MODES_SCALING, &numModes, pModes.get());
		} while (hr == DXGI_ERROR_MORE_DATA);
		ASSERT(SUCCEEDED(hr));

		std::set<DisplayMode> modes;
		for (UINT i = 0; i < numModes; ++i)
			modes.emplace(DisplayMode{ pModes[i].Width, pModes[i].Height, static_cast<double>(pModes[i].RefreshRate.Numerator) / pModes[i].RefreshRate.Denominator });

		DXGI_OUTPUT_DESC outputDesc;
		hr = pDxgiOutput->GetDesc(&outputDesc);
		ASSERT(SUCCEEDED(hr));

		//TODO do I need to handle DPI here? https://stackoverflow.com/questions/70976583/get-real-screen-resolution-using-win32-api
		MONITORINFO monitorInfo;
		monitorInfo.cbSize = sizeof(monitorInfo);
		ASSERT(GetMonitorInfo(outputDesc.Monitor, &monitorInfo));

		// TODO which mode is the currently active one?
		out.emplace(DisplaySpec{ "HMONITOR", ssprintf("%p", outputDesc.Monitor), std::move(modes), *modes.begin(), RectI{monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top, monitorInfo.rcMonitor.right, monitorInfo.rcMonitor.bottom} });
	}
}

DXGI_FORMAT FindBackBufferType(ID3D11Device *pDevice, int iBPP)
{
	HRESULT hr;

	std::vector<DXGI_FORMAT> vBackBufferFormats; // throw all possibilities in here

	if( iBPP == 32 )
	{
		vBackBufferFormats.push_back( DXGI_FORMAT_R8G8B8A8_UNORM );
		vBackBufferFormats.push_back( DXGI_FORMAT_B8G8R8A8_UNORM );
		vBackBufferFormats.push_back( DXGI_FORMAT_B8G8R8X8_UNORM );
	}
	// TODO should we even bother with 16 bits at this point? Or just only support 32 bits
	if( iBPP == 16 )
	{
		vBackBufferFormats.push_back( DXGI_FORMAT_B4G4R4A4_UNORM );
		vBackBufferFormats.push_back( DXGI_FORMAT_B5G5R5A1_UNORM );
	}

	if( iBPP != 16 && iBPP != 32 )
	{
		GraphicsWindow::Shutdown();
		RageException::Throw( "Invalid BPP '%i' specified", iBPP );
	}

	// Test each back buffer format until we find something that works.
	for( std::size_t i=0; i < vBackBufferFormats.size(); i++ )
	{
		LOG->Trace( "Testing format: %d...",
					vBackBufferFormats[i] );

		UINT formatSupport;
		hr = pDevice->CheckFormatSupport(vBackBufferFormats[i], &formatSupport);
		ASSERT(SUCCEEDED(hr));

		static constexpr const UINT requiredFlags = D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_DISPLAY;
		if( (formatSupport & requiredFlags) != requiredFlags )
			continue; // skip

		// done searching
		LOG->Trace( "This will work." );
		return vBackBufferFormats[i];
	}

	LOG->Trace( "Couldn't find an appropriate back buffer format." );
	return DXGI_FORMAT_UNKNOWN;
}

// Set the video mode.
RString RageDisplay_D3D11::TryVideoMode( const VideoModeParams &p, bool &bNewDeviceOut )
{
	LOG->Warn( "RageDisplay_D3D11::TryVideoMode( %d, %d, %d, %d, %d, %d )", p.windowed, p.width, p.height, p.bpp, p.rate, p.vsync );

	const DXGI_FORMAT format = FindBackBufferType( m_pDevice.Get(), p.bpp );
	if( format == DXGI_FORMAT_UNKNOWN )	// no possible back buffer formats
		return ssprintf( "FindBackBufferType(%i) failed", p.bpp );	// failed to set mode

	/* Set up and display the window before setting up D3D. If we don't do this,
	 * then setting up a fullscreen window (when we're not coming from windowed)
	 * causes all other windows on the system to be resized to the new resolution. */
	GraphicsWindow::CreateGraphicsWindow( p );

	// TODO can we actually make use of mode switch? Need to call IDXGISwapChain::ResizeBuffers()
	const UINT swapchainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | (m_bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {
		static_cast<UINT>(p.width),
		static_cast<UINT>(p.height),
		format,
		FALSE, // Stereo
		{1, 0}, // DXGI_SAMPLE_DESC {Count, Quality}
		DXGI_USAGE_RENDER_TARGET_OUTPUT,
		2 /* TODO is this enough? */, // Buffer count
		DXGI_SCALING_STRETCH,
		DXGI_SWAP_EFFECT_FLIP_DISCARD,
		DXGI_ALPHA_MODE_IGNORE,
		swapchainFlags
	};

	//TODO perhaps we should use values from DXGI_MODE_DESC here
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {
		{static_cast<UINT>(p.rate), 1}, // DXGI_RATIONAL {Numerator, Denominator}
		DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE,
		DXGI_MODE_SCALING_STRETCHED,
		p.windowed
	};

	HRESULT hr = m_pDxgiFactory->CreateSwapChainForHwnd(m_pDevice.Get(), GraphicsWindow::GetHwnd(), &swapchainDesc, &fullscreenDesc, nullptr, &m_pSwapchain);
	if( !SUCCEEDED(hr) )
	{
		// DXGI_SWAP_EFFECT_FLIP_DISCARD is supported starting in Win 10 so try again with an older mode in case it's not available
		swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		hr = m_pDxgiFactory->CreateSwapChainForHwnd(m_pDevice.Get(), GraphicsWindow::GetHwnd(), &swapchainDesc, &fullscreenDesc, nullptr, &m_pSwapchain);

		if( !SUCCEEDED(hr) )
		{
			LOG->Warn("RageDisplay_D3D11::TryVideoMode() failed to create swapchain with error %ld", hr);
			return "Swapchain creation failed";
		}
	}

	// TODO should I call IDXGIFactory::MakeWindowAssociation() here?
	ResolutionChanged();
	return RString(); // mode change successful
}

void RageDisplay_D3D11::ResolutionChanged()
{
	DXGI_SWAP_CHAIN_DESC swapchainDesc;
	// TODO does desc change on resolution change?
	HRESULT hr = m_pSwapchain->GetDesc(&swapchainDesc);
	ASSERT(SUCCEEDED(hr));

	const D3D11_TEXTURE2D_DESC depthStencilDesc = {
		swapchainDesc.BufferDesc.Width,
		swapchainDesc.BufferDesc.Height,
		1, // MipLevels
		1, // ArraySize
		DXGI_FORMAT_D32_FLOAT,
		{1, 0}, // DXGI_SAMPLE_DESC {Count, Quality}
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_DEPTH_STENCIL,
		0, // CPUAccessFlags
		0 // MiscFlags
	};

	Microsoft::WRL::ComPtr<ID3D11Texture2D> pDepthStencil;
	hr = m_pDevice->CreateTexture2D(&depthStencilDesc, nullptr, &pDepthStencil);
	ASSERT(SUCCEEDED(hr));

	hr = m_pDevice->CreateDepthStencilView(pDepthStencil.Get(), nullptr, &m_pDepthStencilView);
	ASSERT(SUCCEEDED(hr));

	m_iRenderTargetWidth = swapchainDesc.BufferDesc.Width;
	m_iRenderTargetHeight = swapchainDesc.BufferDesc.Height;

	RageDisplay::ResolutionChanged();
}

int RageDisplay_D3D11::GetMaxTextureSize() const
{
	return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

struct RageTexture_D3D11
{
	UINT m_iWidth;
	UINT m_iHeight;
	RagePixelFormat m_Pixfmt;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pSRV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_pRTV;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_pDSV; // If m_pTexture is a render target and a depth buffer was requested, this will be a DSV of the depth buffer (and not of m_pTexture)
};

bool RageDisplay_D3D11::BeginFrame()
{
	GraphicsWindow::Update();

	// It's a bit silly to always create a new RTV for each frame but it seems to be D3D11 standard practice
	HRESULT hr = m_pSwapchain->GetBuffer(0, IID_PPV_ARGS(&m_pRenderTarget));
	// TODO maybe I need to handle device lost here instead of asserting success?
	ASSERT(SUCCEEDED(hr));

	hr = m_pDevice->CreateRenderTargetView(m_pRenderTarget.Get(), nullptr, &m_pRenderTargetView);
	ASSERT(SUCCEEDED(hr));

	static constexpr const float fClearColor[4] = { 0.f, 0.f, 0.f, 1.f };
	m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView.Get(), fClearColor);
	m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);

	m_Viewport.Width = m_iRenderTargetWidth;
	m_Viewport.Height = m_iRenderTargetHeight;
	m_pDeviceContext->RSSetViewports(1, &m_Viewport);

	m_pDeviceContext->PSSetShader(m_pBuiltinPixelShader.Get(), nullptr, 0);

	m_pDeviceContext->OMSetRenderTargets(1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());
	m_pBoundDepthStencilView = m_pDepthStencilView;

	return RageDisplay::BeginFrame();
}

static RageTimer g_LastFrameEndedAt( RageZeroTimer );
void RageDisplay_D3D11::EndFrame()
{
	FrameLimitBeforeVsync( GetActualVideoModeParams().rate );
	// TODO how to handle DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING?
	m_pSwapchain->Present(GetActualVideoModeParams().vsync, 0);
	FrameLimitAfterVsync();

	RageDisplay::EndFrame();
}

bool RageDisplay_D3D11::SupportsTextureFormat( RagePixelFormat pixfmt, bool realtime )
{
	if( DXGI_FORMATS[pixfmt] == DXGI_FORMAT_UNKNOWN )
		return false;

	UINT formatSupport;
	HRESULT hr = m_pDevice->CheckFormatSupport(DXGI_FORMATS[pixfmt], &formatSupport);
	ASSERT(SUCCEEDED(hr));

	// TODO should we have a similar function but for render target? Currently we only test SupportsRenderToTexture() but maybe we should differentiate by format as well?
	return (formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D);
}

bool RageDisplay_D3D11::SupportsThreadedRendering()
{
	return true;
}

RageSurface* RageDisplay_D3D11::CreateScreenshot()
{
	RageSurface * result = nullptr;

	D3D11_TEXTURE2D_DESC renderTargetDesc;
	m_pRenderTarget->GetDesc(&renderTargetDesc);

	RagePixelFormat pf = RagePixelFormat_Invalid;
	for( int i = 0; i < NUM_RagePixelFormat; ++i ) {
		if( DXGI_FORMATS[i] == renderTargetDesc.Format ) {
			pf = static_cast<RagePixelFormat>(i);
			break;
		}
	}

	if( pf == RagePixelFormat_Invalid )
		return result;

	D3D11_TEXTURE2D_DESC surfaceCopyDesc = {
		renderTargetDesc.Width,
		renderTargetDesc.Height,
		1, // MipLevels
		1, // ArraySize
		renderTargetDesc.Format,
		{1, 0}, // DXGI_SAMPLE_DESC {Count, Quality}
		D3D11_USAGE_STAGING,
		0, // BindFlags
		D3D11_CPU_ACCESS_READ,
		0 // MiscFlags
	};

	Microsoft::WRL::ComPtr<ID3D11Texture2D> pSurfaceCopy;
	HRESULT hr = m_pDevice->CreateTexture2D(&surfaceCopyDesc, nullptr, &pSurfaceCopy);
	ASSERT(SUCCEEDED(hr));

	m_pDeviceContext->CopyResource(pSurfaceCopy.Get(), m_pRenderTarget.Get());

	D3D11_MAPPED_SUBRESOURCE mappedSubresource;
	hr = m_pDeviceContext->Map(
		pSurfaceCopy.Get(),
		0, // Subresource
		D3D11_MAP_READ,
		0, // MapFlags
		&mappedSubresource);
	ASSERT(SUCCEEDED(hr));

	RageSurface* surface = CreateSurfaceFromPixfmt(pf, mappedSubresource.pData, renderTargetDesc.Width, renderTargetDesc.Height, mappedSubresource.RowPitch);
	ASSERT(nullptr != surface);

	// We need to make a copy, since mappedSubresource.pData will go away when we call Unmap().
	result = CreateSurface(surface->w, surface->h,
		surface->format->BitsPerPixel,
		surface->format->Rmask, surface->format->Gmask,
		surface->format->Bmask, surface->format->Amask);
	RageSurfaceUtils::CopySurface(surface, result);
	delete surface;

	m_pDeviceContext->Unmap(
		pSurfaceCopy.Get(),
		0 // Subresource
	);

	return result;
}

ActualVideoModeParams RageDisplay_D3D11::GetActualVideoModeParams() const
{
	// TODO is this correct? perhaps things like refresh rate should be taken from swapchain?
	return GraphicsWindow::GetParams();
}

void RageDisplay_D3D11::UpdateTransforms()
{
	// TODO we don't have any nice way to check if the matrices have changed
	m_bConstantBufferVSChanged = true;
	m_bConstantBufferPSChanged = true;

	RageMatrix modelView;
	RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());

	std::memcpy(&m_ConstantBufferVS.vertexEyeTransform, &modelView, sizeof(m_ConstantBufferVS.vertexEyeTransform));

	// Clear out 4th row and column of the matrix to make the calculations approprate for transforming vectors
	RageMatrix temp;
	temp = modelView;
	temp(3, 0) = 0.f;
	temp(3, 1) = 0.f;
	temp(3, 2) = 0.f;
	temp(0, 3) = 0.f;
	temp(1, 3) = 0.f;
	temp(2, 3) = 0.f;
	temp(3, 3) = 1.f;

	// Move to DirectXMath
	DirectX::XMMATRIX normalTransformInverse;
	std::memcpy(&normalTransformInverse, &temp, sizeof(normalTransformInverse));

	// Calculate inverse and make this the normal transform
	DirectX::XMMATRIX normalTransform = DirectX::XMMatrixInverse(nullptr, normalTransformInverse);
	std::memcpy(&m_ConstantBufferVS.normalTransform, &normalTransform, sizeof(m_ConstantBufferVS.normalTransform));

	RageMatrix projection;
	RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());

	RageMatrix modelViewProjection;
	RageMatrixMultiply(&modelViewProjection, &projection, &modelView);

	std::memcpy(&m_ConstantBufferVS.vertexProjectionTransform, &modelViewProjection, sizeof(m_ConstantBufferVS.vertexProjectionTransform));
	std::memcpy(&m_ConstantBufferVS.texcoordTransform, GetTextureTop(), sizeof(m_ConstantBufferVS.texcoordTransform));
}

void RageDisplay_D3D11::BindRenderingState()
{
	UpdateTransforms();

	if (m_bRasterizerStateChanged)
	{
		m_bRasterizerStateChanged = false;

		Microsoft::WRL::ComPtr<ID3D11RasterizerState> pRasterizerState;
		HRESULT hr = m_pDevice->CreateRasterizerState(&m_RasterizerDesc, &pRasterizerState);
		ASSERT(SUCCEEDED(hr));

		m_pDeviceContext->RSSetState(pRasterizerState.Get());
	}

	if (m_bBlendStateChanged)
	{
		m_bBlendStateChanged = false;

		Microsoft::WRL::ComPtr<ID3D11BlendState> pBlendState;
		HRESULT hr = m_pDevice->CreateBlendState(&m_BlendDesc, &pBlendState);
		ASSERT(SUCCEEDED(hr));

		m_pDeviceContext->OMSetBlendState(pBlendState.Get(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	}

	if (m_bDepthStateChanged)
	{
		m_bDepthStateChanged = false;

		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> pDepthStencilState;
		HRESULT hr = m_pDevice->CreateDepthStencilState(&m_DepthStencilDesc, &pDepthStencilState);
		ASSERT(SUCCEEDED(hr));

		m_pDeviceContext->OMSetDepthStencilState(pDepthStencilState.Get(), D3D11_DEFAULT_STENCIL_REFERENCE);
	}

	if (m_bLightsChanged)
	{
		m_bLightsChanged = false;
		m_bConstantBufferVSChanged = true;

		if (m_bLightingEnabled)
		{
			std::uint32_t numLights = 0;

			for (unsigned i = 0; i < D3D11_MAX_LIGHTS; ++i)
			{
				if (m_bLightsEnabled[i])
					m_ConstantBufferVS.lights[numLights++] = m_Lights[i];
			}

			m_ConstantBufferVS.numLights = numLights + 1;
		}
		else
		{
			m_ConstantBufferVS.numLights = 0;
		}
	}

	if (m_bConstantBufferVSChanged)
	{
		m_bConstantBufferVSChanged = false;

		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		HRESULT hr = m_pDeviceContext->Map(m_pConstantBufferVS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
		ASSERT(SUCCEEDED(hr));

		std::memcpy(mappedSubresource.pData, &m_ConstantBufferVS, sizeof(m_ConstantBufferVS));
		m_pDeviceContext->Unmap(m_pConstantBufferVS.Get(), 0);
		m_pDeviceContext->VSSetConstantBuffers(0, 1, m_pConstantBufferVS.GetAddressOf());
	}

	if (m_bTexturesChanged)
	{
		m_bTexturesChanged = false;
		m_bConstantBufferPSChanged = true;

		m_ConstantBufferPS.numTextures = 0;
		m_ConstantBufferPS.textureModes = 0;

		ID3D11SamplerState* pSamplerStates[D3D11_MAX_TEXTURES];
		ID3D11ShaderResourceView* pSRVs[D3D11_MAX_TEXTURES];

		for (unsigned i = 0; i < D3D11_MAX_TEXTURES; ++i)
		{
			if (m_iTextures[i] != 0)
			{
				if (m_bSamplerStateChanged[i])
				{
					m_bSamplerStateChanged[i] = false;
					HRESULT hr = m_pDevice->CreateSamplerState(&m_SamplerStates[i], &m_pSamplerStates[i]);
					ASSERT(SUCCEEDED(hr));
				}

				m_ConstantBufferPS.textureModes |= m_TextureModes[i] << (m_ConstantBufferPS.numTextures * 3);
				m_ConstantBufferPS.textureModes |= m_bSphereMapping[i] << (m_ConstantBufferPS.numTextures * 3 + 2);
				pSamplerStates[m_ConstantBufferPS.numTextures] = m_pSamplerStates[i].Get();

				RageTexture_D3D11* pTex = reinterpret_cast<RageTexture_D3D11*>(m_iTextures[i]);
				pSRVs[m_ConstantBufferPS.numTextures++] = pTex->m_pSRV.Get();
			}
		}

		m_pDeviceContext->PSSetSamplers(0, m_ConstantBufferPS.numTextures, pSamplerStates);
		m_pDeviceContext->PSSetShaderResources(0, m_ConstantBufferPS.numTextures, pSRVs);
	}

	if (m_bConstantBufferPSChanged)
	{
		m_bConstantBufferPSChanged = false;

		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		HRESULT hr = m_pDeviceContext->Map(m_pConstantBufferPS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
		ASSERT(SUCCEEDED(hr));

		std::memcpy(mappedSubresource.pData, &m_ConstantBufferPS, sizeof(m_ConstantBufferPS));
		m_pDeviceContext->Unmap(m_pConstantBufferPS.Get(), 0);
		m_pDeviceContext->PSSetConstantBuffers(0, 1, m_pConstantBufferPS.GetAddressOf());
	}
}

void RageDisplay_D3D11::BindVertexBuffers( const RageSpriteVertex v[], int iNumVerts )
{
	m_pDeviceContext->IASetInputLayout(m_pSpriteInputLayout.Get());

	//TODO don't allocate a new buffer for each draw
	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.ByteWidth = iNumVerts * sizeof(RageSpriteVertex);
	bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA subresourceData;
	subresourceData.pSysMem = reinterpret_cast<const void*>(v);
	subresourceData.SysMemPitch = 0;
	subresourceData.SysMemSlicePitch = 0;

	Microsoft::WRL::ComPtr<ID3D11Buffer> pTempVertexBuffer;
	HRESULT hr = m_pDevice->CreateBuffer(&bufferDesc, &subresourceData, &pTempVertexBuffer);
	ASSERT(SUCCEEDED(hr));

	constexpr const UINT stride = sizeof(RageSpriteVertex);
	constexpr const UINT offset = 0;
	m_pDeviceContext->IASetVertexBuffers(0, 1, pTempVertexBuffer.GetAddressOf(), &stride, &offset);

	m_pDeviceContext->VSSetShader(m_pSpriteVertexShader.Get(), nullptr, 0);
}

class RageCompiledModelGeometryD3D11 : public RageCompiledModelGeometry
{
public:
	RageCompiledModelGeometryD3D11(ID3D11Device1* pDevice, ID3D11DeviceContext1* pDeviceContext) :
		m_pDevice(pDevice),
		m_pDeviceContext(pDeviceContext)
	{}

	void Allocate( const std::vector<msMesh> &vMeshes )
	{
		D3D11_BUFFER_DESC bufferDesc;
		// TODO do I need to make the size at least 1?
		bufferDesc.ByteWidth = GetTotalVertices() * sizeof(float) * 8;
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = 0;
		bufferDesc.StructureByteStride = 0;

		HRESULT hr = m_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_pVertexBuffer);
		ASSERT(SUCCEEDED(hr));

		if (m_bAnyNeedsTextureMatrixScale)
		{
			bufferDesc.ByteWidth = GetTotalVertices() * sizeof(float) * 2;
			hr = m_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_pVertexTextureScaleBuffer);
			ASSERT(SUCCEEDED(hr));
		}

		bufferDesc.ByteWidth = GetTotalTriangles() * sizeof(msTriangle);
		bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		hr = m_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_pIndexBuffer);
		ASSERT(SUCCEEDED(hr));
	}

	void Change( const std::vector<msMesh> &vMeshes )
	{
		std::vector<float> vVertexBuffer(GetTotalVertices() * 8), vVertexTextureScaleBuffer;
		std::vector<std::uint16_t> vIndexBuffer(GetTotalTriangles() * 3);
		if (m_bAnyNeedsTextureMatrixScale)
			vVertexTextureScaleBuffer.resize(GetTotalVertices() * 2);

		for (std::size_t i = 0; i < vMeshes.size(); i++)
		{
			const MeshInfo& meshInfo = m_vMeshInfo[i];
			const msMesh& mesh = vMeshes[i];

			for (std::size_t j = 0; j < mesh.Vertices.size(); j++)
			{
				vVertexBuffer[(meshInfo.iVertexStart + j) * 8    ] = mesh.Vertices[j].p.x;
				vVertexBuffer[(meshInfo.iVertexStart + j) * 8 + 1] = mesh.Vertices[j].p.y;
				vVertexBuffer[(meshInfo.iVertexStart + j) * 8 + 2] = mesh.Vertices[j].p.z;
				vVertexBuffer[(meshInfo.iVertexStart + j) * 8 + 3] = mesh.Vertices[j].n.x;
				vVertexBuffer[(meshInfo.iVertexStart + j) * 8 + 4] = mesh.Vertices[j].n.y;
				vVertexBuffer[(meshInfo.iVertexStart + j) * 8 + 5] = mesh.Vertices[j].n.z;
				vVertexBuffer[(meshInfo.iVertexStart + j) * 8 + 6] = mesh.Vertices[j].t.x;
				vVertexBuffer[(meshInfo.iVertexStart + j) * 8 + 7] = mesh.Vertices[j].t.y;
			}

			if (m_bAnyNeedsTextureMatrixScale)
			{
				for (std::size_t j = 0; j < mesh.Vertices.size(); j++)
				{
					vVertexTextureScaleBuffer[(meshInfo.iVertexStart + j) * 2    ] = mesh.Vertices[j].TextureMatrixScale.x;
					vVertexTextureScaleBuffer[(meshInfo.iVertexStart + j) * 2 + 1] = mesh.Vertices[j].TextureMatrixScale.y;
				}
			}

			for (std::size_t j = 0; j < mesh.Triangles.size(); j++)
				for (std::size_t k = 0; k < 3; k++)
					vIndexBuffer[(meshInfo.iTriangleStart + j) * 3 + k] = static_cast<std::uint16_t>(meshInfo.iVertexStart) + mesh.Triangles[j].nVertexIndices[k];
		}

		m_pDeviceContext->UpdateSubresource1(m_pVertexBuffer.Get(), 0, nullptr, reinterpret_cast<const void*>(vVertexBuffer.data()), 0, 0, D3D11_COPY_DISCARD);
		m_pDeviceContext->UpdateSubresource1(m_pIndexBuffer.Get(), 0, nullptr, reinterpret_cast<const void*>(vIndexBuffer.data()), 0, 0, D3D11_COPY_DISCARD);
		if (m_bAnyNeedsTextureMatrixScale)
			m_pDeviceContext->UpdateSubresource1(m_pVertexTextureScaleBuffer.Get(), 0, nullptr, reinterpret_cast<const void*>(vVertexTextureScaleBuffer.data()), 0, 0, D3D11_COPY_DISCARD);
	}

	void Draw( int iMeshIndex ) const
	{
		const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];

		m_pDeviceContext->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
		m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D11Buffer* vertexBuffers[2] = {m_pVertexBuffer.Get(), m_pVertexTextureScaleBuffer.Get()};
		constexpr const UINT strides[2] = {offsetof(RageModelVertex, bone), sizeof(RageVector2)};
		constexpr const UINT offsets[2] = {0, 0};
		m_pDeviceContext->IASetVertexBuffers(0, m_bAnyNeedsTextureMatrixScale ? 2 : 1, vertexBuffers, strides, offsets);

		m_pDeviceContext->DrawIndexed(meshInfo.iTriangleCount * 3, meshInfo.iTriangleStart * 3, 0);
	}

protected:
	Microsoft::WRL::ComPtr<ID3D11Device1> m_pDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_pDeviceContext;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pVertexTextureScaleBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pIndexBuffer;
};

RageCompiledModelGeometry* RageDisplay_D3D11::CreateCompiledModelGeometry()
{
	return new RageCompiledModelGeometryD3D11(m_pDevice.Get(), m_pDeviceContext.Get());
}

void RageDisplay_D3D11::DrawQuadsInternal( const RageSpriteVertex v[], int iNumVerts )
{
	// there isn't a quad primitive in D3D11, so we have to fake it with indexed triangles
	int iNumQuads = iNumVerts / 4;
	int iNumTriangles = iNumQuads * 2;
	int iNumNewVerts = iNumTriangles * 3;
	std::vector<std::uint16_t> vTempIndexBuffer(iNumNewVerts);
	for (int i = 0; i < iNumQuads; ++i)
	{
		vTempIndexBuffer[i * 6    ] = i * 4;
		vTempIndexBuffer[i * 6 + 1] = i * 4 + 1;
		vTempIndexBuffer[i * 6 + 2] = i * 4 + 2;
		vTempIndexBuffer[i * 6 + 3] = i * 4 + 2;
		vTempIndexBuffer[i * 6 + 4] = i * 4 + 3;
		vTempIndexBuffer[i * 6 + 5] = i * 4;
	}

	//TODO don't allocate a new buffer for each draw
	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.ByteWidth = iNumNewVerts * sizeof(std::uint16_t);
	bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA subresourceData;
	subresourceData.pSysMem = reinterpret_cast<const void*>(vTempIndexBuffer.data());
	subresourceData.SysMemPitch = 0;
	subresourceData.SysMemSlicePitch = 0;

	Microsoft::WRL::ComPtr<ID3D11Buffer> pTempIndexBuffer;
	HRESULT hr = m_pDevice->CreateBuffer(&bufferDesc, &subresourceData, &pTempIndexBuffer);
	ASSERT(SUCCEEDED(hr));

	m_pDeviceContext->IASetIndexBuffer(pTempIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	BindVertexBuffers(v, iNumVerts);
	BindRenderingState();

	m_pDeviceContext->DrawIndexed(iNumNewVerts, 0, 0);
}

void RageDisplay_D3D11::DrawQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	// there isn't a quad strip primitive in D3D11, so we have to fake it
	// but it seems that quad strip is pretty much identical to triangle strip
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	BindVertexBuffers(v, iNumVerts);
	BindRenderingState();

	m_pDeviceContext->Draw(iNumVerts, 0);
}

void RageDisplay_D3D11::DrawSymmetricQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	// there isn't a quad strip primitive in D3D11, so we have to fake it with indexed triangles
	int iNumQuadsHalf = (iNumVerts - 3) / 3;
	int iNumTriangles = iNumQuadsHalf * 4;
	int iNumNewVerts = iNumTriangles * 3;
	std::vector<std::uint16_t> vTempIndexBuffer(iNumNewVerts);
	for (int i = 0; i < iNumQuadsHalf; ++i)
	{
		// { 1, 3, 0 } { 1, 4, 3 } { 1, 5, 4 } { 1, 2, 5 }
		vTempIndexBuffer[i * 12     ] = i * 3 + 1;
		vTempIndexBuffer[i * 12 +  1] = i * 3 + 3;
		vTempIndexBuffer[i * 12 +  2] = i * 3 + 0;
		vTempIndexBuffer[i * 12 +  3] = i * 3 + 1;
		vTempIndexBuffer[i * 12 +  4] = i * 3 + 4;
		vTempIndexBuffer[i * 12 +  5] = i * 3 + 3;
		vTempIndexBuffer[i * 12 +  6] = i * 3 + 1;
		vTempIndexBuffer[i * 12 +  7] = i * 3 + 5;
		vTempIndexBuffer[i * 12 +  8] = i * 3 + 4;
		vTempIndexBuffer[i * 12 +  9] = i * 3 + 1;
		vTempIndexBuffer[i * 12 + 10] = i * 3 + 2;
		vTempIndexBuffer[i * 12 + 11] = i * 3 + 5;
	}

	//TODO don't allocate a new buffer for each draw
	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.ByteWidth = iNumNewVerts * sizeof(std::uint16_t);
	bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA subresourceData;
	subresourceData.pSysMem = reinterpret_cast<const void*>(vTempIndexBuffer.data());
	subresourceData.SysMemPitch = 0;
	subresourceData.SysMemSlicePitch = 0;

	Microsoft::WRL::ComPtr<ID3D11Buffer> pTempIndexBuffer;
	HRESULT hr = m_pDevice->CreateBuffer(&bufferDesc, &subresourceData, &pTempIndexBuffer);
	ASSERT(SUCCEEDED(hr));

	m_pDeviceContext->IASetIndexBuffer(pTempIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	BindVertexBuffers(v, iNumVerts);
	BindRenderingState();

	m_pDeviceContext->DrawIndexed(iNumNewVerts, 0, 0);
}

void RageDisplay_D3D11::DrawFanInternal( const RageSpriteVertex v[], int iNumVerts )
{
	// there isn't a quad strip primitive in D3D11, so we have to fake it with indexed triangles
	int iNumTriangles = iNumVerts - 2;
	int iNumNewVerts = iNumTriangles * 3;
	std::vector<std::uint16_t> vTempIndexBuffer(iNumNewVerts);
	for (int i = 0; i < iNumTriangles; ++i)
	{
		vTempIndexBuffer[i * 3    ] = 0;
		vTempIndexBuffer[i * 3 + 1] = i + 1;
		vTempIndexBuffer[i * 3 + 2] = i + 2;
	}

	//TODO don't allocate a new buffer for each draw
	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.ByteWidth = iNumNewVerts * sizeof(std::uint16_t);
	bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA subresourceData;
	subresourceData.pSysMem = reinterpret_cast<const void*>(vTempIndexBuffer.data());
	subresourceData.SysMemPitch = 0;
	subresourceData.SysMemSlicePitch = 0;

	Microsoft::WRL::ComPtr<ID3D11Buffer> pTempIndexBuffer;
	HRESULT hr = m_pDevice->CreateBuffer(&bufferDesc, &subresourceData, &pTempIndexBuffer);
	ASSERT(SUCCEEDED(hr));

	m_pDeviceContext->IASetIndexBuffer(pTempIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	BindVertexBuffers(v, iNumVerts);
	BindRenderingState();

	m_pDeviceContext->DrawIndexed(iNumNewVerts, 0, 0);
}

void RageDisplay_D3D11::DrawStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	BindVertexBuffers(v, iNumVerts);
	BindRenderingState();

	m_pDeviceContext->Draw(iNumVerts, 0);
}

void RageDisplay_D3D11::DrawTrianglesInternal( const RageSpriteVertex v[], int iNumVerts )
{
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	BindVertexBuffers(v, iNumVerts);
	BindRenderingState();

	m_pDeviceContext->Draw(iNumVerts, 0);
}

void RageDisplay_D3D11::DrawCompiledModelGeometryInternal( const RageCompiledModelGeometry *p, int iMeshIndex )
{
	if (p->NeedsTextureMatrixScale())
	{
		m_pDeviceContext->IASetInputLayout(m_pModelTextureMatrixScaleInputLayout.Get());
		m_pDeviceContext->VSSetShader(m_pModelTextureMatrixScaleVertexShader.Get(), nullptr, 0);
	}
	else
	{
		m_pDeviceContext->IASetInputLayout(m_pModelInputLayout.Get());
		m_pDeviceContext->VSSetShader(m_pModelVertexShader.Get(), nullptr, 0);
	}

	BindRenderingState();
	p->Draw( iMeshIndex );
}

void RageDisplay_D3D11::DrawLineStripInternal( const RageSpriteVertex v[], int iNumVerts, float LineWidth )
{
	// D3D11 doesn't support lines with width other than 1
	if (LineWidth != 1.f)
		return RageDisplay::DrawLineStripInternal(v, iNumVerts, LineWidth);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);

	BindVertexBuffers(v, iNumVerts);
	BindRenderingState();

	m_pDeviceContext->Draw(iNumVerts, 0);

	StatsAddVerts( iNumVerts );
}

void RageDisplay_D3D11::ClearAllTextures()
{
	FOREACH_ENUM( TextureUnit, i )
		SetTexture( i, 0 );
}

int RageDisplay_D3D11::GetNumTextureUnits()
{
	return D3D11_MAX_TEXTURES;
}

void RageDisplay_D3D11::SetTexture( TextureUnit tu, std::uintptr_t iTexture )
{
	unsigned int idx = static_cast<unsigned int>(tu);
	if( idx >= D3D11_MAX_TEXTURES )	// not supported
		return;

	if( m_iTextures[idx] != iTexture)
	{
		m_bTexturesChanged = true;
		m_iTextures[idx] = iTexture;
	}
}

void RageDisplay_D3D11::SetTextureMode( TextureUnit tu, TextureMode tm )
{
	unsigned int idx = static_cast<unsigned int>(tu);
	if( idx >= D3D11_MAX_TEXTURES )	// not supported
		return;

	ASSERT_M(tm < NUM_TextureMode, ssprintf("Invalid TextureMode: %i", tm));

	if( m_TextureModes[idx] != tm )
	{
		m_bTexturesChanged = true;
		m_TextureModes[idx] = tm;
	}
}

void RageDisplay_D3D11::SetTextureFiltering( TextureUnit tu, bool b )
{
	unsigned int idx = static_cast<unsigned int>(tu);
	if( idx >= D3D11_MAX_TEXTURES )	// not supported
		return;

	// TODO maybe should be D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT?
	D3D11_FILTER filter = b ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
	if( m_SamplerStates[idx].Filter != filter )
	{
		m_bTexturesChanged = true;
		m_bSamplerStateChanged[idx] = true;
		m_SamplerStates[idx].Filter = filter;
	}
}

void RageDisplay_D3D11::SetEffectMode(EffectMode effect)
{
	// TODO
}

bool RageDisplay_D3D11::IsEffectModeSupported(EffectMode effect)
{
	// TODO
	return false;
}

void RageDisplay_D3D11::SetBlendMode( BlendMode mode )
{
	if (mode == m_CurrentBlendMode)
		return;

	m_CurrentBlendMode = mode;
	m_bBlendStateChanged = true;
	m_BlendDesc.RenderTarget[0].BlendEnable = TRUE;

	switch (mode)
	{
	case BLEND_NORMAL:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	case BLEND_ADD:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	case BLEND_SUBTRACT:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_REV_SUBTRACT;
		break;
	case BLEND_MODULATE:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	case BLEND_COPY_SRC:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	case BLEND_ALPHA_MASK:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	case BLEND_ALPHA_KNOCK_OUT:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	case BLEND_ALPHA_MULTIPLY:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	case BLEND_WEIGHTED_MULTIPLY:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	case BLEND_INVERT_DEST:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_SUBTRACT;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_SUBTRACT;
		break;
	case BLEND_NO_EFFECT:
		m_BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		m_BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		m_BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;
	default:
		FAIL_M(ssprintf("Invalid BlendMode: %i", mode));
	}
}

bool RageDisplay_D3D11::IsZWriteEnabled() const
{
	return m_DepthStencilDesc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL;
}

void RageDisplay_D3D11::SetZBias( float f )
{
	m_Viewport.MinDepth = SCALE( f, 0.0f, 1.0f, 0.05f, 0.0f );
	m_Viewport.MaxDepth = SCALE( f, 0.0f, 1.0f, 1.0f, 0.95f );
	m_pDeviceContext->RSSetViewports(1, &m_Viewport);
}

bool RageDisplay_D3D11::IsZTestEnabled() const
{
	return m_DepthStencilDesc.DepthEnable == TRUE && m_DepthStencilDesc.DepthFunc != D3D11_COMPARISON_ALWAYS;
}

void RageDisplay_D3D11::SetZWrite( bool b )
{
	if (b == IsZWriteEnabled())
		return;

	m_bDepthStateChanged = true;
	m_DepthStencilDesc.DepthWriteMask = b ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
}

void RageDisplay_D3D11::SetZTestMode( ZTestMode mode )
{
	if (m_DepthStencilDesc.DepthEnable != TRUE)
	{
		m_bDepthStateChanged = true;
		m_DepthStencilDesc.DepthEnable = TRUE;
	}

	D3D11_COMPARISON_FUNC depthFunc;
	switch (mode)
	{
	case ZTEST_OFF:
		depthFunc = D3D11_COMPARISON_ALWAYS;
		break;
	case ZTEST_WRITE_ON_PASS:
		depthFunc = D3D11_COMPARISON_LESS_EQUAL;
		break;
	case ZTEST_WRITE_ON_FAIL:
		depthFunc = D3D11_COMPARISON_GREATER;
		break;
	default:
		FAIL_M(ssprintf("Invalid ZTestMode: %i", mode));
	}

	if (m_DepthStencilDesc.DepthFunc != depthFunc)
	{
		m_bDepthStateChanged = true;
		m_DepthStencilDesc.DepthFunc = depthFunc;
	}
}

void RageDisplay_D3D11::ClearZBuffer()
{
	if (m_pBoundDepthStencilView)
		m_pDeviceContext->ClearDepthStencilView(m_pBoundDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
}

void RageDisplay_D3D11::SetTextureWrapping( TextureUnit tu, bool b )
{
	unsigned int idx = static_cast<unsigned int>(tu);
	if( idx >= D3D11_MAX_TEXTURES )	// not supported
		return;

	D3D11_TEXTURE_ADDRESS_MODE mode = b ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
	if( m_SamplerStates[idx].AddressU != mode )
	{
		m_bTexturesChanged = true;
		m_bSamplerStateChanged[idx] = true;
		m_SamplerStates[idx].AddressU = mode;
		m_SamplerStates[idx].AddressV = mode;
	}
}

void RageDisplay_D3D11::SetMaterial(
	const RageColor &emissive,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	float shininess
	)
{
	m_bConstantBufferVSChanged = true;

	/* If lighting is off, then the current material will have no effect.
	 * We want to still be able to color models with lighting off, so shove the
	 * material color in texture factor and modify the texture stage to use it
	 * instead of the vertex color (our models don't have vertex coloring anyway). */
	if( m_bLightingEnabled )
	{
		std::memcpy( &m_ConstantBufferVS.materialDiffuse, diffuse, sizeof(m_ConstantBufferVS.materialDiffuse) );
		std::memcpy( &m_ConstantBufferVS.materialAmbient, ambient, sizeof(m_ConstantBufferVS.materialAmbient) );
		std::memcpy( &m_ConstantBufferVS.materialSpecular, specular, sizeof(m_ConstantBufferVS.materialSpecular) );
		std::memcpy( &m_ConstantBufferVS.materialEmission, emissive, sizeof(m_ConstantBufferVS.materialEmission) );
		m_ConstantBufferVS.materialShininess = shininess;
	}
	else
	{
		RageColor c;
		// Seems that a lot of the times the sum will exceed 1 so we should clamp to get more correct colors
		// In fact D3D renderer will always clamp the values
		// But OpenGL will not clamp but then the values seem to be clamped somewhere in the runtime anyway
		// TODO - figure out the right way to do this
		c.r = clamp(diffuse.r + emissive.r + ambient.r, 0.f, 1.f);
		c.g = clamp(diffuse.g + emissive.g + ambient.g, 0.f, 1.f);
		c.b = clamp(diffuse.b + emissive.b + ambient.b, 0.f, 1.f);
		c.a = clamp(diffuse.a, 0.f, 1.f);
		std::memcpy( &m_ConstantBufferVS.defaultVertexColor, &c, sizeof(m_ConstantBufferVS.defaultVertexColor) );
	}
}

void RageDisplay_D3D11::SetLighting( bool b )
{
	if (m_bLightingEnabled != b)
	{
		m_bLightsChanged = true;
		m_bLightingEnabled = b;
	}
}

void RageDisplay_D3D11::SetLightOff( int index )
{
	if( m_bLightsEnabled[index] )
	{
		m_bLightsChanged = true;
		m_bLightsEnabled[index] = false;
	}
}

void RageDisplay_D3D11::SetLightDirectional(
	int index,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	const RageVector3 &dir )
{
	m_bLightsChanged = true;
	m_bLightsEnabled[index] = true;
	std::memcpy( &m_Lights[index].ambient, ambient, sizeof(ambient) );
	std::memcpy( &m_Lights[index].diffuse, diffuse, sizeof(diffuse) );
	std::memcpy( &m_Lights[index].specular, specular, sizeof(specular) );
	std::memcpy( &m_Lights[index].direction, dir, sizeof(dir) );

#if 0
	// TODO Do I need to flip Z like in D3D9???
	/* Z for lighting is flipped for D3D compared to OpenGL.
	 * XXX: figure out exactly why this is needed. Our transforms are probably
	 * goofed up, but the Z test is the same for both API's, so I'm not sure
	 * why we don't see other weirdness. -Chris */
	float position[] = { dir.x, dir.y, -dir.z };
#endif
}

void RageDisplay_D3D11::SetCullMode( CullMode mode )
{
	D3D11_CULL_MODE cullMode;
	switch( mode )
	{
	case CULL_BACK:
		cullMode = D3D11_CULL_BACK;
		break;
	case CULL_FRONT:
		cullMode = D3D11_CULL_FRONT;
		break;
	case CULL_NONE:
		cullMode = D3D11_CULL_NONE;
		break;
	default:
		FAIL_M(ssprintf("Invalid CullMode: %i", mode));
	}

	if (m_RasterizerDesc.CullMode != cullMode)
	{
		m_bRasterizerStateChanged = true;
		m_RasterizerDesc.CullMode = cullMode;
		m_RasterizerDesc.FrontCounterClockwise = TRUE; // Use OpenGL convention for culling
	}
}

std::uintptr_t RageDisplay_D3D11::CreateTexture(
	RagePixelFormat pixfmt,
	RageSurface* img,
	bool bGenerateMipMaps,
	ResourceUsagePattern usagePattern )
{
	// Since we are using D3D11_USAGE_DYNAMIC for ResourceUsagePattern::UPDATED_OFTEN D3D11 won't support mipmaps in that case, but that's probably fine
	ASSERT(!bGenerateMipMaps || usagePattern != ResourceUsagePattern::UPDATED_OFTEN);

	D3D11_TEXTURE2D_DESC textureDesc;
	textureDesc.Width = img->w;
	textureDesc.Height = img->h;
	textureDesc.MipLevels = bGenerateMipMaps ? 0 : 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMATS[pixfmt];
	textureDesc.SampleDesc = { 1, 0 };
	textureDesc.Usage = usagePattern == ResourceUsagePattern::UPDATED_RARELY ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (bGenerateMipMaps ? D3D11_BIND_RENDER_TARGET: 0);
	textureDesc.CPUAccessFlags = usagePattern == ResourceUsagePattern::UPDATED_RARELY ? 0 : D3D11_CPU_ACCESS_WRITE;
	textureDesc.MiscFlags = bGenerateMipMaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> pTexture;
	HRESULT hr = m_pDevice->CreateTexture2D(&textureDesc, nullptr, &pTexture);
	ASSERT(SUCCEEDED(hr));

	// TODO do we really need 2 separate paths here to update the texture?
	// TODO this whole block is pretty much identical to RageDisplay_D3D11::UpdateTexture() (except for offset)
	// TODO does it matter if we update the texture here or just give initial data to CreateTexture2D()?
	const RagePixelFormatDesc& desc = PIXEL_FORMAT_DESC[pixfmt];
	if (usagePattern == ResourceUsagePattern::UPDATED_RARELY)
	{
		RageSurface* pSurface;
		if(!RageSurfaceUtils::ConvertSurface(img, pSurface, textureDesc.Width, textureDesc.Height, desc.bpp, desc.masks[0], desc.masks[1], desc.masks[2], desc.masks[3]))
			pSurface = img;

		m_pDeviceContext->UpdateSubresource1(pTexture.Get(), 0, nullptr, reinterpret_cast<const void*>(pSurface->pixels), pSurface->pitch, 0, D3D11_COPY_DISCARD);
		if(pSurface != img)
			delete pSurface;
	}
	else
	{
		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		hr = m_pDeviceContext->Map(pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
		ASSERT(SUCCEEDED(hr));

		RageSurface* pSurface = CreateSurfaceFrom(textureDesc.Width, textureDesc.Height, desc.bpp, desc.masks[0], desc.masks[1], desc.masks[2], desc.masks[3], reinterpret_cast<std::uint8_t*>(mappedSubresource.pData), mappedSubresource.RowPitch);
		RageSurfaceUtils::Blit(img, pSurface);
		delete pSurface;

		m_pDeviceContext->Unmap(pTexture.Get(), 0);
	}

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pSRV;
	hr = m_pDevice->CreateShaderResourceView(pTexture.Get(), nullptr, &pSRV);
	ASSERT(SUCCEEDED(hr));

	if (bGenerateMipMaps)
		m_pDeviceContext->GenerateMips(pSRV.Get());

	RageTexture_D3D11* pTex = new RageTexture_D3D11{static_cast<UINT>(img->w), static_cast<UINT>(img->h), pixfmt, std::move(pTexture), std::move(pSRV)};
	return reinterpret_cast<std::uintptr_t>(pTex);
}

// TODO - remove xoffset, yoffset, width and height parameters since this function is only used for updating the whole texture anyway
void RageDisplay_D3D11::UpdateTexture(
	std::uintptr_t uTexHandle,
	RageSurface* img )
{
	RageTexture_D3D11* pTex = reinterpret_cast<RageTexture_D3D11*>(uTexHandle);

	D3D11_TEXTURE2D_DESC textureDesc;
	pTex->m_pTexture->GetDesc(&textureDesc);

	// TODO do we really need 2 separate paths here to update the texture?
	// TODO this whole block is pretty much identical to RageDisplay_D3D11::CreateTexture() (except for offset)
	const RagePixelFormatDesc& desc = PIXEL_FORMAT_DESC[pTex->m_Pixfmt];
	if (textureDesc.Usage == D3D11_USAGE_DEFAULT)
	{
		RageSurface* pSurface;
		if (!RageSurfaceUtils::ConvertSurface(img, pSurface, textureDesc.Width, textureDesc.Height, desc.bpp, desc.masks[0], desc.masks[1], desc.masks[2], desc.masks[3]))
			pSurface = img;

		// TODO - this will fail if we're updating just a part of a resource but this function is only ever used by MoveTexture and it always updates the whole texture
		m_pDeviceContext->UpdateSubresource1(pTex->m_pTexture.Get(), 0, nullptr, reinterpret_cast<const void*>(pSurface->pixels), pSurface->pitch, 0, D3D11_COPY_DISCARD);
		if (pSurface != img)
			delete pSurface;
	}
	else
	{
		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		HRESULT hr = m_pDeviceContext->Map(pTex->m_pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
		ASSERT(SUCCEEDED(hr));

		RageSurface* pSurface = CreateSurfaceFrom(textureDesc.Width, textureDesc.Height, desc.bpp, desc.masks[0], desc.masks[1], desc.masks[2], desc.masks[3], reinterpret_cast<std::uint8_t*>(mappedSubresource.pData), mappedSubresource.RowPitch);
		RageSurfaceUtils::Blit(img, pSurface);
		delete pSurface;
	}

	m_pDeviceContext->Unmap(pTex->m_pTexture.Get(), 0);
}

void RageDisplay_D3D11::DeleteTexture(std::uintptr_t iTexHandle)
{
	RageTexture_D3D11* pTex = reinterpret_cast<RageTexture_D3D11*>(iTexHandle);

	if (pTex == nullptr)
		return;

	delete pTex;
}

std::uintptr_t RageDisplay_D3D11::CreateRenderTarget(const RenderTargetParam& param, int& iTextureWidthOut, int& iTextureHeightOut)
{
	iTextureWidthOut = param.iWidth;
	iTextureHeightOut = param.iHeight;

	D3D11_TEXTURE2D_DESC textureDesc;
	textureDesc.Width = param.iWidth;
	textureDesc.Height = param.iHeight;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = param.bFloat ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.SampleDesc = { 1, 0 };
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> pTexture;
	HRESULT hr = m_pDevice->CreateTexture2D(&textureDesc, nullptr, &pTexture);
	ASSERT(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pSRV;
	hr = m_pDevice->CreateShaderResourceView(pTexture.Get(), nullptr, &pSRV);
	ASSERT(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pRTV;
	hr = m_pDevice->CreateRenderTargetView(pTexture.Get(), nullptr, &pRTV);
	ASSERT(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> pDSV;
	if (param.bWithDepthBuffer)
	{
		textureDesc.Format = DXGI_FORMAT_D16_UNORM;
		textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> pDepthTexture;
		hr = m_pDevice->CreateTexture2D(&textureDesc, nullptr, &pTexture);
		ASSERT(SUCCEEDED(hr));

		hr = m_pDevice->CreateDepthStencilView(pDepthTexture.Get(), nullptr, &pDSV);
		ASSERT(SUCCEEDED(hr));
	}

	// TODO probably doesn't matter that for the render targer we don't have a valid RagePixelFormat but please check
	// Looks like the format is only used for UpdateTexture or RageTextureLock but both of those are not allowed for the render target
	RageTexture_D3D11* pTex = new RageTexture_D3D11{static_cast<UINT>(param.iWidth), static_cast<UINT>(param.iHeight), RagePixelFormat_Invalid, std::move(pTexture), std::move(pSRV), std::move(pRTV), std::move(pDSV)};
	return reinterpret_cast<std::uintptr_t>(pTex);
}

std::uintptr_t RageDisplay_D3D11::GetRenderTarget()
{
	return reinterpret_cast<std::uintptr_t>(m_pCurrentRenderTarget);
}

void RageDisplay_D3D11::SetRenderTarget(std::uintptr_t iHandle, bool bPreserveTexture)
{
	RageTexture_D3D11* pTex = reinterpret_cast<RageTexture_D3D11*>(iHandle);

	if (pTex == nullptr)
	{
		/* Pop matrixes affected by SetDefaultRenderStates, undoing the push below. */
		/* XXX: This will break if for example we call SetRenderTarget(0, ...) twice in a row, but that's how it's alwayhs been, even in the OpenGL renderer */
		DISPLAY->CameraPopMatrix();

		m_Viewport.Width = m_iRenderTargetWidth;
		m_Viewport.Height = m_iRenderTargetHeight;
		m_pDeviceContext->RSSetViewports(1, &m_Viewport);

		m_pDeviceContext->OMSetRenderTargets(1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());
		m_pBoundDepthStencilView = m_pDepthStencilView;
	}
	else
	{
		/* For compatibility with OpenGL renderer, set appropriate rendering state here. Push matrixes affected by SetDefaultRenderStates. */
		DISPLAY->CameraPushMatrix();
		SetDefaultRenderStates();
		SetZWrite(true);

		if (!bPreserveTexture)
		{
			static constexpr const float fClearColor[4] = { 0.f, 0.f, 0.f, 1.f };
			m_pDeviceContext->ClearRenderTargetView(pTex->m_pRTV.Get(), fClearColor);
			if (pTex->m_pDSV)
				m_pDeviceContext->ClearDepthStencilView(pTex->m_pDSV.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
		}

		m_Viewport.Width = pTex->m_iWidth;
		m_Viewport.Height = pTex->m_iHeight;
		m_pDeviceContext->RSSetViewports(1, &m_Viewport);

		m_pDeviceContext->OMSetRenderTargets(1, pTex->m_pRTV.GetAddressOf(), pTex->m_pDSV.Get());
		m_pBoundDepthStencilView = pTex->m_pDSV;
	}
}

struct RageTextureLock_D3D11 : public RageTextureLock
{
	RageTextureLock_D3D11(ID3D11DeviceContext* pDeviceContext)
		: m_pDeviceContext(pDeviceContext)
	{
	}

	// TODO For performance reasons, the format of RageSurface must match the format of the texture
	// So it would probably be a good reason to not take RageSurface as argument but rather return a surface with appropriate format from here
	// But the problem is that this class is only ever used by MovieTexture_Generic and it also needs a surface format compatible with the decoder, so as it is now we could run into a format mismatch
	void Lock(std::uintptr_t iTexHandle, RageSurface* pSurface)
	{
		ASSERT(m_pTexture.Get() == nullptr);
		ASSERT(pSurface->pixels == nullptr);

		RageTexture_D3D11* pTex = reinterpret_cast<RageTexture_D3D11*>(iTexHandle);

		// TODO in order to do the map, the texture must be D3D11_USAGE_DYNAMIC, we need to guarantee this somehow
		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		HRESULT hr = m_pDeviceContext->Map(m_pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
		ASSERT(SUCCEEDED(hr));

		D3D11_TEXTURE2D_DESC textureDesc;
		pTex->m_pTexture->GetDesc(&textureDesc);
		ASSERT(static_cast<UINT>(pSurface->w) == textureDesc.Width);
		ASSERT(static_cast<UINT>(pSurface->h) == textureDesc.Height);
		ASSERT(static_cast<UINT>(pSurface->pitch) == mappedSubresource.RowPitch);

		const RageDisplay::RagePixelFormatDesc& desc = PIXEL_FORMAT_DESC[pTex->m_Pixfmt];
		ASSERT(desc.bpp == pSurface->fmt.BitsPerPixel);
		ASSERT(desc.masks[0] == pSurface->fmt.Rmask);
		ASSERT(desc.masks[1] == pSurface->fmt.Gmask);
		ASSERT(desc.masks[2] == pSurface->fmt.Bmask);
		ASSERT(desc.masks[3] == pSurface->fmt.Amask);

		pSurface->pixels = reinterpret_cast<std::uint8_t*>(mappedSubresource.pData);
		pSurface->pixels_owned = false;
	}

	void Unlock(RageSurface* pSurface, bool bChanged)
	{
		m_pDeviceContext->Unmap(m_pTexture.Get(), 0);

		m_pTexture.Reset();
		pSurface->pixels = nullptr;
	}

private:
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_pDeviceContext;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pTexture;
};

RageTextureLock* RageDisplay_D3D11::CreateTextureLock()
{
	return new RageTextureLock_D3D11(m_pDeviceContext.Get());
}

void RageDisplay_D3D11::SetAlphaTest( bool b )
{
	if( m_ConstantBufferPS.bAlphaTestEnabled != b )
	{
		m_bConstantBufferPSChanged = true;
		m_ConstantBufferPS.bAlphaTestEnabled = b;
	}
}

RageMatrix RageDisplay_D3D11::GetOrthoMatrix( float l, float r, float b, float t, float zn, float zf )
{
	RageMatrix m = RageDisplay::GetOrthoMatrix( l, r, b, t, zn, zf );

	// Convert from OpenGL's [-1,+1] Z values to D3D's [0,+1].
	RageMatrix tmp;
	RageMatrixScaling( &tmp, 1, 1, 0.5f );
	RageMatrixMultiply( &m, &tmp, &m );

	RageMatrixTranslation( &tmp, 0, 0, 0.5f );
	RageMatrixMultiply( &m, &tmp, &m );

	return m;
}

RageMatrix RageDisplay_D3D11::GetFrustumMatrix( float l, float r, float b, float t, float zn, float zf )
{
	RageMatrix m = RageDisplay::GetFrustumMatrix( l, r, b, t, zn, zf );

	// Convert from OpenGL's [-1,+1] Z values to D3D's [0,+1].
	RageMatrix tmp;
	RageMatrixScaling( &tmp, 1, 1, 0.5f );
	RageMatrixMultiply( &m, &tmp, &m );

	RageMatrixTranslation( &tmp, 0, 0, 0.5f );
	RageMatrixMultiply( &m, &tmp, &m );

	return m;
}

void RageDisplay_D3D11::SetSphereEnvironmentMapping( TextureUnit tu, bool b )
{
	unsigned int idx = static_cast<unsigned int>(tu);
	if( idx >= D3D11_MAX_TEXTURES )	// not supported
		return;

	if( m_bSphereMapping[idx] != b )
	{
		m_bTexturesChanged = true;
		m_bSphereMapping[idx] = b;
	}
}

void RageDisplay_D3D11::SetCelShaded( int stage )
{
	// todo: implement me!
}

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
