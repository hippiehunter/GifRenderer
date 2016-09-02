#pragma once

#include "IImageDecoder.h"
#include <memory>
#include <wrl.h>
#include <wrl\client.h>
#include <wincodec.h>

class ResourceLoader;

class WICImageDecoder : public IImageDecoder
{
private:
	Windows::Storage::Streams::IRandomAccessStream^ _imageStream;
	Windows::Foundation::Size _currentRenderSize;
	Windows::Foundation::Size _imageSize;
	concurrency::cancellation_token _cancelToken;
	Windows::Foundation::Size _defaultRenderSize;
public:
	WICImageDecoder(Windows::Storage::Streams::IRandomAccessStream^ imageStream, concurrency::cancellation_token cancelToken);
	static std::shared_ptr<IImageDecoder> MakeImageDecoder(Windows::Storage::Streams::IRandomAccessStream^ imageStream, concurrency::cancellation_token cancelToken);
	virtual Windows::Foundation::Size MaxSize();
	virtual Windows::Foundation::Size DefaultSize();
	virtual void RenderSize(Windows::Foundation::Size size);
	virtual concurrency::task<Windows::Foundation::Rect> DecodeRectangleAsync(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext);
	virtual bool CanDecode(Windows::Foundation::Rect rect);
	virtual Windows::Foundation::Rect DecodeRectangle(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext, Microsoft::WRL::ComPtr<ID2D1Bitmap1>& copyDestination, bool& requeue);
	virtual void Suspend();
	virtual void Resume();
	virtual ~WICImageDecoder() {};
};
