#pragma once

#include <stdint.h>
#include <vector>
#include <memory>
#include "gif_lib.h"

namespace GifRenderer
{
	//returns empty array when there is remaining data to be transmitted, returns nullptr for end of file
	public delegate Platform::Array<uint8_t>^ GetMoreData();
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
		GetMoreData^ getter;
		int revertPosition;
	};

	class GifLoader
	{
	private:
		gif_user_data _loaderData;
		bool _isLoaded;
		GifFileType* _gifFile;
		std::vector<GifFrame> _frames;
		std::unique_ptr<uint32_t[]> _renderBuffer;
	public:
		GifLoader(GetMoreData^ getter);
		~GifLoader();

		bool IsLoaded() const;
		bool LoadMore();
		uint32_t Height() const;
		uint32_t Width() const;
		size_t FrameCount() const;
		uint32_t GetFrameDelay(size_t index) const;
		std::unique_ptr<uint32_t[]>& GetFrame(size_t currentIndex, size_t targetIndex);


	};
}