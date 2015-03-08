#include "pch.h"
#include "VirtualSurfaceRenderer.h"
#include "task_helper.h"
#include <robuffer.h>
#include <collection.h>
#include <windows.h>
#include <string>

using namespace concurrency;
using namespace GifRenderer;
using namespace task_helper;
using namespace Lumia::Imaging;
using namespace Windows::Security::Cryptography::Core;
using namespace Windows::Security::Cryptography;


bool starts_with(std::wstring const &fullString, std::wstring const &start)
{
	if (fullString.length() >= start.length())
	{
		return (0 == fullString.compare(0, start.length(), start));
	}
	else
	{
		return false;
	}
}

concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> VirtualSurfaceRenderer::GetFileSource(Platform::String^ onDiskName, bool isUri)
{
	return continue_task(isUri ?
						 Windows::Storage::StorageFile::GetFileFromApplicationUriAsync(ref new Windows::Foundation::Uri(onDiskName)) :
						 Windows::Storage::StorageFile::GetFileFromPathAsync(onDiskName),
						 [=](Windows::Storage::StorageFile^ file)
	{

		return continue_task(file->OpenReadAsync(),
							 [](Windows::Storage::Streams::IRandomAccessStreamWithContentType^ accessStream)
		{
			return task_from_result(dynamic_cast<Windows::Storage::Streams::IRandomAccessStream^>(accessStream));
		});
	});
}

