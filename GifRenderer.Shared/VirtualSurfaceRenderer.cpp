#include "VirtualSurfaceRenderer.h"
#include <robuffer.h>
#include <collection.h>
using namespace concurrency;
using namespace GifRenderer;
using namespace Nokia::Graphics::Imaging;
VirtualSurfaceRenderer::VirtualSurfaceRenderer(Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream)
{
	_filterState = FilterState::WAIT;
	_d3dDevice = nullptr;
	_d2dDevice = nullptr;
	_displayInfo = DisplayInformation::GetForCurrentView();
	_suspendingCookie = (Application::Current->Suspending += ref new SuspendingEventHandler(this, &VirtualSurfaceRenderer::OnSuspending));
	_resumingCookie = (Application::Current->Resuming += ref new Windows::Foundation::EventHandler<Object^>(this, &VirtualSurfaceRenderer::OnResuming));
	_callback = Make<VirtualSurfaceUpdatesCallbackNative>(this);
	GetImageSource(initialData, inputStream)
		.then([=](concurrency::task<RandomAccessStreamImageSource^> imageSourceTask)
	{
		try
		{
			_streamImageSource = imageSourceTask.get();
			create_task(_streamImageSource->GetInfoAsync())
				.then([=](task<ImageProviderInfo^> providerInfoTask)
			{
				try
				{
					auto providerInfo = providerInfoTask.get();
					_imageSize = providerInfo->ImageSize;

          if (!_suspended)
					{
						if ((_imageSize.Width * _imageSize.Height) > (1024 * 1024))
						{
							_imageSource = ref new VirtualSurfaceImageSource(_imageSize.Width, _imageSize.Height);
							reinterpret_cast<IUnknown*>(_imageSource)->QueryInterface(IID_PPV_ARGS(&_sisNative));

							ThrowIfFailed(_sisNative->RegisterForUpdatesNeeded(_callback.Get()));
							CreateDeviceResources();

							// Find aspect ratio for resize
							auto nPercentW = (1024.0 / _imageSize.Width);
							auto nPercentH = (1024.0 / _imageSize.Height);
							_overallImageScale = nPercentH < nPercentW ? nPercentH : nPercentW;
							PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs("ImageSource"));

							_filterState = FilterState::APPLY;
							create_task(_streamImageSource->GetBitmapAsync(ref new Bitmap(Windows::Foundation::Size(_imageSize.Width * _overallImageScale, _imageSize.Height * _overallImageScale), ColorMode::Bgra8888), OutputOption::PreserveAspectRatio))
								.then([=](Bitmap^ bitmap)
							{
                if (!_suspended && _d3dDevice != nullptr && _sisNative != nullptr)
                {
                  _overallBitmap = bitmap;
                  _filterState = FilterState::WAIT;
                  ThrowIfFailed(_sisNative->Invalidate(RECT{ 0, 0, _imageSize.Width, _imageSize.Height }));
                }
							});
						}
						else
						{
							auto wbmp = ref new Windows::UI::Xaml::Media::Imaging::WriteableBitmap(_imageSize.Width, _imageSize.Height);
							Nokia::Graphics::Imaging::WriteableBitmapRenderer^ wbr = ref new WriteableBitmapRenderer(_streamImageSource, wbmp);
							create_task(wbr->RenderAsync())
								.then([=](task<Windows::UI::Xaml::Media::Imaging::WriteableBitmap^> bitmapTask)
							{
								try
								{
									_imageSource = bitmapTask.get();
									PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs("ImageSource"));
									delete _streamImageSource;
									_streamImageSource = nullptr;
								}
								catch (...) {}
							});
						}



					}

				}
				catch (...)
				{

				}
			});
		}
		catch (...) {}
	});

}

