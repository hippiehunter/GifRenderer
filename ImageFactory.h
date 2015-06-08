#pragma once

#include <ppltasks.h>
#include <memory>

class IImageRenderer;
class IImageDecoder;

class ImageFactory
{
private:
	bool _isGif;
	std::shared_ptr<IImageDecoder> _decoder;
	unsigned int _loadedBytes;
public:
	static bool IsGif(Windows::Storage::Streams::IBuffer^ buffer);
	static concurrency::task<std::tuple<std::shared_ptr<IImageRenderer>, Windows::UI::Xaml::Media::ImageSource^>> MakeRenderer(Platform::String^ resourceIdentifier,
		std::function<void(float)> loadProgressCallback, Windows::UI::Core::CoreDispatcher^ dispatcher, concurrency::cancellation_token cancelToken);
};