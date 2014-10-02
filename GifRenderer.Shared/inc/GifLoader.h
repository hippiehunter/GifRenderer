#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include "giflibpp.h"

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
    int read(GifByteType * buf, unsigned int length);
    void revert() 
    {
      position = 0;
    }
    void checkpoint() 
    {
      buffer.erase(buffer.begin(), buffer.begin() + position);
      position = 0;
    }
	};

	private ref class GifLoader sealed
	{
	private:
		gif_user_data _loaderData;
		bool _isLoaded;
    std::unique_ptr<GifFileType<gif_user_data>> _gifFile;
		std::vector<GifFrame> _frames;
		std::unique_ptr<uint32_t[]> _renderBuffer;
	public:
    GifLoader(Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream);
		virtual ~GifLoader();

  internal:
		bool IsLoaded() const;
		bool LoadMore();
		uint32_t Height() const;
		uint32_t Width() const;
		size_t FrameCount() const;
		uint32_t GetFrameDelay(size_t index) const;
		std::unique_ptr<uint32_t[]>& GetFrame(size_t currentIndex, size_t targetIndex);
    void ReadComplete(Windows::Foundation::IAsyncOperation<unsigned int>^ asyncInfo, Windows::Foundation::AsyncStatus asyncStatus);

	};
}