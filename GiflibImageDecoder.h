#pragma once

#include "IImageDecoder.h"
#include "giflibpp.h"
#include "BasicTimer.h"

#include <mutex>

enum DISPOSAL_METHODS
{
	DM_UNDEFINED = 0,
	DM_NONE = 1,
	DM_BACKGROUND = 2,
	DM_PREVIOUS = 3
};

struct bgraColor
{
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t alpha;
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
	unsigned int length;
	unsigned int position;
	unsigned int revertPos;
	std::vector<Windows::Storage::Streams::IBuffer^> buffer;
	bool finishedLoad;
	int read(GifByteType * buf, unsigned int length);
	void addData(Windows::Storage::Streams::IBuffer^ pbuffer);
	gif_user_data(concurrency::cancellation_token pcancelToken)
	{
		finishedLoad = false;
		position = 0;
	}

	void init(unsigned int pposition, Windows::Storage::Streams::IBuffer^ pbuffer)
	{
		revertPos = 0;
		position = pposition;
		length = pbuffer->Length;
		buffer.push_back(pbuffer);
		finishedLoad = false;
	}

	void revert()
	{
		position = revertPos;
	}
	void retro_checkpoint(int negativePosition)
	{
		int realPosition = position;
		position -= negativePosition;
		checkpoint();
		position = realPosition;
	}

	void checkpoint()
	{
		unsigned int endBufferIndex = 0;
		unsigned int consumedBufferSize = 0;
		unsigned int foundPos = 0;
		for (;endBufferIndex < buffer.size() && foundPos < position; )
		{
			foundPos += buffer[endBufferIndex]->Length;
			if (buffer.size() > endBufferIndex && foundPos < position)
			{
				consumedBufferSize += buffer[endBufferIndex++]->Length;
			}
		}

		if (((int)position - (int)consumedBufferSize) < 0)
		{
			throw std::runtime_error("invalid position");
		}

		if (position > buffer[0]->Length)
		{
			buffer.erase(buffer.begin(), buffer.begin() + endBufferIndex);
		}
		length -= consumedBufferSize;
		revertPos = position - consumedBufferSize;
		position = revertPos;
		
	}
};

class GiflibImageDecoder : public IImageDecoder
{
private:
	std::mutex _loadMutex;
	std::mutex _frameMutex;
	std::unique_ptr<GifFileType<gif_user_data>> _gifFile;
	std::vector<GifFrame> _frames;
	std::vector<SavedImage> _decodedImages;
	std::unique_ptr<uint32_t[]> _renderBuffer;
	gif_user_data _loaderData;
	Windows::Foundation::Size _renderSize;
	bool _isLoaded;
	int	_currentFrame;
	int	_lastFrame;
	bool _startedRendering;
	concurrency::cancellation_token _cancelToken;
	BasicTimer^ _timer;
	void MapRasterBits(uint8_t* rasterBits, std::unique_ptr<uint32_t[]>& targetFrame, ColorMapObject& colorMap, int top, int left, int bottom, int right, int width, int32_t transparencyColor);
	bool Update(float total, float delta);
	uint32_t GetFrameDelay(size_t index) const;
	size_t FrameCount() const;

public:
	static std::shared_ptr<IImageDecoder> MakeImageDecoder(Windows::Storage::Streams::IBuffer^ initialBuffer, concurrency::cancellation_token canceledToken);
	GiflibImageDecoder(Windows::Storage::Streams::IBuffer^ initialBuffer, concurrency::cancellation_token canceledToken);
	virtual ~GiflibImageDecoder() {};
	virtual void LoadHandler(Windows::Storage::Streams::IBuffer^ buffer, bool finished, uint32_t expectedSize);
	virtual Windows::Foundation::Size MaxSize();
	virtual Windows::Foundation::Size DefaultSize();
	virtual void RenderSize(Windows::Foundation::Size size);
	virtual concurrency::task<Windows::Foundation::Rect> DecodeRectangleAsync(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext);
	virtual bool CanDecode(Windows::Foundation::Rect rect);
	virtual Windows::Foundation::Rect DecodeRectangle(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext, Microsoft::WRL::ComPtr<ID2D1Bitmap1>& copyDestination, bool& requeue);
	virtual void Suspend();
	virtual void Resume();
private:
	template<typename GIFTYPE>
	void LoadGifFrames(GIFTYPE& gifFile, std::vector<GifFrame>& frames)
	{
		std::lock_guard<std::mutex> readGuard(_frameMutex);
		std::copy(make_move_iterator(gifFile->SavedImages.begin()), make_move_iterator(gifFile->SavedImages.end()), std::back_inserter(_decodedImages));
		gifFile->SavedImages.clear();
		uint32_t width = gifFile->SWidth;
		uint32_t height = gifFile->SHeight;

		for (auto i = frames.size(); i < _decodedImages.size(); i++)
		{
			uint32_t delay = 100;
			DISPOSAL_METHODS disposal = DISPOSAL_METHODS::DM_NONE;
			int32_t transparentColor = -1;

			auto extensionBlocks = _decodedImages[i].ExtensionBlocks;
			for (size_t ext = 0; ext < _decodedImages[i].ExtensionBlocks.size(); ext++)
			{
				if (extensionBlocks[ext].Function == 0xF9)
				{
					GraphicsControlBlock gcb(extensionBlocks[ext]);

					delay = gcb.DelayTime * 10;

					if (delay < 20)
					{
						delay = 100;
					}

					disposal = (DISPOSAL_METHODS)gcb.DisposalMode;
					transparentColor = gcb.TransparentColor;
				}
			}
			auto& imageDesc = _decodedImages[i].ImageDesc;
			int right = imageDesc.Left + imageDesc.Width;
			int bottom = imageDesc.Top + imageDesc.Height;
			int top = imageDesc.Top;
			int left = imageDesc.Left;

			frames.push_back(GifFrame());
			auto& frame = frames.back();
			frame.transparentColor = transparentColor;
			frame.height = height;
			frame.width = width;
			frame.delay = delay;
			frame.top = top;
			frame.bottom = bottom;
			frame.right = right;
			frame.left = left;
			frame.disposal = disposal;
		}
		if (frames.size() != _decodedImages.size())
			throw ref new Platform::InvalidArgumentException("image count didnt match frame size");
	}

