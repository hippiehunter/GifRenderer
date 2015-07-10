#include "pch.h"
#include "D2DRenderer.h"
#include "task_helper.h"
#include "windows.ui.xaml.media.dxinterop.h"
#include <cmath>
using namespace Microsoft::WRL;
using Windows::UI::Xaml::Media::ImageSource;
using Windows::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource;
using Windows::Foundation::Size;
using std::function;
using std::shared_ptr;
using std::make_shared;
using std::dynamic_pointer_cast;
using std::tuple;
using std::make_tuple;
using namespace concurrency;
using namespace task_helper;

void D2DRenderer::Draw()
{
	OutputDebugString(L"Drawing\n");
	CreateDeviceResources();

	RECT requestedBounds = { 0, 0, 0, 0 };
	DWORD rectCount;
	
	ThrowIfFailed(_sisNative->GetUpdateRectCount(&rectCount));
	if (rectCount > 0)
	{
		RECT* updateRects = reinterpret_cast<RECT*>(_alloca(rectCount * sizeof(RECT)));
		ThrowIfFailed(_sisNative->GetUpdateRects(updateRects, rectCount));
		requestedBounds = updateRects[0];
		for (DWORD i = 1; i < rectCount; i++)
		{
			if (updateRects[i].left < requestedBounds.left)
				requestedBounds.left = updateRects[i].left;
			if (updateRects[i].top < requestedBounds.top)
				requestedBounds.top = updateRects[i].top;

			if (updateRects[i].right > requestedBounds.right)
				requestedBounds.right = updateRects[i].right;
			if (updateRects[i].bottom > requestedBounds.bottom)
				requestedBounds.bottom = updateRects[i].bottom;
		}
		
		for (DWORD i = 0; i < rectCount; i++)
		{
			POINT offset;
			ComPtr<IDXGISurface> surface;
			ComPtr<ID2D1Bitmap1> renderBitmap;
			BeginDraw(offset, updateRects[i], surface);
			_d2dContext->Clear();
			DrawRequested(offset, updateRects[i], updateRects[i], renderBitmap);
			_d2dContext->SetTransform(D2D1::IdentityMatrix());
			_d2dContext->PopAxisAlignedClip();
			_d2dContext->Flush();
			EndDraw();
		}

		if (_lastRequestedRequeue)
		{
			_lastRequestedRequeue = false;
			_sisNative->Invalidate(RECT{ 0, 0, _currentWidth, _currentHeight });
		}
	}
}

void D2DRenderer::EndDraw()
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

