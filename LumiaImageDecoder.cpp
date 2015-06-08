//#include "pch.h"
//#include "LumiaImageDecoder.h"
//#include "ResourceLoader.h"
//#include "task_helper.h"
//
//using namespace Windows::Foundation;
//using namespace Windows::Storage::Streams;
//using namespace std;
//using namespace concurrency;
//using namespace Lumia::Imaging;
//using namespace task_helper;
//
//Size LumiaImageDecoder::MaxSize()
//{
//	return _imageSize;
//}
//
//Size LumiaImageDecoder::DefaultSize()
//{
//	auto nPercentW = (_maxRenderDimension / (float)_imageSize.Width);
//	auto nPercentH = (_maxRenderDimension / (float)_imageSize.Height);
//	auto overallImageScale = nPercentH < nPercentW ? nPercentH : nPercentW;
//
//	auto reframeWidth = (long)(_imageSize.Width * overallImageScale);
//	auto reframeHeight = (long)(_imageSize.Height * overallImageScale);
//	return Size(static_cast<float>(reframeWidth), static_cast<float>(reframeHeight));
//}
//
//void LumiaImageDecoder::RenderSize(Size size)
//{
//	if (_renderSize != size)
//	{
//		_lastRenderBitmapData = nullptr;
//	}
//	_renderSize = size;
//}
//
////handler is passed the decoded bytes and a bool to indicate whether or not to put it back in the queue to render again
//task<void> LumiaImageDecoder::DecodeRectangleAsync(Rect rect, function<void(uint8_t*, long, long, bool)> handler)
//{
//	if (rect.Width > 0 && rect.Height > 0)
//	{
//		auto nPercentW = (_maxRenderDimension / (float)_imageSize.Width);
//		auto nPercentH = (_maxRenderDimension / (float)_imageSize.Height);
//		auto overallImageScale = nPercentH < nPercentW ? nPercentH : nPercentW;
//
//		auto reframeWidth = (long)(_imageSize.Width * overallImageScale);
//		auto reframeHeight = (long)(_imageSize.Height * overallImageScale);
//		if(_loadedImageProvider == nullptr)
//			_loadedImageProvider = ref new RandomAccessStreamImageSource(_imageStream);
//		
//		return continue_task(_loadedImageProvider->GetBitmapAsync(ref new Bitmap(Windows::Foundation::Size((float)reframeWidth, (float)reframeHeight), ColorMode::Bgra8888), OutputOption::PreserveAspectRatio),
//			[=](Bitmap^ bitmap) -> task<void>
//		{
//			_lastRenderRect = rect;
//			_lastRenderBitmapData = bitmap;
//			auto pitch = bitmap->Buffers->Data[0]->Pitch;
//			ResourceLoader::GetBytesFromBuffer(bitmap->Buffers[0]->Buffer, [=](uint8_t* bytes, uint32_t byteCount) { handler(bytes, reframeWidth, reframeHeight, false); });
//			return task_from_result();
//		}, &default_error_handler<void, Platform::Exception^>, _cancelToken);
//
//	}
//	else
//	{
//		OutputDebugString(L"invalid decode request size");
//		return task_from_result();
//	}
//}
//
//bool LumiaImageDecoder::CanDecode(Windows::Foundation::Rect rect)
//{
//	if (_renderSize == DefaultSize())
//		return true;
//	else if (rect == _lastRenderRect)
//		return true;
//	else
//		return false;
//}
//
//void LumiaImageDecoder::DecodeRectangle(Rect rect, function<void(uint8_t*, long, long, bool)> handler, uint8_t* copyDestination)
//{
//	Lumia::Imaging::Bitmap^ bitmapData = nullptr;
//	if (rect == _lastRenderRect && _lastRenderBitmapData != nullptr)
//	{
//		bitmapData = _lastRenderBitmapData;
//		ResourceLoader::GetBytesFromBuffer(_baseBitmapData->Buffers[0]->Buffer, [=](uint8_t* bytes, uint32_t size)
//		{
//			if (copyDestination != nullptr)
//			{
//				memcpy(copyDestination, bytes, size);
//				handler(copyDestination, bitmapData->Dimensions.Width, bitmapData->Dimensions.Height, false);
//			}
//			else
//			{
//				handler(bytes, bitmapData->Dimensions.Width, bitmapData->Dimensions.Height, false);
//			}
//		});
//	}
//	else if (_baseBitmapData != nullptr)
//	{
//		ResourceLoader::GetBytesFromBuffer(_baseBitmapData->Buffers[0]->Buffer, [=](uint8_t* bytes, uint32_t size)
//		{
//			if (copyDestination != nullptr)
//			{
//				memcpy(copyDestination, bytes, size);
//				handler(copyDestination, _baseBitmapData->Dimensions.Width, _baseBitmapData->Dimensions.Height, false);
//			}
//			else
//			{
//				handler(bytes, _baseBitmapData->Dimensions.Width, _baseBitmapData->Dimensions.Height, false);
//			}
//		});
//	}
//	else
//	{
//		OutputDebugString(L"Failed to load base bitmap data");
//	}
//}
//
//void LumiaImageDecoder::Suspend()
//{
//	_loadedImageProvider = nullptr;
//	_lastRenderRect = {};
//	_lastRenderBitmapData = nullptr;
//}
//
//void LumiaImageDecoder::Resume()
//{
//}
//
//LumiaImageDecoder::LumiaImageDecoder(IRandomAccessStream^ imageStream, cancellation_token cancelToken) : _cancelToken(cancelToken)
//{
//	auto handle_errors = [=](Platform::Exception^ ex) { _readySource.set_exception(ex); return task_from_result(); };
//	_imageStream = imageStream;
//	_lastRenderRect = Rect{};
//	_lastRenderBitmapData = nullptr;
//	_loadedImageProvider = ref new RandomAccessStreamImageSource(imageStream);
//	finish_task(continue_task(_loadedImageProvider->GetInfoAsync(),
//		[=](ImageProviderInfo^ providerInfo) -> task<void>
//		{
//			_imageSize = providerInfo->ImageSize;
//			// Find aspect ratio for resize
//			
//			_renderSize = DefaultSize();
//
//			return continue_task(_loadedImageProvider->GetBitmapAsync(ref new Bitmap(Windows::Foundation::Size((float)_renderSize.Width, (float)_renderSize.Height), ColorMode::Bgra8888), OutputOption::PreserveAspectRatio),
//				[=](Bitmap^ bitmap)->task<void>
//				{
//					_baseBitmapData = bitmap;
//					//mark us as ready to render
//					_readySource.set();
//					return task_from_result();
//				}, handle_errors, _cancelToken);
//		}, handle_errors, _cancelToken), handle_errors);
//}
//
//std::shared_ptr<IImageDecoder> LumiaImageDecoder::MakeImageDecoder(IRandomAccessStream^ imageStream, cancellation_token cancelToken)
//{
//	return std::dynamic_pointer_cast<IImageDecoder>(std::make_shared<LumiaImageDecoder>(imageStream, cancelToken));
//}