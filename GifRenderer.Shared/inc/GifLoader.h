#pragma once

#include <stdint.h>
#include <vector>
#include <memory>
#include "gif_lib.h"

namespace GifRenderer
{
	public interface class GetMoreData
	{
	public:
		bool Get(Platform::Array<uint8_t>^* data);
		void DisposeWorkaround();
	};

	enum DISPOSAL_METHODS
	{
		DM_UNDEFINED = 0,
		DM_NONE = 1,
		DM_BACKGROUND = 2,
		DM_PREVIOUS = 3
	};

	struct GifFrame
	{
		int width;
		int height;
		int top;
		int left;
		int right;
		int bottom;
		int transparentColor;
		uint32_t delay;
		DISPOSAL_METHODS disposal;
	};

	struct gif_user_data
	{
		int position;
		std::vector<uint8_t> buffer;
		Windows::Storage::Streams::IDataReader^ reader;
		Windows::Storage::Streams::DataReaderLoadOperation^ loadOperation;
		bool finishedLoad;
		bool finishedData;
	};

	private ref class GifLoader
	{
	private:
		gif_user_data _loaderData;
		bool _isLoaded;
		GifFileType* _gifFile;
		std::vector<GifFrame> _frames;
		std::unique_ptr<uint32_t[]> _renderBuffer;
	public:
		GifLoader(Windows::Storage::Streams::IDataReader^ reader);
		~GifLoader();

		bool IsLoaded() const;
		bool LoadMore();
		uint32_t Height() const;
		uint32_t Width() const;
		size_t FrameCount() const;
		uint32_t GetFrameDelay(size_t index) const;
		std::unique_ptr<uint32_t[]>& GetFrame(size_t currentIndex, size_t targetIndex);
		void ReadComplete();

	};
}