void VirtualSurfaceRenderer::OnSuspending(Object ^sender, SuspendingEventArgs ^e)
{
	_suspended = true;
	_renderBitmap = nullptr;
	_d2dContext = nullptr;

	if (_d3dDevice != nullptr)
	{
		ComPtr<IDXGIDevice3> dxgiDevice;
		_d3dDevice.As(&dxgiDevice);

		// Hints to the driver that the app is entering an idle state and that its memory can be used temporarily for other apps.
		dxgiDevice->Trim();
		_d3dDevice = nullptr;
		_d2dDevice = nullptr;
	}

}

void VirtualSurfaceRenderer::OnResuming(Object ^sender, Object ^e)
{
	_filterState = FilterState::WAIT;
	_suspended = false;
	_needsRender = true;

	RECT invalidateRect{ 0, 0, 1, 1 };
	if (_sisNative != nullptr)
		_sisNative->Invalidate(invalidateRect);
}

Platform::Object^ VirtualSurfaceRenderer::ImageSource::get()
{
	return _imageSource;
}

VirtualSurfaceRenderer::~VirtualSurfaceRenderer()
{
	if (_streamImageSource != nullptr)
	{
		delete _streamImageSource;
		_streamImageSource = nullptr;
	}

	if (_overallBitmap != nullptr)
	{
		delete _overallBitmap;
		_overallBitmap = nullptr;
	}
	_callback = nullptr;
	_suspended = true;
	if (_sisNative)
		_sisNative->RegisterForUpdatesNeeded(nullptr);
	_sisNative = nullptr;
	_callback = nullptr;
	_renderBitmap = nullptr;
	_d2dContext = nullptr;
	Application::Current->Suspending -= _suspendingCookie;
	Application::Current->Resuming -= _resumingCookie;
}

