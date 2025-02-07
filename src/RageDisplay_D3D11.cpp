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
#endif

#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <d3d11sdklayers.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>


// TODO: Instead of defining this here, enumerate the possible formats and select whatever one we want to use. This format should
// be fine for the uses of this application though.
const DXGI_FORMAT g_DefaultAdapterFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

/* Direct3D doesn't associate a palette with textures. Instead, we load a
 * palette into a slot. We need to keep track of which texture's palette is
 * stored in what slot. */
std::map<std::uintptr_t, std::size_t>		g_TexResourceToPaletteIndex;
std::list<std::size_t>			g_PaletteIndex;
struct TexturePalette { PALETTEENTRY p[256]; };
std::map<std::uintptr_t, TexturePalette>	g_TexResourceToTexturePalette;

// Load the palette, if any, for the given texture into a palette slot, and make it current.
static void SetPalette( std::uintptr_t TexResource )
{
	// If the texture isn't paletted, we have nothing to do.
	if( g_TexResourceToTexturePalette.find(TexResource) == g_TexResourceToTexturePalette.end() )
		return;

	// Is the palette already loaded?
	if( g_TexResourceToPaletteIndex.find(TexResource) == g_TexResourceToPaletteIndex.end() )
	{
		// It's not. Grab the least recently used slot.
		UINT iPalIndex = static_cast<UINT>(g_PaletteIndex.front());

		// If any other texture is currently using this slot, mark that palette unloaded.
		for( std::map<std::uintptr_t, std::size_t>::iterator i = g_TexResourceToPaletteIndex.begin(); i != g_TexResourceToPaletteIndex.end(); ++i )
		{
			if( i->second != iPalIndex )
				continue;
			g_TexResourceToPaletteIndex.erase(i);
			break;
		}

		// Load it.
		TexturePalette& pal = g_TexResourceToTexturePalette[TexResource];
		g_pd3dDevice->SetPaletteEntries( iPalIndex, pal.p );

		g_TexResourceToPaletteIndex[TexResource] = iPalIndex;
	}

	const int iPalIndex = g_TexResourceToPaletteIndex[TexResource];

	// Find this palette index in the least-recently-used queue and move it to the end.
	for(std::list<std::size_t>::iterator i = g_PaletteIndex.begin(); i != g_PaletteIndex.end(); ++i)
	{
		if( *i != iPalIndex )
			continue;
		g_PaletteIndex.erase(i);
		g_PaletteIndex.push_back(iPalIndex);
		break;
	}

	g_pd3dDevice->SetCurrentTexturePalette( iPalIndex );
}

