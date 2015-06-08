//#pragma once
//
//#include "IImageDecoder.h"
//#include <memory>
//class ResourceLoader;
//
//class LumiaImageDecoder : public IImageDecoder
//{
//private:
//	concurrency::cancellation_token _cancelToken;
//	Windows::Foundation::Size _imageSize;
//	Windows::Foundation::Rect _lastRenderRect;
//	Windows::Storage::Streams::IRandomAccessStream^ _imageStream;
//	Lumia::Imaging::IImageProvider^ _loadedImageProvider;
//	Windows::Foundation::Size _renderSize;
//	Lumia::Imaging::Bitmap^ _baseBitmapData;
//	Lumia::Imaging::Bitmap^ _lastRenderBitmapData;
//	void StridedCopy(uint8_t* src, uint8_t* dst);
//public:
//	LumiaImageDecoder(Windows::Storage::Streams::IRandomAccessStream^ imageStream, concurrency::cancellation_token cancelToken);
//	static std::shared_ptr<IImageDecoder> MakeImageDecoder(Windows::Storage::Streams::IRandomAccessStream^ imageStream, concurrency::cancellation_token cancelToken);
//
//	virtual Windows::Foundation::Size MaxSize();
//	virtual Windows::Foundation::Size DefaultSize();
//	virtual void RenderSize(Windows::Foundation::Size size);
//	//handler is passed the decoded bytes and a bool to indicate whether or not to put it back in the queue to render again
//	virtual concurrency::task<Windows::Foundation::Rect> DecodeRectangleAsync(Windows::Foundation::Rect requestedRect);
//	virtual bool CanDecode(Windows::Foundation::Rect rect);
//	virtual Windows::Foundation::Rect DecodeRectangle(Windows::Foundation::Rect rect, Microsoft::WRL::ComPtr<ID2D1Bitmap1>& copyDestination, bool& requeue);
//	virtual void Suspend();
//	virtual void Resume();
//};