VirtualSurfaceRenderer::VirtualSurfaceRenderer(Platform::Array<std::uint8_t>^ initialData, Platform::String^ url,
											   Windows::Storage::Streams::IInputStream^ inputStream, std::function<void(int, int, Windows::UI::Xaml::Media::ImageSource^)>& fn, std::function<void(int)> loadCallback,
											   std::function<void(Platform::String^)>& errorHandler, concurrency::cancellation_token cancel) : _cancelToken(cancel)
{
	_errorHandler = errorHandler;
	_loadCallback = loadCallback;
	_updateCallback = fn;
#if WINDOWS_PHONE_APP
	_maxRenderDimension = Windows::System::MemoryManager::AppMemoryUsageLimit > 300 * 1024 * 1024 ? 1024.0f : 400.0f;
#else
	_maxRenderDimension = 2048;
#endif
	_lastRender = RECT{ 0, 0, 0, 0 };
	_filterState = FilterState::WAIT;
	_d3dDevice = nullptr;
	_d2dDevice = nullptr;
	_displayInfo = DisplayInformation::GetForCurrentView();
	_suspendingCookie = (Application::Current->Suspending += ref new SuspendingEventHandler(this, &VirtualSurfaceRenderer::OnSuspending));
	_resumingCookie = (Application::Current->Resuming += ref new Windows::Foundation::EventHandler<Object^>(this, &VirtualSurfaceRenderer::OnResuming));
	_callback = Make<VirtualSurfaceUpdatesCallbackNative>(this);

	//decide if the file is already local
	//it might be a bad assumption but we're going to assume if the url doesnt start with http then its not a network resource
	auto onDiskName = Windows::Storage::ApplicationData::Current->TemporaryFolder->Path + L"\\deleteme" + ComputeMD5(url) + L".jpg";
	_stat64i32 stat;
	bool existsOnDisk = false;
	bool isUri = false;
	if (!starts_with(url->Data(), L"http") && _wstat(url->Data(), &stat) == 0)
	{
		if (starts_with(url->Data(), L"ms-"))
			isUri = true;

		onDiskName = url;
		existsOnDisk = true;
	}
	else if (_wstat(onDiskName->Data(), &stat) == 0)
	{
		existsOnDisk = true;
	}

	auto handle_errors = [=](Platform::Exception^ ex)
	{
		auto opCanceled = dynamic_cast<Platform::OperationCanceledException^>(ex);
		if (opCanceled != nullptr)
			_errorHandler("Canceled");
		else
			_errorHandler(ex->Message);

		return task_from_result();
	};

	finish_task(
		continue_task(existsOnDisk ? GetFileSource(onDiskName, isUri) : GetImageSource(initialData, inputStream, url, cancel),
					   [=](Windows::Storage::Streams::IRandomAccessStream^ fileStream) -> task<void>
	{
		_fileStream = fileStream;
		auto imageSource = ref new RandomAccessStreamImageSource(_fileStream);
		return continue_task(imageSource->GetInfoAsync(),
								  [=](ImageProviderInfo^ providerInfo) -> task<void>
		{
			_imageSize = providerInfo->ImageSize;

			if (!_suspended)
			{
				if ((_imageSize.Width * _imageSize.Height) > (_maxRenderDimension * _maxRenderDimension * 2))
				{
					// Find aspect ratio for resize
					auto nPercentW = (_maxRenderDimension / (float) _imageSize.Width);
					auto nPercentH = (_maxRenderDimension / (float) _imageSize.Height);
					_overallImageScale = nPercentH < nPercentW ? nPercentH : nPercentW;

					auto reframeWidth = (long) (_imageSize.Width * _overallImageScale);
					auto reframeHeight = (long) (_imageSize.Height * _overallImageScale);


					_filterState = FilterState::APPLY;
					return continue_task(imageSource->GetBitmapAsync(ref new Bitmap(Windows::Foundation::Size((float) reframeWidth, (float) reframeHeight), ColorMode::Bgra8888), OutputOption::PreserveAspectRatio),
											  [=](Bitmap^ bitmap) -> task<void>
					{
						if (!_suspended)
						{
							_imageSource = ref new VirtualSurfaceImageSource(_currentWidth = (int) reframeWidth, _currentHeight = (int) reframeHeight);
							reinterpret_cast<IUnknown*>(_imageSource)->QueryInterface(IID_PPV_ARGS(&_sisNative));

							ThrowIfFailed(_sisNative->RegisterForUpdatesNeeded(_callback.Get()));
							CreateDeviceResources();


							_updateCallback((int) _imageSize.Width, (int) _imageSize.Height, _imageSource);
							_updateCallback = nullptr;

							D2D1_BITMAP_PROPERTIES properties;
							properties.dpiX = _displayInfo->RawDpiX;
							properties.dpiY = _displayInfo->RawDpiY;
							auto buffer = bitmap->Buffers->Data[0]->Buffer;
							// Obtain IBufferByteAccess
							ComPtr<Windows::Storage::Streams::IBufferByteAccess> pBufferByteAccess;
							ComPtr<IUnknown> pBuffer((IUnknown*) buffer);
							pBuffer.As(&pBufferByteAccess);
							byte* bufferData;
							pBufferByteAccess->Buffer(&bufferData);

							D2D1_SIZE_U size = { (UINT) bitmap->Dimensions.Width, (UINT) bitmap->Dimensions.Height };
							properties.pixelFormat = D2D1_PIXEL_FORMAT{ DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE };

							ThrowIfFailed(_d2dContext->CreateBitmap(size, bufferData, bitmap->Buffers->Data[0]->Pitch, properties, _originalBitmap.ReleaseAndGetAddressOf()));
							pBufferByteAccess.Reset();
							pBuffer.Reset();
							delete bitmap;
							RECT invalidateRect{ 0, 0, _currentWidth, _currentHeight };
							_sisNative->Invalidate(invalidateRect);

							_filterState = FilterState::WAIT;
						}
						return task_from_result();
					}, handle_errors, cancel);
				}
				else
				{
					auto wbmp = ref new Windows::UI::Xaml::Media::Imaging::WriteableBitmap((int) _imageSize.Width, (int) _imageSize.Height);
					WriteableBitmapRenderer^ wbr = ref new WriteableBitmapRenderer(imageSource, wbmp);
					return continue_task(wbr->RenderAsync(),
											  [=](Windows::UI::Xaml::Media::Imaging::WriteableBitmap^ bitmap) -> task<void>
					{
						_imageSource = bitmap;
						_updateCallback((int) _imageSize.Width, (int) _imageSize.Height, _imageSource);
						_updateCallback = nullptr;
						_imageSource = nullptr;
						delete imageSource;
						delete _fileStream;
						_fileStream = nullptr;
						return task_from_result();
					}, handle_errors, cancel);
				}
			}
			else
			{
				return task_from_result();
			}
		}, handle_errors, cancel);
	}, handle_errors), handle_errors);
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

	RECT invalidateRect{ 0, 0, _currentHeight, _currentWidth };
	if (_sisNative != nullptr)
		_sisNative->Invalidate(invalidateRect);
}

Windows::UI::Xaml::Media::ImageSource^ VirtualSurfaceRenderer::ImageSource::get()
{
	return _imageSource;
}