static const RageDisplay::RagePixelFormatDesc PIXEL_FORMAT_DESC[NUM_RagePixelFormat] = {
	{
		/* R8G8B8A8 */
		32,
		{ 0xFF000000,
		  0x00FF0000,
		  0x0000FF00,
		  0x000000FF }
	}, {
		/* B8G8R8A8 */
		32,
		{ 0x0000FF00,
		  0x00FF0000,
		  0xFF000000,
		  0x000000FF }
	}, {
		/* B4G4R4A4 */
		16,
		{ 0x00F0,
		  0x0F00,
		  0xF000,
		  0x000F }
	}, {
		/* B5G5R5A1 */
		16,
		{ 0x003E,
		  0x07C0,
		  0xF800,
		  0x0001 }
	}, {
		/* RGB5 (N/A) */
		0, { 0,0,0,0 }
	}, {
		/* RGB8 (N/A) */
		0, { 0,0,0,0 }
	}, {
		/* Paletted */
		8,
		{ 0,0,0,0 } /* N/A */
	}, {
		/* B8G8R8X8 */
		32,
		{ 0x0000FF00,
		  0x00FF0000,
		  0xFF000000,
		  0x00000000 }
	}, {
		/* B5G5R5A1 */
		16,
		{ 0x003E,
		  0x07C0,
		  0xF800,
		  0x0001 }
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
	DXGI_FORMAT_P8,
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
	SetBlendMode(BLEND_NORMAL);

	m_bDepthStateChanged = true;
	m_DepthStencilDesc.DepthEnable = FALSE;
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

	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | (bDebugRenderer ? D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_DEBUGGABLE : 0);
	const D3D_DRIVER_TYPE driverType = pDxgiAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
	hr = D3D11CreateDevice(
		pDxgiAdapter.Get(),
		driverType,
		nullptr,
		flags,
		&featureLevel,
		1,
		D3D11_SDK_VERSION,
		&m_pDevice,
		nullptr,
		&m_pDeviceContext);

	if (!SUCCEEDED(hr)) {
		LOG->Trace("D3D11CreateDevice failed");
		return "D3D11CreateDevice failed";
	}

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
			pModes = std::make_unique_for_overwrite<DXGI_MODE_DESC[]>(numModes);
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

		const UINT requiredFlags = D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_DISPLAY;
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
		p.width,
		p.height,
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
		{p.rate, 1}, // DXGI_RATIONAL {Numerator, Denominator}
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
	HRESULT hr = m_pSwapchain->GetBuffer(0, IID_PPV_ARGS(&m_pRenderTarget));
	// TODO maybe I need to handle device lost here instead of asserting success?
	ASSERT(SUCCEEDED(hr));

	hr = m_pDevice->CreateRenderTargetView(m_pRenderTarget.Get(), nullptr, &m_pRenderTargetView);
	ASSERT(SUCCEEDED(hr));

	DXGI_SWAP_CHAIN_DESC swapchainDesc;
	// TODO does desc change on resolution change?
	hr = m_pSwapchain->GetDesc(&swapchainDesc);
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

	RageDisplay::ResolutionChanged();
}

int RageDisplay_D3D11::GetMaxTextureSize() const
{
	return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

bool RageDisplay_D3D11::BeginFrame()
{
	GraphicsWindow::Update();

	static constexpr float fClearColor[4] = { 0.f, 0.f, 0.f, 1.f };
	m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView.Get(), fClearColor);
	m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);

	m_pDeviceContext->OMSetRenderTargets(1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());

	return RageDisplay::BeginFrame();
}

static RageTimer g_LastFrameEndedAt( RageZeroTimer );
void RageDisplay_D3D11::EndFrame()
{
	FrameLimitBeforeVsync( GetActualVideoModeParams().rate );
	// TODO how to handle DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING?
	// What about vsync and no vsync? should we pass different sync interval here?
	m_pSwapchain->Present(1, 0);
	FrameLimitAfterVsync();

	RageDisplay::EndFrame();
}

bool RageDisplay_D3D11::SupportsTextureFormat( RagePixelFormat pixfmt, bool realtime )
{
	// TODO does D3D11 even support palleted textures? Even if so, is there a point in supporting them?
	if( pixfmt == RagePixelFormat_PAL )
		return false;

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

void RageDisplay_D3D11::SendCurrentMatrices()
{
	RageMatrix m;
	RageMatrixMultiply( &m, GetCentering(), GetProjectionTop() );

	// Convert to OpenGL-style "pixel-centered" coords
	RageMatrix m2 = GetCenteringMatrix( -0.5f, -0.5f, 0, 0 );
	RageMatrix projection;
	RageMatrixMultiply( &projection, &m2, &m );
	g_pd3dDevice->SetTransform( D3DTS_PROJECTION, (D3DMATRIX*)&projection );

	g_pd3dDevice->SetTransform( D3DTS_VIEW, (D3DMATRIX*)GetViewTop() );
	g_pd3dDevice->SetTransform( D3DTS_WORLD, (D3DMATRIX*)GetWorldTop() );

	FOREACH_ENUM( TextureUnit, tu )
	{
		// Optimization opportunity: Turn off texture transform if not using texture coords.
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2 );

		// If no texture is set for this texture unit, don't bother setting it up.
		IDirect3DBaseTexture9* pTexture = nullptr;
		g_pd3dDevice->GetTexture( tu, &pTexture );
		if( pTexture == nullptr )
			 continue;
		pTexture->Release();

		if( g_bSphereMapping[tu] )
		{
			static const RageMatrix tex = RageMatrix
			(
				0.5f,   0.0f,  0.0f, 0.0f,
				0.0f,  -0.5f,  0.0f, 0.0f,
				0.0f,   0.0f,  0.0f, 0.0f,
				0.5f,  -0.5f,  0.0f, 1.0f
			);
			g_pd3dDevice->SetTransform( (D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0+Enum::to_integral(tu)), (D3DMATRIX*)&tex );

			// Tell D3D to use transformed reflection vectors as texture co-ordinate 0
			// and then transform this coordinate by the specified texture matrix.
			g_pd3dDevice->SetTextureStageState( tu, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR );
		}
		else
		{
			/* Direct3D is expecting a 3x3 matrix loaded into the 4x4 in order
			 * to transform the 2-component texture coordinates. We currently
			 * only use translate and scale, and ignore the z component entirely,
			 * so convert the texture matrix from 4x4 to 3x3 by dropping z. */

			const RageMatrix &tex1 = *GetTextureTop();
			const RageMatrix tex2 = RageMatrix
			(
				tex1.m[0][0], tex1.m[0][1],  tex1.m[0][3],	0,
				tex1.m[1][0], tex1.m[1][1],  tex1.m[1][3],	0,
				tex1.m[3][0], tex1.m[3][1],  tex1.m[3][3],	0,
				0,				0,			0,		0
			);
			g_pd3dDevice->SetTransform( D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+Enum::to_integral(tu)), (D3DMATRIX*)&tex2 );

			g_pd3dDevice->SetTextureStageState( tu, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU );
		}
	}
}

