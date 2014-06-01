#include "GifLoader.h"

using namespace GifRenderer;

int istreamReader(GifFileType * gft, GifByteType * buf, int length)
{
	auto egi = reinterpret_cast<gif_user_data*>(gft->UserData);
	if (length == -1)
	{
		egi->position = egi->revertPosition;
		return 0;
	}
	else if (length == -2)
	{
		egi->revertPosition = egi->position;
		return 0;
	}
	else
	{
		if (egi->getter != nullptr && (egi->position == egi->buffer.size() || egi->position + length > egi->buffer.size()))
		{
			auto moreData = egi->getter();
			if (moreData == nullptr)
				egi->getter = nullptr;
			else if (moreData->Length == 0)
				return -1;
			else
			{
				auto existingSize = egi->buffer.size();
				egi->buffer.resize(existingSize + moreData->Length);
				memcpy(egi->buffer.data() + existingSize, moreData->Data, moreData->Length);
			}
			
		}

		auto egiLength = egi->buffer.size();

		if (egi->position == egiLength) return 0;
		if (egi->position + length == egiLength) length = egiLength - egi->position;
		memcpy(buf, egi->buffer.data() + egi->position, length);
		egi->position += length;

		return length;
	}
}

struct bgraColor
{
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t alpha;
};

void mapRasterBits(uint8_t* rasterBits, std::unique_ptr<uint32_t[]>& targetFrame, ColorMapObject * colorMap, int top, int left, int bottom, int right, int width, int32_t transparencyColor)
{

	int i = 0;
	for (int y = top; y < bottom; y++)
	{
		for (int x = left; x < right; x++)
		{
			int offset = y * width + x;
			int index = rasterBits[i];

			if (transparencyColor == -1 ||
				transparencyColor != index)
			{
				auto& gifColor = colorMap->Colors[index];
				bgraColor color = { gifColor.Blue, gifColor.Green, gifColor.Red, (uint8_t)255 };
				memcpy(targetFrame.get() + offset, &color, 4);
			}
			i++;
		}
	}
}

void loadGifFrame(GifFileType* gifFile, GifFrame& frame, std::unique_ptr<uint32_t[]>& buffer, int currentFrame, int targetFrame)
{
	uint32_t width = gifFile->SWidth;
	uint32_t height = gifFile->SHeight;
	int loopCount = 0;
	bool hasLoop = true;

	bgraColor bgColor;
	if (gifFile->SColorMap != nullptr)
	{
		auto color = gifFile->SColorMap->Colors[gifFile->SBackGroundColor];
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


		uint8_t* bufPtr = (uint8_t*)buffer.get();
		uint8_t* lastFramePtr = (uint8_t*)lastFrame.get();

		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				int offset = y * width + x;
				memcpy(buffer.get() + offset, &bgColor, 4);
			}
		}
	}

	for (int i = currentFrame; i < gifFile->ImageCount && i <= targetFrame; i++)
	{
		auto decodeFrame = frame.imageData;
		auto disposal = frame.disposal;
		auto colorMap = (decodeFrame->ImageDesc.ColorMap != nullptr ? decodeFrame->ImageDesc.ColorMap : (gifFile->SColorMap != nullptr ? gifFile->SColorMap : nullptr));

		if (disposal == DISPOSAL_METHODS::DM_PREVIOUS)
		{
			if (lastFrame == nullptr)
				lastFrame = std::unique_ptr<uint32_t[]>(new uint32_t[width * height]);

			memcpy(lastFrame.get(), buffer.get(), width * height * sizeof(uint32_t));
		}

		mapRasterBits(decodeFrame->RasterBits, buffer, colorMap, frame.top, frame.left, frame.bottom, frame.right, width, frame.transparentColor);

		switch (disposal)
		{
		case DISPOSAL_METHODS::DM_BACKGROUND:
			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
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
	}
}

void loadGifFrames(GifFileType* gifFile, std::vector<GifFrame>& frames)
{
	uint32_t width = gifFile->SWidth;
	uint32_t height = gifFile->SHeight;
	int loopCount = 0;
	bool hasLoop = true;

	for (int i = frames.size(); i < gifFile->ImageCount; i++)
	{
		uint32_t delay;
		DISPOSAL_METHODS disposal;
		int32_t transparentColor = -1;

		auto extensionBlocks = gifFile->SavedImages[i].ExtensionBlocks;
		for (int ext = 0; ext < gifFile->SavedImages[i].ExtensionBlockCount; ext++)
		{
			if (extensionBlocks[ext].Function == 0xF9)
			{
				GraphicsControlBlock gcb;
				DGifExtensionToGCB(extensionBlocks[ext].ByteCount, extensionBlocks[ext].Bytes, &gcb);

				delay = gcb.DelayTime * 10;

				if (delay < 20)
				{
					delay = 100;
				}

				disposal = (DISPOSAL_METHODS)gcb.DisposalMode;
				transparentColor = gcb.TransparentColor;
			}
		}
		auto& imageDesc = gifFile->SavedImages[i].ImageDesc;
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
		frame.imageData = gifFile->SavedImages + i;
		frame.disposal = disposal;
	}
}

GifLoader::GifLoader(GetMoreData^ getter)
{
	_loaderData = { 0, std::vector<uint8_t>(), getter, 0 };
	int error = 0;
	
	GifFileType* gifFile = DGifOpen(&_loaderData, istreamReader, &error);
	if (gifFile != nullptr)
	{
		DGifSlurp(gifFile);
		uint32_t width = (gifFile->SWidth % 2) + gifFile->SWidth;
		uint32_t height = (gifFile->SHeight % 2) + gifFile->SHeight;

		gifFile->SHeight = height;
		gifFile->SWidth = width;
		_gifFile = gifFile;
	}
	else
	{
		throw ref new Platform::InvalidArgumentException("invalid gif asset");
	}
}

GifLoader::~GifLoader() 
{
	int error = 0;
	if (_gifFile != nullptr)
		DGifCloseFile(_gifFile, &error);
}

bool GifLoader::IsLoaded() const { return _isLoaded; }
bool GifLoader::LoadMore()
{
	if (DGifSlurp(_gifFile) == GIF_OK)
	{
		_isLoaded = true;
	}
	else
	{
		if (_loaderData.getter == nullptr)
			return false;
		else
			return true;
	}
}
uint32_t GifLoader::GetFrameDelay(size_t index) const { return _frames[index].delay; }
uint32_t GifLoader::Height() const { return _gifFile->SHeight; }
uint32_t GifLoader::Width() const{ return _gifFile->SWidth; }
size_t GifLoader::FrameCount() const{ return _gifFile->ImageCount; }

std::unique_ptr<uint32_t[]>& GifLoader::GetFrame(size_t currentIndex, size_t targetIndex)
{
	auto& frame = _frames[targetIndex];
	loadGifFrame(_gifFile, frame, _renderBuffer, currentIndex, targetIndex);
	return _renderBuffer;
}