void D2DRenderer::BeginDraw(POINT& offset, RECT& updateNativeRect, ComPtr<IDXGISurface>& surface)
{
	// Begin drawing - returns a target surface and an offset to use as the top left origin when drawing.
	HRESULT beginDrawHR = _sisNative->BeginDraw(updateNativeRect, &surface, &offset);

	if (beginDrawHR == DXGI_ERROR_DEVICE_REMOVED || beginDrawHR == DXGI_ERROR_DEVICE_RESET)
	{
		// If the device has been removed or reset, attempt to recreate it and continue drawing.
		CreateDeviceResources();
		BeginDraw(offset, updateNativeRect, surface);
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

	_d2dContext->SetUnitMode(D2D1_UNIT_MODE::D2D1_UNIT_MODE_PIXELS);
	auto width = updateNativeRect.right - updateNativeRect.left;
	auto height = updateNativeRect.bottom - updateNativeRect.top;
	_d2dContext->PushAxisAlignedClip(
		D2D1::RectF(
			static_cast<float>(offset.x),
			static_cast<float>(offset.y),
			static_cast<float>(offset.x) + width,
			static_cast<float>((offset.y) + height)
			),
		D2D1_ANTIALIAS_MODE_ALIASED
		);
}

bool D2DRenderer::DrawRequested(POINT offset, RECT requested, RECT overallRequested, ComPtr<ID2D1Bitmap1>& renderBitmap)
{
	bool requeue = false;
	Windows::Foundation::Rect requestedFoundationRect(static_cast<float>(overallRequested.left), static_cast<float>(overallRequested.top),
		static_cast<float>(overallRequested.right - overallRequested.left), static_cast<float>(overallRequested.bottom - overallRequested.top));
	bool canDecode = _decoder != nullptr && _decoder->CanDecode(requestedFoundationRect);
	if (_decoder)
	{
		try
		{
			if (renderBitmap == nullptr)
			{
				auto decodedRect = _decoder->DecodeRectangle(requestedFoundationRect, _d2dContext, renderBitmap, _lastRequestedRequeue);
				_lastRequested = RECT{ static_cast<long>(decodedRect.Left), static_cast<long>(decodedRect.Top), static_cast<long>(decodedRect.Right), static_cast<long>(decodedRect.Bottom) };
			}

			_d2dContext->SetTransform(
				D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x) - (requested.left - _lastRequested.left),
					static_cast<float>(offset.y) - (requested.top - _lastRequested.top)));

			_d2dContext->DrawBitmap(renderBitmap.Get());
		}
		catch (...) {}
	}

	if (_decoder != nullptr && !canDecode && _filterState == D2DRenderer::WAIT)
	{
		_filterState = D2DRenderer::APPLY;
		_lastRequested = overallRequested;
		finish_task(
			continue_task(_decoder->DecodeRectangleAsync(requestedFoundationRect, _d2dContext),
			[=](Windows::Foundation::Rect resultRect)
		{
			if (!_suspended)
			{
				_sisNative->Invalidate(RECT{ static_cast<long>(resultRect.Left), static_cast<long>(resultRect.Top), static_cast<long>(resultRect.Right), static_cast<long>(resultRect.Bottom) });
			}
			_filterState = D2DRenderer::WAIT;
			return task_from_result();
		}, [](Platform::Exception^ ex)
		{
			OutputDebugString(ex->ToString()->Data());
			return task_from_result();
		}), [](Platform::Exception^ ex) { OutputDebugString(ex->ToString()->Data()); } );
		requeue = true;
	}
	else if (_decoder != nullptr && !canDecode)
	{
		if(_lastRequested != overallRequested)
			_filterState = D2DRenderer::SCHEDULE;

		requeue = true;
	}
	return requeue;
}

void D2DRenderer::CreateDeviceResources()
{
	if (_d3dDevice == nullptr)
	{
		// This flag adds support for surfaces with a different color channel ordering
		// than the API default. It is required for compatibility with Direct2D.
		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT ;

#if defined(_DEBUG)    
		// If the project is in a debug build, enable debugging via SDK Layers.
		//creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
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
				&_d3dDevice,                   // Returns the Direct3D device created.
				nullptr,
				nullptr
				)
			);

		// Get the Direct3D 11.1 API device.
		ComPtr<IDXGIDevice> dxgiDevice;
		ThrowIfFailed(
			_d3dDevice.As(&dxgiDevice)
			);

		// Create the Direct2D device object and a corresponding context.
		ThrowIfFailed(
			D2D1CreateDevice(
				dxgiDevice.Get(),
				nullptr,
				&_d2dDevice
				)
			);
		
	}

	if (!_sisBound)
	{
		_sisBound = true;
		ThrowIfFailed(
			_d2dDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
				&_d2dContext
				)
			);

		_displayInfo = Windows::Graphics::Display::DisplayInformation::GetForCurrentView();

		// Set DPI to the display's current DPI.
		_d2dContext->SetDpi(_displayInfo->RawDpiX, _displayInfo->RawDpiY);

		// Get the Direct3D 11.1 API device.
		ComPtr<IDXGIDevice> dxgiDevice;
		ThrowIfFailed(
			_d3dDevice.As(&dxgiDevice)
			);
		// Associate the DXGI device with the SurfaceImageSource.
		ThrowIfFailed(
			_sisNative->SetDevice(dxgiDevice.Get())
			);
	}
}