class RageCompiledGeometryD3D11 : public RageCompiledGeometry
{
public:
	RageCompiledGeometryD3D11(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext) :
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
		std::vector<float> vVertexBuffer(GetTotalVertices() * 8), vVertexTextureScaleBuffer, vIndexBuffer(GetTotalTriangles() * 3);
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

		//TODO UpdateSubresource1 with D3D11_COPY_DISCARD
		m_pDeviceContext->UpdateSubresource(m_pVertexBuffer.Get(), 0, nullptr, vVertexBuffer.data(), 0, 0);
		m_pDeviceContext->UpdateSubresource(m_pIndexBuffer.Get(), 0, nullptr, vIndexBuffer.data(), 0, 0);
		if (m_bAnyNeedsTextureMatrixScale)
			m_pDeviceContext->UpdateSubresource(m_pVertexTextureScaleBuffer.Get(), 0, nullptr, vVertexTextureScaleBuffer.data(), 0, 0);
	}

	void Draw( int iMeshIndex ) const
	{
		const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];

		m_pDeviceContext->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
		m_pDeviceContext->IASetInputLayout(TODO);
		m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D11Buffer* vertexBuffers[2] = { m_pVertexBuffer.Get(), m_pVertexTextureScaleBuffer.Get() };
		m_pDeviceContext->IASetVertexBuffers(0, 2, vertexBuffers, nullptr, nullptr);

		//TODO rest of rendering state

		// TODO handle the texture matrix scale somehow
		if( meshInfo.m_bNeedsTextureMatrixScale )
		{
			// Kill the texture translation.
			// XXX: Change me to scale the translation by the TextureTranslationScale of the first vertex.
			RageMatrix m;
			g_pd3dDevice->GetTransform( D3DTS_TEXTURE0, (D3DMATRIX*)&m );

			m.m[2][0] = 0;
			m.m[2][1] = 0;

			g_pd3dDevice->SetTransform( D3DTS_TEXTURE0, (D3DMATRIX*)&m );
		}

		m_pDeviceContext->DrawIndexed(meshInfo.iTriangleCount * 3, meshInfo.iTriangleStart * 3, 0);
	}

protected:
	Microsoft::WRL::ComPtr<ID3D11Device> m_pDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_pDeviceContext;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pVertexTextureScaleBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pIndexBuffer;
};

RageCompiledGeometry* RageDisplay_D3D11::CreateCompiledGeometry()
{
	return new RageCompiledGeometryD3D11(m_pDevice.Get(), m_pDeviceContext.Get());
}

void RageDisplay_D3D11::DeleteCompiledGeometry( RageCompiledGeometry* p )
{
	delete p;
}