	template<typename GIFTYPE>
	void LoadGifFrame(GIFTYPE& gifFile, const std::vector<GifFrame>& frames, std::unique_ptr<uint32_t[]>& buffer, size_t currentFrame, size_t targetFrame)
	{
		uint32_t width = gifFile->SWidth;
		uint32_t height = gifFile->SHeight;

		bgraColor bgColor = { 0, 0, 0, 0 };
		if (gifFile->SColorMap.Colors.size() != 0 && gifFile->SBackGroundColor > 0)
		{
			auto color = gifFile->SColorMap.Colors[gifFile->SBackGroundColor];
			bgColor.red = color.Red;
			bgColor.green = color.Green;
			bgColor.blue = color.Blue;
			bgColor.alpha = 255;
		}

		std::unique_ptr<uint32_t[]> lastFrame = nullptr;

		if (buffer == nullptr || targetFrame == 0 || currentFrame > targetFrame)
		{
			if (buffer == nullptr)
			{
				buffer = std::unique_ptr<uint32_t[]>(new uint32_t[width * height]);
				currentFrame = 0;
			}

			if (currentFrame > targetFrame)
				currentFrame = 0;

			for (decltype(height) y = 0; y < height; y++)
			{
				for (decltype(width) x = 0; x < width; x++)
				{
					auto offset = y * width + x;
					memcpy(buffer.get() + offset, &bgColor, 4);
				}
			}
		}

		for (auto i = currentFrame; i < _frames.size() && i <= targetFrame; i++)
		{
			auto& frame = frames[i];
			auto& decodeFrame = _decodedImages[i];
			auto disposal = frame.disposal;
			auto colorMap = (decodeFrame.ImageDesc.ColorMap.Colors.size() != 0 ? decodeFrame.ImageDesc.ColorMap : (gifFile->SColorMap.Colors.size() != 0 ? gifFile->SColorMap : ColorMapObject{}));

			if (disposal == DISPOSAL_METHODS::DM_PREVIOUS)
			{
				if (lastFrame == nullptr)
					lastFrame = std::unique_ptr<uint32_t[]>(new uint32_t[width * height]);

				memcpy(lastFrame.get(), buffer.get(), width * height * sizeof(uint32_t));
			}

			switch (disposal)
			{
			case DISPOSAL_METHODS::DM_BACKGROUND:
				for (decltype(height) y = 0; y < height; y++)
				{
					for (decltype(width) x = 0; x < width; x++)
					{
						int offset = y * width + x;
						memcpy(buffer.get() + offset, &bgColor, 4);
					}
				}
				break;
			case DISPOSAL_METHODS::DM_PREVIOUS:
				memcpy(buffer.get(), lastFrame.get(), width * height * sizeof(uint32_t));
				break;
			}
			MapRasterBits(decodeFrame.RasterBits.get(), buffer, colorMap, max(0, frame.top), max(frame.left, 0), min((int)height, frame.bottom), min((int)width, frame.right), (int)width, frame.transparentColor);
		}
	}
};