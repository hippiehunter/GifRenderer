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
	try
	{
		Microsoft::WRL::ComPtr<IStream> comImageStream;
		Microsoft::WRL::ComPtr<IWICBitmapDecoder> bitmapDecoder;
		Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> baseBitmapFrame;
		Microsoft::WRL::ComPtr<IWICImagingFactory> imagingFactory;

		HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
			IID_IWICImagingFactory, (LPVOID*)&imagingFactory);
		_imageStream = imageStream;


		_imageStream->Seek(0);
		ThrowIfFailed(CreateStreamOverRandomAccessStream(_imageStream, __uuidof(IStream), &comImageStream));
		ThrowIfFailed(imagingFactory->CreateDecoderFromStream(comImageStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &bitmapDecoder));
		ThrowIfFailed(bitmapDecoder->GetFrame(0, &baseBitmapFrame));

		UINT width, height;
		ThrowIfFailed(baseBitmapFrame->GetSize(&width, &height));
		_imageSize = Windows::Foundation::Size(static_cast<float>(width), static_cast<float>(height));
		_defaultRenderSize = _currentRenderSize = DefaultSize();
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
}
std::shared_ptr<IImageDecoder> WICImageDecoder::MakeImageDecoder(Windows::Storage::Streams::IRandomAccessStream^ imageStream, concurrency::cancellation_token cancelToken)
{
	return std::dynamic_pointer_cast<IImageDecoder>(std::make_shared<WICImageDecoder>(imageStream, cancelToken));
}

Windows::Foundation::Size WICImageDecoder::MaxSize()
{
	return _imageSize;
}

Windows::Foundation::Size WICImageDecoder::DefaultSize()
{
	auto nPercentW = (_maxRenderDimension / _imageSize.Width);
	auto nPercentH = (_maxRenderDimension / _imageSize.Height);
	auto overallImageScale = nPercentH < nPercentW ? nPercentH : nPercentW;

	auto reframeWidth = (_imageSize.Width * overallImageScale);
	auto reframeHeight = (_imageSize.Height * overallImageScale);
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

Windows::Foundation::Rect WICImageDecoder::DecodeRectangle(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext, Microsoft::WRL::ComPtr<ID2D1Bitmap1>& copyDestination, bool& requeue)
{
	Microsoft::WRL::ComPtr<IWICBitmapClipper> clipper;
	Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
	Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
	Microsoft::WRL::ComPtr<IWICBitmapSource> stageSource;
	Microsoft::WRL::ComPtr<IStream> comImageStream;
	Microsoft::WRL::ComPtr<IWICBitmapDecoder> bitmapDecoder;
	Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> baseBitmapFrame;

	Microsoft::WRL::ComPtr<IWICImagingFactory> imagingFactory;

	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory, (LPVOID*)&imagingFactory);

	_imageStream->Seek(0);
	ThrowIfFailed(CreateStreamOverRandomAccessStream(_imageStream, __uuidof(IStream), &comImageStream));
	ThrowIfFailed(imagingFactory->CreateDecoderFromStream(comImageStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &bitmapDecoder));
	ThrowIfFailed(bitmapDecoder->GetFrame(0, &baseBitmapFrame));

	ThrowIfFailed(baseBitmapFrame.As(&stageSource));

	UINT imageWidth, imageHeight;
	ThrowIfFailed(baseBitmapFrame->GetSize(&imageWidth, &imageHeight));

	if (requestedRect.Left != 0 || requestedRect.Right != 0 ||
		requestedRect.Right != _currentRenderSize.Width || requestedRect.Bottom != _currentRenderSize.Height)
	{
		ThrowIfFailed(imagingFactory->CreateBitmapClipper(&clipper));

		auto nPercentW = ((float)imageWidth / _currentRenderSize.Width);
		auto nPercentH = ((float)imageHeight / _currentRenderSize.Height);
		auto overallImageScale = nPercentH < nPercentW ? nPercentH : nPercentW;

		WICRect clipRect = { (int)(overallImageScale * requestedRect.Left), (int)(overallImageScale * requestedRect.Top),
		  (int)(overallImageScale * requestedRect.Width), (int)(overallImageScale * requestedRect.Height) };
		ThrowIfFailed(clipper->Initialize(stageSource.Get(), &clipRect));
		ThrowIfFailed(clipper.As(&stageSource));
	}

	if (_currentRenderSize.Width != static_cast<float>(imageWidth) ||
		_currentRenderSize.Height != static_cast<float>(imageHeight))
	{
		ThrowIfFailed(imagingFactory->CreateBitmapScaler(&scaler));
		ThrowIfFailed(scaler->Initialize(stageSource.Get(), requestedRect.Width, requestedRect.Height, WICBitmapInterpolationMode::WICBitmapInterpolationModeFant));
		ThrowIfFailed(scaler.As(&stageSource));
	}

	ThrowIfFailed(imagingFactory->CreateFormatConverter(&converter));
	ThrowIfFailed(converter->Initialize(stageSource.Get(), GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone, nullptr, 0.0f,
		WICBitmapPaletteTypeCustom));

	ThrowIfFailed(d2dContext->CreateBitmapFromWicBitmap(converter.Get(), copyDestination.ReleaseAndGetAddressOf()));
	return requestedRect;
}

void WICImageDecoder::Suspend()
{
}

void WICImageDecoder::Resume()
{
}