void RageDisplay_D3D11::PrepareVertexBuffers( const RageSpriteVertex v[], int iNumVerts )
{
	m_pDeviceContext->IASetInputLayout(TODO);

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

	m_pDeviceContext->IASetVertexBuffers(0, 1, pTempVertexBuffer.GetAddressOf(), nullptr, nullptr);
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

	PrepareVertexBuffers(v, iNumVerts);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// TODO rest of rendering state
	SendCurrentMatrices();

	m_pDeviceContext->Draw(iNumNewVerts, 0);
}

void RageDisplay_D3D11::DrawQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
#if 0
	// there isn't a quad strip primitive in D3D11, so we have to fake it with indexed triangles
	int iNumQuads = (iNumVerts - 2) / 2;
	int iNumTriangles = iNumQuads * 2;
	int iNumNewVerts = iNumTriangles * 3;
	std::vector<std::uint16_t> vTempIndexBuffer(iNumNewVerts);
	for (int i = 0; i < iNumQuads; ++i)
	{
		vTempIndexBuffer[i * 6    ] = i;
		vTempIndexBuffer[i * 6 + 1] = i + 1;
		vTempIndexBuffer[i * 6 + 2] = i + 2;
		vTempIndexBuffer[i * 6 + 3] = i + 1;
		vTempIndexBuffer[i * 6 + 4] = i + 2;
		vTempIndexBuffer[i * 6 + 5] = i + 3;
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

	PrepareVertexBuffers(v, iNumVerts);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// TODO rest of rendering state
	SendCurrentMatrices();

	m_pDeviceContext->Draw(iNumNewVerts, 0);
#else
	// there isn't a quad strip primitive in D3D11, so we have to fake it
	// but it seems that quad strip is pretty much identical to triangle strip
	PrepareVertexBuffers(v, iNumVerts);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// TODO rest of rendering state
	SendCurrentMatrices();

	m_pDeviceContext->Draw(iNumVerts, 0);
#endif
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

	PrepareVertexBuffers(v, iNumVerts);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// TODO rest of rendering state
	SendCurrentMatrices();

	m_pDeviceContext->Draw(iNumNewVerts, 0);
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

	PrepareVertexBuffers(v, iNumVerts);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// TODO rest of rendering state
	SendCurrentMatrices();

	m_pDeviceContext->Draw(iNumNewVerts, 0);
}

void RageDisplay_D3D11::DrawStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	PrepareVertexBuffers(v, iNumVerts);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// TODO rest of rendering state
	SendCurrentMatrices();

	m_pDeviceContext->Draw(iNumVerts, 0);
}

void RageDisplay_D3D11::DrawTrianglesInternal( const RageSpriteVertex v[], int iNumVerts )
{
	PrepareVertexBuffers(v, iNumVerts);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// TODO rest of rendering state
	SendCurrentMatrices();

	m_pDeviceContext->Draw(iNumVerts, 0);
}

void RageDisplay_D3D11::DrawCompiledGeometryInternal( const RageCompiledGeometry *p, int iMeshIndex )
{
	SendCurrentMatrices();

	/* If lighting is off, then the current material will have no effect. We
	 * want to still be able to color models with lighting off, so shove the
	 * material color in texture factor and modify the texture stage to use it
	 * instead of the vertex color (our models don't have vertex coloring anyway). */
	DWORD bLighting;
	g_pd3dDevice->GetRenderState( D3DRS_LIGHTING, &bLighting );

	if( !bLighting )
	{
		g_pd3dDevice->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR );
		g_pd3dDevice->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR );
	}

	p->Draw( iMeshIndex );

	if( !bLighting )
	{
		g_pd3dDevice->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_CURRENT );
		g_pd3dDevice->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
	}
}

/* Use the default poly-based implementation.  D3D lines apparently don't support
 * AA with greater-than-one widths. */
/*
void RageDisplay_D3D11::DrawLineStrip( const RageSpriteVertex v[], int iNumVerts, float LineWidth )
{
	ASSERT( iNumVerts >= 2 );
	g_pd3dDevice->SetRenderState( D3DRS_POINTSIZE, *((DWORD*)&LineWidth) );	// funky cast.  See D3DRENDERSTATETYPE doc
	g_pd3dDevice->SetVertexShader( D3DFVF_RageSpriteVertex );
	SendCurrentMatrices();
	g_pd3dDevice->DrawPrimitiveUP(
		D3DPT_LINESTRIP, // PrimitiveType
		iNumVerts-1, // PrimitiveCount,
		v, // pVertexStreamZeroData,
		sizeof(RageSpriteVertex)
	);
	StatsAddVerts( iNumVerts );
}
*/