void D2DRenderer::ViewChanged(float zoomFactor)
{
	try
	{
		auto imageSize = _decoder->MaxSize();
		auto reframeWidth = (imageSize.Width * min(1.0f, zoomFactor));
		auto reframeHeight = (imageSize.Height * min(1.0f, zoomFactor));
		if (min((int)reframeWidth, (int)imageSize.Width) != _currentWidth || min((int)reframeHeight, (int)imageSize.Height) != _currentHeight)
		{
			_currentHeight = min((int)reframeHeight, (int)imageSize.Height);
			_currentWidth = min((int)reframeWidth, (int)imageSize.Width);
			if (_sisNative != nullptr)
			{
				_sisNative->Resize(static_cast<int>(reframeWidth), static_cast<int>(reframeHeight));
				_sisNative->Invalidate(RECT{ 0, 0, static_cast<long>(reframeWidth), static_cast<long>(reframeHeight) });
			}
			_decoder->RenderSize(Windows::Foundation::Size(reframeWidth, reframeHeight));
		}
	}
	catch (...)
	{
		OutputDebugString(L"unknown error resizing");
	}
}

Size D2DRenderer::Size()
{
	return Windows::Foundation::Size(static_cast<float>(_currentWidth), static_cast<float>(_currentHeight));
}

Size D2DRenderer::MaxSize()
{
	return _decoder->MaxSize();
}

void D2DRenderer::Suspend()
{
	_suspended = true;
	if(_decoder != nullptr)
		_decoder->Suspend();
	else
		OutputDebugString(L"decoder was null in Suspend()");
}

void D2DRenderer::Resume()
{
	_suspended = false;
	if (_decoder != nullptr)
	{
		_decoder->Resume();
		if(_sisNative != nullptr)
			_sisNative->Invalidate(RECT{ 0, 0, _currentWidth, _currentHeight });
		else
			OutputDebugString(L"sisNative was null in Resume()");
	}
	else
		OutputDebugString(L"decoder was null in Resume()");
}

D2DRenderer::D2DRenderer(shared_ptr<IImageDecoder> decoder, ComPtr<IVirtualSurfaceImageSourceNative> sisNative, Windows::Foundation::Size initialSize, cancellation_token cancelToken) : _cancelToken(cancelToken)
{
	_sisBound = false;
	_suspended = false;
	_filterState = D2DRenderer::WAIT;
	_lastRequested = RECT{};
	_decoder = decoder;
	_sisNative = sisNative;
	_currentWidth = static_cast<int>(initialSize.Width);
	_currentHeight = static_cast<int>(initialSize.Height);
}

task<tuple<shared_ptr<IImageRenderer>, ImageSource^>> D2DRenderer::MakeRenderer(shared_ptr<IImageDecoder> decoder, Windows::UI::Core::CoreDispatcher^ dispatcher, cancellation_token cancelToken)
{
	task_completion_event<tuple<shared_ptr<IImageRenderer>, ImageSource^>> completionSource;
	auto handle_errors = [=](Platform::Exception^ ex) { completionSource.set_exception(ex); return task_from_result(); };
	finish_task(continue_void_task(decoder->Ready(), [=]()
	{
		ui_task(dispatcher, [=]()
		{
			auto size = decoder->DefaultSize();
			auto imageSource = ref new VirtualSurfaceImageSource(static_cast<int>(size.Width), static_cast<int>(size.Height));
			ComPtr<IVirtualSurfaceImageSourceNative> sisNative;
			reinterpret_cast<IUnknown*>(imageSource)->QueryInterface(IID_PPV_ARGS(&sisNative));
			auto renderer = make_shared<D2DRenderer>(decoder, sisNative, size, cancelToken);
			auto castRenderer = dynamic_pointer_cast<IImageRenderer>(renderer);
			renderer->_callback = Make<ImageRendererUpdatesCallbackNative>(castRenderer);
			renderer->ThrowIfFailed(sisNative->RegisterForUpdatesNeeded(renderer->_callback.Get()));
			completionSource.set(make_tuple(castRenderer, dynamic_cast<ImageSource^>(imageSource)));
		});
		return task_from_result();
	}, handle_errors, cancelToken), handle_errors);
	return task<tuple<shared_ptr<IImageRenderer>, ImageSource^>>(completionSource);
}

Microsoft::WRL::ComPtr<ID3D11Device> D2DRenderer::_d3dDevice = 0;
Microsoft::WRL::ComPtr<ID2D1Device> D2DRenderer::_d2dDevice = 0;