VirtualSurfaceRenderer::~VirtualSurfaceRenderer()
{
	if (_fileStream != nullptr)
	{
		delete _fileStream;
		_fileStream = nullptr;
	}

	_callback = nullptr;
	_suspended = true;
	_sisNative = nullptr;
	_callback = nullptr;
	_renderBitmap = nullptr;
	_d2dContext = nullptr;
	_d3dDevice = nullptr;
	_d2dDevice = nullptr;

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
	auto width = updateRectNative.right - updateRectNative.left;
	auto height = updateRectNative.bottom - updateRectNative.top;
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
		RECT visibleBounds = { 0, 0, 0, 0 };
		RECT requestedBounds = { 0, 0, 0, 0 };
		ThrowIfFailed(_sisNative->GetVisibleBounds(&visibleBounds));
		DWORD rectCount;
		RECT updateRects[12];
		ThrowIfFailed(_sisNative->GetUpdateRectCount(&rectCount));
		if (rectCount > 0)
		{

			ThrowIfFailed(_sisNative->GetUpdateRects(updateRects, rectCount));
			requestedBounds = updateRects[0];
			for (int i = 1; i < rectCount; i++)
			{
				if (updateRects[i].left < visibleBounds.left)
					requestedBounds.left = updateRects[i].left;
				if (updateRects[i].top < visibleBounds.top)
					requestedBounds.top = updateRects[i].top;

				if (updateRects[i].right > visibleBounds.right)
					requestedBounds.right = updateRects[i].right;
				if (updateRects[i].bottom > visibleBounds.bottom)
					requestedBounds.bottom = updateRects[i].bottom;
			}
		}

		auto width = requestedBounds.right - requestedBounds.left;
		auto height = requestedBounds.bottom - requestedBounds.top;

		if (_fileStream != nullptr && _specificRender != requestedBounds && width > 16 && height > 16)
		{
			switch (_filterState)
			{
			case FilterState::WAIT:
			{
				_filterState = FilterState::APPLY;
				_specificRender = requestedBounds;
				_fileStream->Seek(0);
				auto filter = ref new FilterEffect(ref new RandomAccessStreamImageSource(_fileStream));
				auto filters = ref new Platform::Collections::Vector<IFilter^>();
				auto frameScale = _imageSize.Width / static_cast<float>(_currentWidth);
				filters->Append(ref new Lumia::Imaging::Transforms::ReframingFilter(Windows::Foundation::Rect(
					static_cast<float>(requestedBounds.left) * frameScale,
					static_cast<float>(requestedBounds.top) * frameScale,
					static_cast<float>(width)* frameScale,
					static_cast<float>(height)* frameScale), 0.0));
				filter->Filters = filters;
				auto render = ref new BitmapRenderer(filter, ref new Bitmap(Windows::Foundation::Size((float) width, (float) height), ColorMode::Bgra8888));

				finish_task(create_task(render->RenderAsync(), _cancelToken)
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

							ThrowIfFailed(_d2dContext->CreateBitmap(size, bufferData, reframedBitmap->Buffers->Data[0]->Pitch, properties, _renderBitmap.ReleaseAndGetAddressOf()));
							_lastRender = RECT{ 0, 0, 0, 0 };
							if (_sisNative != nullptr)
								_sisNative->Invalidate(requestedBounds);
						}
						catch (...) {}
					}
					else
					{
						_specificRender = RECT{ 0, 0, 0, 0 };
					}
					_filterState = FilterState::WAIT;
				}), [](Platform::Exception^ ex) 
				{ 
					OutputDebugString(ex->Message->Data()); 
				});
				break;
			}
			case FilterState::APPLY:
			case FilterState::SCHEDULE:
				_filterState = FilterState::SCHEDULE;
			}
		}

		if (_d2dContext == nullptr)
			CreateDeviceResources();

		bool invalidate = false;
		if (_originalBitmap != nullptr || _renderBitmap != nullptr)
		{
			for (int i = 0; i < rectCount; i++)
			{
				POINT offset;
				BeginDraw(offset, updateRects[i]);
				_d2dContext->Clear();
				if (DrawRequested(offset, updateRects[i], requestedBounds))
					invalidate = true;
				_d2dContext->SetTransform(D2D1::IdentityMatrix());
				_d2dContext->PopAxisAlignedClip();
				_d2dContext->Flush();
				EndDraw();
			}
		}

		if (invalidate)
		{
			_sisNative->Invalidate(requestedBounds);
		}
	}
	catch (...) {}
}

bool VirtualSurfaceRenderer::DrawRequested(POINT offset, RECT requested, RECT overallRequested)
{
	if (_renderBitmap != nullptr && _filterState == FilterState::WAIT && _specificRender == overallRequested)
	{
		_d2dContext->SetTransform(
			D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x) - (requested.left - _specificRender.left),
			static_cast<float>(offset.y) - (requested.top - _specificRender.top)));

		_d2dContext->DrawBitmap(_renderBitmap.Get());
		return false;
	}
	else if (_originalBitmap != nullptr)
	{
		auto bitmapSize = _originalBitmap->GetPixelSize();
		auto scale = _currentWidth / static_cast<float>(bitmapSize.width);

		_d2dContext->SetTransform(
			D2D1::Matrix3x2F::Scale(scale, scale) *
			D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x) - requested.left, static_cast<float>((offset.y) - requested.top)));

		_d2dContext->DrawBitmap(_originalBitmap.Get());
		return true;
	}
	return true;
}