void RageDisplay_D3D11::ClearAllTextures()
{
	FOREACH_ENUM( TextureUnit, i )
		SetTexture( i, 0 );
}

int RageDisplay_D3D11::GetNumTextureUnits()
{
	return g_DeviceCaps.MaxSimultaneousTextures;
}

void RageDisplay_D3D11::SetTexture( TextureUnit tu, std::uintptr_t iTexture )
{
//	g_DeviceCaps.MaxSimultaneousTextures = 1;
	if( tu >= (int) g_DeviceCaps.MaxSimultaneousTextures )	// not supported
		return;

	if( iTexture == 0 )
	{
		g_pd3dDevice->SetTexture( tu, nullptr );

		/* Intentionally commented out. Don't mess with texture stage state
		 * when just setting the texture. Model sets its texture modes before
		 * setting the final texture. */
		//g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLOROP, D3DTOP_DISABLE );
	}
	else
	{
		IDirect3DTexture9* pTex = reinterpret_cast<IDirect3DTexture9*>(iTexture);
		g_pd3dDevice->SetTexture( tu, pTex );

		/* Intentionally commented out. Don't mess with texture stage state
		 * when just setting the texture. Model sets its texture modes before
		 * setting the final texture. */
		//g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLOROP, D3DTOP_MODULATE );

		// Set palette (if any)
		SetPalette( iTexture );
	}
}

void RageDisplay_D3D11::SetTextureMode( TextureUnit tu, TextureMode tm )
{
	if( tu >= (int) g_DeviceCaps.MaxSimultaneousTextures )	// not supported
		return;

	switch( tm )
	{
	case TextureMode_Modulate:
		// Use D3DTA_CURRENT instead of diffuse so that multitexturing works
		// properly.  For stage 0, D3DTA_CURRENT is the diffuse color.

		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLORARG2, D3DTA_CURRENT );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLOROP,   D3DTOP_MODULATE );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
		break;
	case TextureMode_Add:
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLORARG2, D3DTA_CURRENT );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLOROP,   D3DTOP_ADD );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
		break;
	case TextureMode_Glow:
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLORARG2, D3DTA_CURRENT );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLOROP,   D3DTOP_SELECTARG2 );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
		g_pd3dDevice->SetTextureStageState( tu, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
		break;
	}
}

void RageDisplay_D3D11::SetTextureFiltering( TextureUnit tu, bool b )
{
	if( tu >= (int) g_DeviceCaps.MaxSimultaneousTextures ) // not supported
		return;

	g_pd3dDevice->SetSamplerState( tu, D3DSAMP_MINFILTER, b ? D3DTEXF_LINEAR : D3DTEXF_POINT );
	g_pd3dDevice->SetSamplerState( tu, D3DSAMP_MAGFILTER, b ? D3DTEXF_LINEAR : D3DTEXF_POINT );
}

void RageDisplay_D3D11::SetBlendMode( BlendMode mode )
{
	if (mode == m_CurrentBlendMode)
		return;

	m_CurrentBlendMode = mode;
	m_bBlendStateChanged = true;

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
	D3DVIEWPORT9 viewData;
	g_pd3dDevice->GetViewport( &viewData );
	viewData.MinZ = SCALE( f, 0.0f, 1.0f, 0.05f, 0.0f );
	viewData.MaxZ = SCALE( f, 0.0f, 1.0f, 1.0f, 0.95f );
	g_pd3dDevice->SetViewport( &viewData );
}

bool RageDisplay_D3D11::IsZTestEnabled() const
{
	// TODO should probably be disabled by default
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
	m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
}

void RageDisplay_D3D11::SetTextureWrapping( TextureUnit tu, bool b )
{
	if( tu >= (int) g_DeviceCaps.MaxSimultaneousTextures )	// not supported
		return;

	int mode = b ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;
	g_pd3dDevice->SetSamplerState( tu, D3DSAMP_ADDRESSU, mode );
	g_pd3dDevice->SetSamplerState( tu, D3DSAMP_ADDRESSV, mode );
}

