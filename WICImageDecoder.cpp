#include "pch.h"
#include "WICImageDecoder.h"


#include <shcore.h>
#include <wincodec.h>

static void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw Platform::Exception::CreateException(hr);
	}
}

WICImageDecoder::WICImageDecoder(Windows::Storage::Streams::IRandomAccessStream^ imageStream, concurrency::cancellation_token cancelToken) : _cancelToken(cancelToken)
{
	Windows::System::Threading::ThreadPool::RunAsync(ref new Windows::System::Threading::WorkItemHandler([=](Windows::Foundation::IAsyncAction^ asyncAction)
	{
		try
		{
			HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
				IID_IWICImagingFactory, (LPVOID*)&_imagingFactory);

			Microsoft::WRL::ComPtr<IStream> pStream;
			imageStream->Seek(0);
			ThrowIfFailed(CreateStreamOverRandomAccessStream(imageStream, __uuidof(IStream), &pStream));
			Microsoft::WRL::ComPtr<IWICStream> pWicStream;
			ThrowIfFailed(_imagingFactory->CreateStream(&pWicStream));
			ThrowIfFailed(pWicStream->InitializeFromIStream(pStream.Get()));
			ThrowIfFailed(_imagingFactory->CreateDecoderFromStream(pWicStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &_bitmapDecoder));
			ThrowIfFailed(_bitmapDecoder->GetFrame(0, &_baseBitmapFrame));

			_defaultRenderSize = _currentRenderSize = DefaultSize();
			auto resizedDefault = DecodeRectangleInternal(Windows::Foundation::Rect(0, 0, _currentRenderSize.Width, _currentRenderSize.Height));
			ThrowIfFailed(_imagingFactory->CreateBitmapFromSource(resizedDefault.Get(), WICBitmapCreateCacheOption::WICBitmapNoCache, &_defaultBitmap));

			_readySource.set();
		}
		catch (Platform::Exception^ ex)
		{
			OutputDebugString(ex->ToString()->Data());
		}
		catch (...)
		{
			OutputDebugString(L"Exception while initializing WICImageDecoder");
		}
	}));
}
std::shared_ptr<IImageDecoder> WICImageDecoder::MakeImageDecoder(Windows::Storage::Streams::IRandomAccessStream^ imageStream, concurrency::cancellation_token cancelToken)
{
	return std::dynamic_pointer_cast<IImageDecoder>(std::make_shared<WICImageDecoder>(imageStream, cancelToken));
}

Windows::Foundation::Size WICImageDecoder::MaxSize()
{
	UINT width, height;
	ThrowIfFailed(_baseBitmapFrame->GetSize(&width, &height));
	return Windows::Foundation::Size(static_cast<float>(width), static_cast<float>(height));
}

Windows::Foundation::Size WICImageDecoder::DefaultSize()
{
	UINT width, height;
	ThrowIfFailed(_baseBitmapFrame->GetSize(&width, &height));
	auto nPercentW = (_maxRenderDimension / (float)width);
	auto nPercentH = (_maxRenderDimension / (float)height);
	auto overallImageScale = nPercentH < nPercentW ? nPercentH : nPercentW;
	
	auto reframeWidth = ((float)width * overallImageScale);
	auto reframeHeight = ((float)height * overallImageScale);
	return Windows::Foundation::Size(reframeWidth, reframeHeight);
}

void WICImageDecoder::RenderSize(Windows::Foundation::Size size)
{
	_currentRenderSize = size;
}

concurrency::task<Windows::Foundation::Rect> WICImageDecoder::DecodeRectangleAsync(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext)
{
	throw ref new Platform::NotImplementedException();
}

bool WICImageDecoder::CanDecode(Windows::Foundation::Rect rect)
{
	return true;
}

Microsoft::WRL::ComPtr<IWICBitmapSource> WICImageDecoder::DecodeRectangleInternal(Windows::Foundation::Rect requestedRect)
{
	Microsoft::WRL::ComPtr<IWICBitmapSource> stageSource;
	Microsoft::WRL::ComPtr<IWICBitmapClipper> clipper;
	Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
	Microsoft::WRL::ComPtr<IWICFormatConverter> converter;

	ThrowIfFailed(_baseBitmapFrame.As(&stageSource));

	UINT imageWidth, imageHeight;
	ThrowIfFailed(_baseBitmapFrame->GetSize(&imageWidth, &imageHeight));

	if (requestedRect.Left != 0 || requestedRect.Right != 0 ||
		requestedRect.Right != _currentRenderSize.Width || requestedRect.Bottom != _currentRenderSize.Height)
	{
		ThrowIfFailed(_imagingFactory->CreateBitmapClipper(&clipper));

		auto nPercentW = ((float)imageWidth / _currentRenderSize.Width);
		auto nPercentH = ((float)imageHeight / _currentRenderSize.Height);
		auto overallImageScale = nPercentH < nPercentW ? nPercentH : nPercentW;

		WICRect clipRect = { overallImageScale * requestedRect.Left, overallImageScale * requestedRect.Top,
			overallImageScale * requestedRect.Width, overallImageScale * requestedRect.Height };
		ThrowIfFailed(clipper->Initialize(stageSource.Get(), &clipRect));
		ThrowIfFailed(clipper.As(&stageSource));
	}

	if (_currentRenderSize.Width != static_cast<float>(imageWidth) ||
		_currentRenderSize.Height != static_cast<float>(imageHeight))
	{
		ThrowIfFailed(_imagingFactory->CreateBitmapScaler(&scaler));
		ThrowIfFailed(scaler->Initialize(stageSource.Get(), requestedRect.Width, requestedRect.Height, WICBitmapInterpolationMode::WICBitmapInterpolationModeFant));
		ThrowIfFailed(scaler.As(&stageSource));
	}

	ThrowIfFailed(_imagingFactory->CreateFormatConverter(&converter));
	ThrowIfFailed(converter->Initialize(stageSource.Get(), GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone, nullptr, 0.0f,
		WICBitmapPaletteTypeCustom));

	ThrowIfFailed(converter.As(&stageSource));
	return stageSource;
}

Windows::Foundation::Rect WICImageDecoder::DecodeRectangle(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext, Microsoft::WRL::ComPtr<ID2D1Bitmap1>& copyDestination, bool& requeue)
{
	Windows::Foundation::Rect defaultRenderRect(0, 0, _defaultRenderSize.Width, _defaultRenderSize.Height);
	if (_defaultRenderSize == _currentRenderSize && requestedRect.Right <= defaultRenderRect.Right && requestedRect.Bottom <= defaultRenderRect.Bottom)
	{
		ThrowIfFailed(d2dContext->CreateBitmapFromWicBitmap(_defaultBitmap.Get(), copyDestination.ReleaseAndGetAddressOf()));
		return defaultRenderRect;
	}
	else
	{
		auto converter = DecodeRectangleInternal(requestedRect);
		ThrowIfFailed(d2dContext->CreateBitmapFromWicBitmap(converter.Get(), copyDestination.ReleaseAndGetAddressOf()));
		return requestedRect;
	}
	
}

void WICImageDecoder::Suspend()
{
}

void WICImageDecoder::Resume()
{
}
