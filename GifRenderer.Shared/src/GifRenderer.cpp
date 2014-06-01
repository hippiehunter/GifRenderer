#include <cstdint>
#include <wrl.h>
#include <wrl\client.h>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include "windows.ui.xaml.media.dxinterop.h"

#include "GifLoader.h"
#include "BasicTimer.h"

using Windows::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource;
using namespace Microsoft::WRL;

namespace GifRenderer
{
	// Direct3D device
	Microsoft::WRL::ComPtr<ID3D11Device> g_d3dDevice;

	// Direct2D object
	Microsoft::WRL::ComPtr<ID2D1Device> g_d2dDevice;

	public ref class GifRenderer sealed
	{
	private:
		void Update();

		struct VirtualSerufaceUpdatesCallback : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IVirtualSurfaceUpdatesCallbackNative>
		{
		private:
			GifRenderer^ _renderer;
		public:
			VirtualSerufaceUpdatesCallback(GifRenderer^ renderer) : _renderer(renderer) {}
			
			virtual HRESULT STDMETHODCALLTYPE UpdatesNeeded()
			{
				_renderer->Update();
				return S_OK;
			}
		};

		ComPtr<IVirtualSurfaceImageSourceNative> _sisNative;
		Microsoft::WRL::ComPtr<ID2D1DeviceContext> _d2dContext;
		Microsoft::WRL::ComPtr<ID2D1Bitmap> _renderBitmap;
		GifLoader _gifLoader;
		BasicTimer^ _timer;
		VirtualSerufaceUpdatesCallback _callback;
		int	_currentFrame;
		int	_lastFrame;
		bool _startedRendering;

		inline void ThrowIfFailed(HRESULT hr)
		{
			if (FAILED(hr))
			{
				throw Platform::Exception::CreateException(hr);
			}
		}

	public:
		GifRenderer(GetMoreData^ getter) : _gifLoader(getter), _callback(this)
		{
			ImageSource = ref new VirtualSurfaceImageSource(_gifLoader.Width(), _gifLoader.Height());
			reinterpret_cast<IUnknown*>(ImageSource)->QueryInterface(IID_PPV_ARGS(&_sisNative));
			_sisNative->RegisterForUpdatesNeeded(&_callback);
			_timer = ref new BasicTimer();
		}

		property VirtualSurfaceImageSource^ ImageSource;

		
	private:

		void GifRenderer::CreateDeviceResources()
		{
			if (g_d3dDevice == nullptr)
			{
				// This flag adds support for surfaces with a different color channel ordering
				// than the API default. It is required for compatibility with Direct2D.
				UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)    
				// If the project is in a debug build, enable debugging via SDK Layers.
				creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

				// This array defines the set of DirectX hardware feature levels this app will support.
				// Note the ordering should be preserved.
				// Don't forget to declare your application's minimum required feature level in its
				// description.  All applications are assumed to support 9.1 unless otherwise stated.
				const D3D_FEATURE_LEVEL featureLevels[] =
				{
					D3D_FEATURE_LEVEL_11_1,
					D3D_FEATURE_LEVEL_11_0,
					D3D_FEATURE_LEVEL_10_1,
					D3D_FEATURE_LEVEL_10_0,
					D3D_FEATURE_LEVEL_9_3,
					D3D_FEATURE_LEVEL_9_2,
					D3D_FEATURE_LEVEL_9_1,
				};

				// Create the Direct3D 11 API device object.
				ThrowIfFailed(
					D3D11CreateDevice(
					nullptr,                        // Specify nullptr to use the default adapter.
					D3D_DRIVER_TYPE_HARDWARE,
					nullptr,
					creationFlags,                  // Set debug and Direct2D compatibility flags.
					featureLevels,                  // List of feature levels this app can support.
					ARRAYSIZE(featureLevels),
					D3D11_SDK_VERSION,              // Always set this to D3D11_SDK_VERSION for Metro style apps.
					&g_d3dDevice,                   // Returns the Direct3D device created.
					nullptr,
					nullptr
					)
					);

				// Get the Direct3D 11.1 API device.
				ComPtr<IDXGIDevice> dxgiDevice;
				ThrowIfFailed(
					g_d3dDevice.As(&dxgiDevice)
					);

				// Create the Direct2D device object and a corresponding context.
				ThrowIfFailed(
					D2D1CreateDevice(
					dxgiDevice.Get(),
					nullptr,
					&g_d2dDevice
					)
					);
			}

			ThrowIfFailed(
				g_d2dDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
				&_d2dContext
				)
				);

			// Set DPI to the display's current DPI.
			_d2dContext->SetDpi(Windows::Graphics::Display::DisplayProperties::LogicalDpi, Windows::Graphics::Display::DisplayProperties::LogicalDpi);

			// Get the Direct3D 11.1 API device.
			ComPtr<IDXGIDevice> dxgiDevice2;
			ThrowIfFailed(
				g_d3dDevice.As(&dxgiDevice2)
				);