void VirtualSurfaceRenderer::CreateDeviceResources()
{
	if (_d3dDevice == nullptr)
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
		const D3D_FEATURE_LEVEL featureLevels [] =
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

	ThrowIfFailed(
		_d2dDevice->CreateDeviceContext(
		D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
		&_d2dContext
		)
		);

	// Set DPI to the display's current DPI.
	_d2dContext->SetDpi(_displayInfo->RawDpiX, _displayInfo->RawDpiY);

	// Get the Direct3D 11.1 API device.
	ComPtr<IDXGIDevice> dxgiDevice2;
	ThrowIfFailed(
		_d3dDevice.As(&dxgiDevice2)
		);

	// Associate the DXGI device with the SurfaceImageSource.
	ThrowIfFailed(
		_sisNative->SetDevice(dxgiDevice2.Get())
		);
}
void VirtualSurfaceRenderer::BeginDraw(POINT& offset, RECT& updateRectNative)
{
	ComPtr<IDXGISurface> surface;
	//_sisNative->GetVisibleBounds(&updateRectNative);

	// Begin drawing - returns a target surface and an offset to use as the top left origin when drawing.
	HRESULT beginDrawHR = _sisNative->BeginDraw(updateRectNative, &surface, &offset);

	if (beginDrawHR == DXGI_ERROR_DEVICE_REMOVED || beginDrawHR == DXGI_ERROR_DEVICE_RESET)
	{
		// If the device has been removed or reset, attempt to recreate it and continue drawing.
		CreateDeviceResources();
		BeginDraw(offset, updateRectNative);
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

	_d2dContext->PushAxisAlignedClip(
		D2D1::RectF(
		static_cast<float>(offset.x) + updateRectNative.left,
		static_cast<float>(offset.y) + updateRectNative.top,
		static_cast<float>(offset.x + (updateRectNative.right - updateRectNative.left)),
		static_cast<float>((offset.y) + (updateRectNative.bottom - updateRectNative.top))
		),
		D2D1_ANTIALIAS_MODE_ALIASED
		);

	
}

void VirtualSurfaceRenderer::EndDraw()
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

void VirtualSurfaceRenderer::Update()
{
	try
	{
		RECT visibleBounds;
		_sisNative->GetVisibleBounds(&visibleBounds);

		auto width = visibleBounds.right - visibleBounds.left;
		auto height = visibleBounds.bottom - visibleBounds.top;
		auto nPercentW = (width / _imageSize.Width);
		auto nPercentH = (height / _imageSize.Height);
		auto targetPct = nPercentH < nPercentW ? nPercentH : nPercentW;

		if (_streamImageSource != nullptr && _specificRender != visibleBounds)
		{
			switch (_filterState)
			{
			case FilterState::WAIT:
			{
				_specificBitmap = nullptr;
				_filterState = FilterState::APPLY;
				_specificRender = visibleBounds;
				auto filter = ref new FilterEffect(_streamImageSource);
				auto nPercentW = (1024.0 / width);
				auto nPercentH = (1024.0 / height);
				_specificImageScale = min(1.0, nPercentH < nPercentW ? nPercentH : nPercentW);
				auto filters = ref new Platform::Collections::Vector<IFilter^>();
				filters->Append(ref new ReframingFilter(Windows::Foundation::Rect(visibleBounds.left, visibleBounds.top, width, height), 0.0));
				filter->Filters = filters;
				auto render = ref new BitmapRenderer(filter, ref new Bitmap(Windows::Foundation::Size(width * _specificImageScale, height * _specificImageScale), ColorMode::Bgra8888));
				
				create_task(render->RenderAsync())
					.then([=](task<Bitmap^> reframedBitmapTask)
				{
					if (_filterState == FilterState::APPLY)
					{
						try
						{
							auto reframedBitmap = reframedBitmapTask.get();
							D2D1_BITMAP_PROPERTIES properties;
							properties.dpiX = _displayInfo->RawDpiX;
							properties.dpiY = _displayInfo->RawDpiY;
							auto buffer = reframedBitmap->Buffers->Data[0]->Buffer;
							// Obtain IBufferByteAccess
							ComPtr<Windows::Storage::Streams::IBufferByteAccess> pBufferByteAccess;
							ComPtr<IUnknown> pBuffer((IUnknown*) buffer);
							pBuffer.As(&pBufferByteAccess);
							byte* bufferData;
							pBufferByteAccess->Buffer(&bufferData);

							D2D1_SIZE_U size = { (UINT) reframedBitmap->Dimensions.Width, (UINT) reframedBitmap->Dimensions.Height };
							switch (reframedBitmap->ColorMode)
							{
							case ColorMode::Bgra8888:
								properties.pixelFormat = D2D1_PIXEL_FORMAT{ DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE };
								break;
							default:
								break;
							}

							ThrowIfFailed(_d2dContext->CreateBitmap(size, bufferData, reframedBitmap->Buffers->Data[0]->Pitch, properties, _specificBitmap.ReleaseAndGetAddressOf()));
							_needsRender = true;
						}
						catch (...){}
					}
					_filterState = FilterState::WAIT;
				});
				break;
			}
			case FilterState::APPLY:
			case FilterState::SCHEDULE:
				_filterState = FilterState::SCHEDULE;
			}
			_needsRender = true;
		}
		else if (_specificRender != visibleBounds)
		{
			_specificBitmap = nullptr;
		}

		

		if (_d2dContext == nullptr)
			CreateDeviceResources();

		if (_overallBitmap != nullptr)
		{
			if (_renderBitmap == nullptr)
			{
				_needsRender = true;
				D2D1_BITMAP_PROPERTIES properties;
				properties.dpiX = _displayInfo->RawDpiX;
				properties.dpiY = _displayInfo->RawDpiY;
				auto buffer = _overallBitmap->Buffers->Data[0]->Buffer;
				// Obtain IBufferByteAccess
				ComPtr<Windows::Storage::Streams::IBufferByteAccess> pBufferByteAccess;
				ComPtr<IUnknown> pBuffer((IUnknown*) buffer);
				pBuffer.As(&pBufferByteAccess);
				byte* bufferData;
				pBufferByteAccess->Buffer(&bufferData);

				D2D1_SIZE_U size = { (UINT) _overallBitmap->Dimensions.Width, (UINT) _overallBitmap->Dimensions.Height };
				switch (_overallBitmap->ColorMode)
				{
				case ColorMode::Bgra8888:
					properties.pixelFormat = D2D1_PIXEL_FORMAT{ DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE };
					break;
				default:
					break;
				}

				ThrowIfFailed(_d2dContext->CreateBitmap(size, bufferData, _overallBitmap->Buffers->Data[0]->Pitch, properties, _renderBitmap.ReleaseAndGetAddressOf()));
			}
			if (_needsRender)
			{
				POINT offset;
				RECT updateNativeRect = { 0, 0, _imageSize.Width, _imageSize.Height };
				BeginDraw(offset, updateNativeRect);

				if (_specificBitmap != nullptr && _filterState == FilterState::WAIT)
				{
					_d2dContext->SetTransform(
						D2D1::Matrix3x2F::Scale(1.0 / _specificImageScale, 1.0 / _specificImageScale) * 
						D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x + _specificRender.left), static_cast<float>((offset.y) + _specificRender.top))
						
						);


					_d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::White));
					if (_specificBitmap != nullptr)
						_d2dContext->DrawBitmap(_specificBitmap.Get());
				}
				else
				{
					_d2dContext->SetTransform(
						D2D1::Matrix3x2F::Scale(1.0 / _overallImageScale, 1.0 / _overallImageScale) *
						D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x + updateNativeRect.left), static_cast<float>((offset.y) + updateNativeRect.top)));

					_d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::White));
					if (_renderBitmap != nullptr)
						_d2dContext->DrawBitmap(_renderBitmap.Get());
				}
				

				
				_d2dContext->SetTransform(D2D1::IdentityMatrix());
				_d2dContext->PopAxisAlignedClip();
				_d2dContext->Flush();
				EndDraw();
				_needsRender = false;
			}
		}

		if (!_suspended)
		{
			RECT visibleBounds;
			_sisNative->GetVisibleBounds(&visibleBounds);
			_sisNative->Invalidate(visibleBounds);
		}
		

	}
	catch (...) {}
}