void RageDisplay_D3D11::SetMaterial(
	const RageColor &emissive,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	float shininess
	)
{
	/* If lighting is off, then the current material will have no effect.
	 * We want to still be able to color models with lighting off, so shove the
	 * material color in texture factor and modify the texture stage to use it
	 * instead of the vertex color (our models don't have vertex coloring anyway). */
	DWORD bLighting;
	g_pd3dDevice->GetRenderState( D3DRS_LIGHTING, &bLighting );

	if( bLighting )
	{
		D3DMATERIAL9 mat;
		memcpy( &mat.Diffuse, diffuse, sizeof(float)*4 );
		memcpy( &mat.Ambient, ambient, sizeof(float)*4 );
		memcpy( &mat.Specular, specular, sizeof(float)*4 );
		memcpy( &mat.Emissive, emissive, sizeof(float)*4 );
		mat.Power = shininess;
		g_pd3dDevice->SetMaterial( &mat );
	}
	else
	{
		RageColor c = diffuse;
		c.r += emissive.r + ambient.r;
		c.g += emissive.g + ambient.g;
		c.b += emissive.b + ambient.b;
		RageVColor c2 = c;
		DWORD c3 = *(DWORD*)&c2;
		g_pd3dDevice->SetRenderState( D3DRS_TEXTUREFACTOR, c3 );
	}
}

void RageDisplay_D3D11::SetLighting( bool b )
{
	g_pd3dDevice->SetRenderState( D3DRS_LIGHTING, b );
}

void RageDisplay_D3D11::SetLightOff( int index )
{
	g_pd3dDevice->LightEnable( index, false );
}

void RageDisplay_D3D11::SetLightDirectional(
	int index,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	const RageVector3 &dir )
{
	g_pd3dDevice->LightEnable( index, true );

	D3DLIGHT9 light;
	ZERO( light );
	light.Type = D3DLIGHT_DIRECTIONAL;

	/* Z for lighting is flipped for D3D compared to OpenGL.
	 * XXX: figure out exactly why this is needed. Our transforms are probably
	 * goofed up, but the Z test is the same for both API's, so I'm not sure
	 * why we don't see other weirdness. -Chris */
	float position[] = { dir.x, dir.y, -dir.z };
	memcpy( &light.Direction, position, sizeof(position) );
	memcpy( &light.Diffuse, diffuse, sizeof(diffuse) );
	memcpy( &light.Ambient, ambient, sizeof(ambient) );
	memcpy( &light.Specular, specular, sizeof(specular) );

	// Same as OpenGL defaults.  Not used in directional lights.
//	light.Attenuation0 = 1;
//	light.Attenuation1 = 0;
//	light.Attenuation2 = 0;

	g_pd3dDevice->SetLight( index, &light );
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
	}
}

struct RageTexture_D3D11
{
	RagePixelFormat m_Pixfmt;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pSRV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_pRTV;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_pDSV; // If m_pTexture is a render target and a depth buffer was requested, this will be a DSV of the depth buffer (and not of m_pTexture)
};