concurrency::task<void> VirtualSurfaceRenderer::LoadSome(Windows::Storage::Streams::IInputStream^ inputStream, Windows::Storage::Streams::IRandomAccessStream^ target, cancellation_token cancel)
{
	if (_suspended)
	{
		throw ref new Platform::OperationCanceledException();
	}
	else
	{
		auto errorHandler = [](Platform::Exception^ ex)
		{
			return task_from_exception<void>(ex);
		};
		return continue_task(inputStream->ReadAsync(ref new Windows::Storage::Streams::Buffer(32 * 1024), 32 * 1024, Windows::Storage::Streams::InputStreamOptions::None),
							 [=](Windows::Storage::Streams::IBuffer^ bufferResult)
		{
			auto bufferSize = bufferResult->Length;
			if (_loadCallback != nullptr)
				_loadCallback(bufferSize);
			return continue_task(target->WriteAsync(bufferResult),
								 [=](unsigned int)
			{
				if (bufferResult->Length >= 32 * 1024)
					return LoadSome(inputStream, target, cancel);
				else
					return task_from_result();
			}, errorHandler, cancel);
		}, errorHandler, cancel);
	}
}


Platform::String^ VirtualSurfaceRenderer::ComputeMD5(Platform::String^ str)
{
	auto alg = HashAlgorithmProvider::OpenAlgorithm("MD5");
	auto buff = CryptographicBuffer::ConvertStringToBinary(str, BinaryStringEncoding::Utf8);
	auto hashed = alg->HashData(buff);
	auto res = CryptographicBuffer::EncodeToHexString(hashed);
	return res;
}

concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> VirtualSurfaceRenderer::GetImageSource(
	Platform::Array<std::uint8_t>^ initialData,
	Windows::Storage::Streams::IInputStream^ inputStream, Platform::String^ url, cancellation_token cancel)
{
	auto targetFileName = L"deleteme" + ComputeMD5(url) + L".jpg";
	return continue_task(Windows::Storage::ApplicationData::Current->TemporaryFolder->CreateFileAsync(targetFileName, Windows::Storage::CreationCollisionOption::FailIfExists),
						 [=](Windows::Storage::StorageFile^ storeFile)
	{
		auto failCleanup = [=](Platform::Exception^ lex)
		{
			return create_task(storeFile->DeleteAsync())
				.then([=](task<void> tsk)
			{
				try
				{
					tsk.get();
				}
				catch (...)
				{
				}
				return task_from_exception<Windows::Storage::Streams::IRandomAccessStream^>(lex);
			});
		};
		return continue_task(storeFile->OpenAsync(Windows::Storage::FileAccessMode::ReadWrite),
							 [=](Windows::Storage::Streams::IRandomAccessStream^ file)
		{

			return continue_task(file->WriteAsync(Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(initialData)),
								 [=](unsigned int)
			{
				return LoadSome(inputStream, file, cancel)
					.then([=](task<void> loadSomeTask)
				{
					try
					{
						loadSomeTask.get();
						return continue_task(file->FlushAsync(),
											 [=](bool)
						{
							try
							{
								delete file;
								delete storeFile;
								return GetFileSource(Windows::Storage::ApplicationData::Current->TemporaryFolder->Path + L"\\" + targetFileName, false);
							}
							catch (Platform::Exception^ ex)
							{
								return failCleanup(ex);
							}
						}, failCleanup, cancel);

					}
					catch (Platform::Exception^ ex)
					{
						return failCleanup(ex);
					}
				});

			}, failCleanup, cancel);
		}, failCleanup, cancel);
	}, &default_error_handler<Windows::Storage::Streams::IRandomAccessStream^, Platform::Exception^>, cancel);
}

void VirtualSurfaceRenderer::ViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e)
{
	auto reframeWidth = (_imageSize.Width * std::min(1.0f, dynamic_cast<Windows::UI::Xaml::Controls::ScrollViewer^>(sender)->ZoomFactor));
	auto reframeHeight = (_imageSize.Height * std::min(1.0f, dynamic_cast<Windows::UI::Xaml::Controls::ScrollViewer^>(sender)->ZoomFactor));
	if (std::min((int) reframeWidth, (int) _imageSize.Width) != _currentWidth || std::min((int) reframeHeight, (int) _imageSize.Height) != _currentHeight)
	{
		_currentHeight = std::min((int) reframeHeight, (int) _imageSize.Height);
		_currentWidth = std::min((int) reframeWidth, (int) _imageSize.Width);
		if (_sisNative != nullptr)
		{
			_sisNative->Resize(reframeWidth, reframeHeight);
			_sisNative->Invalidate(RECT{ 0, 0, reframeWidth, reframeHeight });
		}
	}
}