			// Associate the DXGI device with the SurfaceImageSource.
			ThrowIfFailed(
				_sisNative->SetDevice(dxgiDevice2.Get())
				);

			
			D2D1_BITMAP_PROPERTIES properties;
			properties.pixelFormat = D2D1_PIXEL_FORMAT{ DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE };
			D2D1_SIZE_U size = { _gifLoader.Width(), _gifLoader.Height() };
			ThrowIfFailed(_d2dContext->CreateBitmap(size, properties, &_renderBitmap));
		}
		void GifRenderer::BeginDraw(Windows::Foundation::Rect updateRect)
		{
			POINT offset;
			ComPtr<IDXGISurface> surface;

			// Express target area as a native RECT type.
			RECT updateRectNative;
			updateRectNative.left = static_cast<LONG>(updateRect.Left);
			updateRectNative.top = static_cast<LONG>(updateRect.Top);
			updateRectNative.right = static_cast<LONG>(updateRect.Right);
			updateRectNative.bottom = static_cast<LONG>(updateRect.Bottom);

			// Begin drawing - returns a target surface and an offset to use as the top left origin when drawing.
			HRESULT beginDrawHR = _sisNative->BeginDraw(updateRectNative, &surface, &offset);

			if (beginDrawHR == DXGI_ERROR_DEVICE_REMOVED || beginDrawHR == DXGI_ERROR_DEVICE_RESET)
			{
				// If the device has been removed or reset, attempt to recreate it and continue drawing.
				CreateDeviceResources();
				BeginDraw(updateRect);
			}
			else
			{
				// Notify the caller by throwing an exception if any other error was encountered.
				ThrowIfFailed(beginDrawHR);
			}


			// Create render target.
			ComPtr<ID2D1Bitmap1> bitmap;

			ThrowIfFailed(
				_d2dContext->CreateBitmapFromDxgiSurface(
				surface.Get(),
				nullptr,
				&bitmap
				)
				);

			// Set context's render target.
			_d2dContext->SetTarget(bitmap.Get());


			// Begin drawing using D2D context.
			_d2dContext->BeginDraw();

			// Apply a clip and transform to constrain updates to the target update area.
			// This is required to ensure coordinates within the target surface remain
			// consistent by taking into account the offset returned by BeginDraw, and
			// can also improve performance by optimizing the area that is drawn by D2D.
			// Apps should always account for the offset output parameter returned by 
			// BeginDraw, since it may not match the passed updateRect input parameter's location.
			_d2dContext->PushAxisAlignedClip(
				D2D1::RectF(
				static_cast<float>(offset.x),
				static_cast<float>(offset.y),
				static_cast<float>(offset.x + updateRect.Width),
				static_cast<float>(offset.y + updateRect.Height)
				),
				D2D1_ANTIALIAS_MODE_ALIASED
				);

			_d2dContext->SetTransform(
				D2D1::Matrix3x2F::Translation(
				static_cast<float>(offset.x),
				static_cast<float>(offset.y)
				)
				);
		}

		void BeginDraw()    { BeginDraw(Windows::Foundation::Rect(0, 0, (float)_gifLoader.Width(), (float)_gifLoader.Height())); }

		void GifRenderer::EndDraw()
		{
			// Remove the transform and clip applied in BeginDraw since
			// the target area can change on every update.

			//_d2dContext->PopAxisAlignedClip();

			// Remove the render target and end drawing.
			ThrowIfFailed(
				_d2dContext->EndDraw()
				);

			_d2dContext->SetTarget(nullptr);

			ThrowIfFailed(
				_sisNative->EndDraw()
				);
		}

		bool GifRenderer::Update(float total, float delta)
		{
			double msDelta = ((double)total) * 1000;
			double accountedFor = 0;
			int i = 0;
			for (; accountedFor < msDelta; i++)
			{
				if (i >= _gifLoader.FrameCount())
					i = 0;

				accountedFor += _gifLoader.GetFrameDelay(i);
			}
			auto newFrame = max(i - 1, 0);
			if (newFrame != _currentFrame || _currentFrame == 0)
			{
				_currentFrame = newFrame;
				_startedRendering = true;
				return true;
			}
			else
			{
				if (!_startedRendering)
				{
					_startedRendering = true;
					return true;
				}
				else
					return false;
			}
		}
	};

	void GifRenderer::Update()
	{
		if (!_startedRendering)
			_timer->Reset();
		else
			_timer->Update();

		if (Update(_timer->Total, _timer->Delta))
		{
			D2D1_RECT_U rect = { 0, 0, _gifLoader.Width(), _gifLoader.Height() };
			D2D1_RECT_F rectf = { 0, 0, _gifLoader.Width(), _gifLoader.Height() };
			auto& renderedData = _gifLoader.GetFrame(_lastFrame, _currentFrame);
			_renderBitmap->CopyFromMemory(&rect, renderedData.get(), _gifLoader.Width() * 4);

			BeginDraw();
			_d2dContext->DrawBitmap(_renderBitmap.Get(), &rectf);
			_d2dContext->SetTransform(D2D1::IdentityMatrix());
			_d2dContext->PopAxisAlignedClip();
			EndDraw();
		}

		RECT invalidateRect{ 0, 0, _gifLoader.Width(), _gifLoader.Height() };
		_sisNative->Invalidate(invalidateRect);
	}
}