std::uintptr_t RageDisplay_D3D11::CreateTexture(
	RagePixelFormat pixfmt,
	RageSurface* img,
	bool bGenerateMipMaps )
{
	D3D11_TEXTURE2D_DESC textureDesc;
	textureDesc.Width = img->w;
	textureDesc.Height = img->h;
	textureDesc.MipLevels = bGenerateMipMaps ? 0 : 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMATS[pixfmt];
	textureDesc.SampleDesc = { 1, 0 };
	textureDesc.Usage = D3D11_USAGE_DYNAMIC;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (bGenerateMipMaps ? D3D11_BIND_RENDER_TARGET: 0);
	textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	textureDesc.MiscFlags = bGenerateMipMaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> pTexture;
	HRESULT hr = m_pDevice->CreateTexture2D(&textureDesc, nullptr, &pTexture);
	ASSERT(SUCCEEDED(hr));

	D3D11_MAPPED_SUBRESOURCE mappedSubresource;
	hr = m_pDeviceContext->Map(pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
	ASSERT(SUCCEEDED(hr));

	const RagePixelFormatDesc& desc = PIXEL_FORMAT_DESC[pixfmt];
	RageSurface* pSurface = CreateSurfaceFrom(textureDesc.Width, textureDesc.Height, desc.bpp, desc.masks[0], desc.masks[1], desc.masks[2], desc.masks[3], reinterpret_cast<std::uint8_t*>(mappedSubresource.pData), mappedSubresource.RowPitch);
	RageSurfaceUtils::Blit(img, pSurface);
	delete pSurface;

	m_pDeviceContext->Unmap(pTexture.Get(), 0);

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pSRV;
	hr = m_pDevice->CreateShaderResourceView(pTexture.Get(), nullptr, &pSRV);
	ASSERT(SUCCEEDED(hr));

	if (bGenerateMipMaps)
		m_pDeviceContext->GenerateMips(pSRV.Get());

	RageTexture_D3D11* pTex = new RageTexture_D3D11{pixfmt, std::move(pTexture), std::move(pSRV)};
	return reinterpret_cast<std::uintptr_t>(pTex);
}

void RageDisplay_D3D11::UpdateTexture(
	std::uintptr_t uTexHandle,
	RageSurface* img,
	int xoffset, int yoffset, int width, int height )
{
	RageTexture_D3D11* pTex = reinterpret_cast<RageTexture_D3D11*>(uTexHandle);

	D3D11_TEXTURE2D_DESC textureDesc;
	pTex->m_pTexture->GetDesc(&textureDesc);

	ASSERT(xoffset + width <= textureDesc.Width);
	ASSERT(yoffset + height <= textureDesc.Height);

	D3D11_MAPPED_SUBRESOURCE mappedSubresource;
	HRESULT hr = m_pDeviceContext->Map(pTex->m_pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
	ASSERT(SUCCEEDED(hr));

	const RagePixelFormatDesc& desc = PIXEL_FORMAT_DESC[pTex->m_Pixfmt];
	RageSurface* pSurface = CreateSurfaceFrom(width, height, desc.bpp, desc.masks[0], desc.masks[1], desc.masks[2], desc.masks[3], reinterpret_cast<std::uint8_t*>(mappedSubresource.pData) + xoffset * desc.bpp / 4 + yoffset * mappedSubresource.RowPitch, mappedSubresource.RowPitch);
	RageSurfaceUtils::Blit(img, pSurface, width, height);
	delete pSurface;

	m_pDeviceContext->Unmap(pTex->m_pTexture.Get(), 0);
}

void RageDisplay_D3D11::DeleteTexture(std::uintptr_t iTexHandle)
{
	if (iTexHandle == 0)
		return;

	RageTexture_D3D11* pTex = reinterpret_cast<RageTexture_D3D11*>(iTexHandle);
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
	RageTexture_D3D11* pTex = new RageTexture_D3D11{RagePixelFormat_Invalid, std::move(pTexture), std::move(pSRV), std::move(pRTV), std::move(pDSV)};
	return reinterpret_cast<std::uintptr_t>(pTex);
}

std::uintptr_t RageDisplay_D3D11::GetRenderTarget();
void RageDisplay_D3D11::SetRenderTarget(std::uintptr_t iHandle, bool bPreserveTexture);

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

		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		HRESULT hr = m_pDeviceContext->Map(m_pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
		ASSERT(SUCCEEDED(hr));

		D3D11_TEXTURE2D_DESC textureDesc;
		pTex->m_pTexture->GetDesc(&textureDesc);
		ASSERT(pSurface->w == textureDesc.Width);
		ASSERT(pSurface->h == textureDesc.Height);
		ASSERT(pSurface->pitch == mappedSubresource.RowPitch);

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
	g_pd3dDevice->SetRenderState( D3DRS_ALPHATESTENABLE, b );
	g_pd3dDevice->SetRenderState( D3DRS_ALPHAREF, 0 );
	g_pd3dDevice->SetRenderState( D3DRS_ALPHAFUNC, D3DCMP_GREATER );
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

void RageDisplay_D3D11::SetSphereEnvironmentMapping( TextureUnit tu, bool b )
{
	g_bSphereMapping[tu] = b;
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
