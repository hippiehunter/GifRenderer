#include "pch.h"

#include "ImageFactory.h"
#include "ResourceLoader.h"
#include "task_helper.h"
#include "IImageDecoder.h"
#include "IImageRenderer.h"
#include "GiflibImageDecoder.h"
#include "WICImageDecoder.h"
#include "D2DRenderer.h"

using namespace concurrency;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Xaml::Media;
using namespace task_helper;

using std::function;
using std::shared_ptr;
using std::make_shared;
using std::make_tuple;
using std::tuple;

task<tuple<shared_ptr<IImageRenderer>, ImageSource^>> ImageFactory::MakeRenderer(String^ resourceIdentifier,
	std::function<void(float)> loadProgressCallback, Windows::UI::Core::CoreDispatcher^ dispatcher, cancellation_token cancelToken)
{
	auto imageFactory = std::make_shared<ImageFactory>();
	task_completion_event<tuple<shared_ptr<IImageRenderer>, ImageSource^>> completionSource;
	finish_task(
		continue_task(
			ResourceLoader::GetResource(resourceIdentifier, 
				[=](IBuffer^ buffer, uint32_t expectedSize)
				{
					if (ImageFactory::IsGif(buffer))
					{
						imageFactory->_isGif = true;
						auto handle_errors = [=](Platform::Exception^ ex) { completionSource.set_exception(ex); return task_from_result(); };
						imageFactory->_decoder = GiflibImageDecoder::MakeImageDecoder(buffer, cancelToken);
						finish_task(continue_task(D2DRenderer::MakeRenderer(imageFactory->_decoder, dispatcher, cancelToken),
							[=](tuple<shared_ptr<IImageRenderer>, ImageSource^> rslt) 
							{
								completionSource.set(std::move(rslt));
								return task_from_result();
							}, handle_errors), handle_errors);
						
						return make_tuple(false, true);
					}
					else
					{
						imageFactory->_isGif = false;
						return make_tuple(true, true);
					}
				}, [=](IBuffer^ buffer, bool finished, uint32_t expectedSize)
				{
					if(imageFactory->_decoder != nullptr)
						imageFactory->_decoder->LoadHandler(buffer, finished, expectedSize);

					if(buffer != nullptr)
						imageFactory->_loadedBytes += buffer->Length;

					//dont divide by zero when there is no expected or an invalid expected size
					if (expectedSize > 0)
						loadProgressCallback((static_cast<float>(imageFactory->_loadedBytes) / static_cast<float>(expectedSize)));
				}, cancelToken),
			[=](IRandomAccessStream^ resultStream)
			{
				if (!imageFactory->_isGif)
				{
					auto handle_errors = [=](Platform::Exception^ ex) { completionSource.set_exception(ex); return task_from_result(); };
					imageFactory->_decoder = WICImageDecoder::MakeImageDecoder(resultStream, cancelToken);
					finish_task(continue_task(D2DRenderer::MakeRenderer(imageFactory->_decoder, dispatcher, cancelToken),
						[=](tuple<shared_ptr<IImageRenderer>, ImageSource^> rslt)
					{
						completionSource.set(std::move(rslt));
						return task_from_result();
					}, handle_errors), handle_errors);
				}
				return task_from_result<IRandomAccessStream^>(resultStream);
			},&default_error_handler<IRandomAccessStream^, Platform::Exception^>),
		[=](Platform::Exception^ ex)
	{
		completionSource.set_exception(ex);
	});
	return task<tuple<shared_ptr<IImageRenderer>, ImageSource^>>(completionSource);
}

bool ImageFactory::IsGif(Windows::Storage::Streams::IBuffer^ buffer)
{
	bool isGif = false;
	ResourceLoader::GetBytesFromBuffer(buffer, [&](uint8_t* bytes, uint32_t byteCount) {
		isGif =
			bytes[0] == 0x47 && // G
			bytes[1] == 0x49 && // I
			bytes[2] == 0x46 && // F
			bytes[3] == 0x38 && // 8
			(bytes[4] == 0x39 || bytes[4] == 0x37) && // 9 or 7
			bytes[5] == 0x61;   // a
	});
	return isGif;
}