concurrency::task<void> VirtualSurfaceRenderer::LoadSome(Windows::Storage::Streams::IInputStream^ inputStream, Windows::Storage::Streams::DataWriter^ target)
{
	return create_task(inputStream->ReadAsync(ref new Windows::Storage::Streams::Buffer(64 * 1024), 64 * 1024, Windows::Storage::Streams::InputStreamOptions::None))
		.then([=](task<Windows::Storage::Streams::IBuffer^> buffer)
	{
		try
		{
			auto bufferResult = buffer.get();
			target->WriteBuffer(bufferResult);
			if (bufferResult->Length >= 64 * 1024)
				return LoadSome(inputStream, target);
		}
		catch (...)
		{
		}
		return task_from_result();
	});
}

concurrency::task<Nokia::Graphics::Imaging::RandomAccessStreamImageSource^> VirtualSurfaceRenderer::GetImageSource(
	Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData,
	Windows::Storage::Streams::IInputStream^ inputStream)
{
	Windows::Storage::Streams::InMemoryRandomAccessStream^ result = ref new Windows::Storage::Streams::InMemoryRandomAccessStream();
	Windows::Storage::Streams::DataWriter^ writer = ref new Windows::Storage::Streams::DataWriter(result);
	for (unsigned int i = 0; i < initialData->Size; i++)
	{
		//yuck
		writer->WriteByte(initialData->GetAt(i));
	}

	return LoadSome(inputStream, writer).then([=]()
	{
		return create_task(writer->StoreAsync())
			.then([=](uint32_t)
		{
			return create_task(writer->FlushAsync())
				.then([=](bool)
			{
				result->Seek(0);
				writer->DetachStream();
				return ref new Nokia::Graphics::Imaging::RandomAccessStreamImageSource(result);
			});
		});
	});
}