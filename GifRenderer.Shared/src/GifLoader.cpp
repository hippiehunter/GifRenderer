#include "GifLoader.h"

using namespace GifRenderer;

int istreamReader(GifFileType * gft, GifByteType * buf, int length)
{
	auto egi = reinterpret_cast<gif_user_data*>(gft->UserData);
	if (length == -1)
	{
		egi->position = 0;
		return 0;
	}
	else if (length == -2)
	{
		egi->buffer.clear();
		return 0;
	}
	else
	{
		if (egi->getter != nullptr && (egi->position == egi->buffer.size() || egi->position + length > egi->buffer.size()))
		{
			if (length > (egi->buffer.size() - egi->position) + egi->reader->UnconsumedBufferLength)
			{
				return -1;
			}
			else
			{
				Platform::WriteOnlyArray<uint8_t>^ moreData = ref new Platform::WriteOnlyArray<uint8_t>(std::min(egi->reader->UnconsumedBufferLength, 4096));
				egi->reader->ReadBytes(moreData);
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
				auto colorTarget = reinterpret_cast<GifColorType*>(targetFrame.get() + offset);
				*colorTarget = colorMap->Colors[index];
			}
			i++;
		}
	}
}

void loadGifFrame(GifFileType* gifFile, const std::vector<GifFrame>& frames, std::unique_ptr<uint32_t[]>& buffer, int currentFrame, int targetFrame)
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
		auto& frame = frames[i];
		auto decodeFrame = gifFile->SavedImages[i];
		auto disposal = frame.disposal;
		auto colorMap = (decodeFrame.ImageDesc.ColorMap != nullptr ? decodeFrame.ImageDesc.ColorMap : (gifFile->SColorMap != nullptr ? gifFile->SColorMap : nullptr));

		if (disposal == DISPOSAL_METHODS::DM_PREVIOUS)
		{
			if (lastFrame == nullptr)
				lastFrame = std::unique_ptr<uint32_t[]>(new uint32_t[width * height]);

			memcpy(lastFrame.get(), buffer.get(), width * height * sizeof(uint32_t));
		}

		mapRasterBits(decodeFrame.RasterBits, buffer, colorMap, frame.top, frame.left, frame.bottom, frame.right, width, frame.transparentColor);

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
		frame.disposal = disposal;
	}
	if (frames.size() != gifFile->ImageCount)
		throw ref new Platform::InvalidArgumentException("image count didnt match frame size");
}

GifLoader::GifLoader(Platform::Array<uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream)
{
	auto dataReader = ref new Windows::Storage::Streams::DataReader(inputStream);
	dataReader->InputStreamOptions = Windows::Storage::Streams::InputStreamOptions::ReadAhead;
	auto loadOperation = dataReader->LoadAsync(16 * 1024 * 1024); //16mb is our max load for a gif
	loadOperation->Completed += ref new Windows::Foundation::AsyncOperationCompletedHandler(this, &GifLoader::ReadComplete);
	_loaderData = { 0, std::vector<uint8_t>(), dataReader, loadOperation, false, false };
	int error = 0;
	
	GifFileType* gifFile = DGifOpen(&_loaderData, istreamReader, &error);
	if (gifFile != nullptr)
	{
		_loaderData.buffer.clear();
		if (DGifSlurp(gifFile) == GIF_OK)
		{
			_isLoaded = true;
			_loaderData.buffer.clear();
			_loaderData.reader = nullptr;
			_loaderData.position = 0;
		}
		uint32_t width = (gifFile->SWidth % 2) + gifFile->SWidth;
		uint32_t height = (gifFile->SHeight % 2) + gifFile->SHeight;

		gifFile->SHeight = height;
		gifFile->SWidth = width;
		_gifFile = gifFile;
		loadGifFrames(_gifFile, _frames);
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

	_gifFile = nullptr;
	if (_loaderData.loadOperation != nullptr)
	{
		_loaderData.loadOperation->Cancel();
	}
}

bool GifLoader::IsLoaded() const { return _isLoaded || _loaderData.finishedLoad; }
bool GifLoader::LoadMore()
{
	if (DGifSlurp(_gifFile) == GIF_OK)
	{
		loadGifFrames(_gifFile, _frames);
		_isLoaded = true;
    _loaderData.buffer.clear();
		_loaderData.reader = nullptr;
    _loaderData.position = 0;
		return false;
	}
	else
	{
		loadGifFrames(_gifFile, _frames);
		if (_loaderData.finishedData)
		{
			_loaderData.finishedLoad;
			return false;
		}
		else
			return true;		
	}
}
uint32_t GifLoader::GetFrameDelay(size_t index) const { return _frames[index].delay; }
uint32_t GifLoader::Height() const { return _gifFile->SHeight; }
uint32_t GifLoader::Width() const{ return _gifFile->SWidth; }
size_t GifLoader::FrameCount() const
{
	if (_frames.size() != _gifFile->ImageCount)
		throw ref new Platform::InvalidArgumentException("image count didnt match frame size");
	return _frames.size(); 
}

std::unique_ptr<uint32_t[]>& GifLoader::GetFrame(size_t currentIndex, size_t targetIndex)
{
	loadGifFrame(_gifFile, _frames, _renderBuffer, currentIndex, targetIndex);
	return _renderBuffer;
}