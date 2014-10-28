#include "GifLoader.h"
#include <algorithm>
#include <collection.h>
#include <ppltasks.h>
using namespace GifRenderer;

void gif_user_data::readSome()
{
  finishedReader = false;
  concurrency::create_task(reader->LoadAsync(256 * 1024))
    .then([=](concurrency::task<uint32_t> loadOp)
  {
    try
    {
      if (loadOp.get() >= 256 *  1024)
        finishedReader = true;
      //else
        //finishedData = true;
    }
    catch (...)
    {
      finishedData = true;
    }
  });

}

int gif_user_data::read(GifByteType * buf, unsigned int length)
{
  if (reader != nullptr && (position == buffer.size() || position + length > buffer.size()))
	{
		if (length > (buffer.size() - position) + reader->UnconsumedBufferLength)
		{
      if (finishedReader)
      {
        if (reader->UnconsumedBufferLength > 0)
        {
          Platform::Array<uint8_t>^ moreData = ref new Platform::Array<uint8_t>(min(reader->UnconsumedBufferLength, (uint32_t)4096));
          reader->ReadBytes(moreData);
          auto existingSize = buffer.size();
          buffer.resize(existingSize + moreData->Length);
          memcpy(buffer.data() + existingSize, moreData->Data, moreData->Length);
        }
        readSome();
      }
			return -1;
		}
		else
		{
      Platform::Array<uint8_t>^ moreData = ref new Platform::Array<uint8_t>(min(reader->UnconsumedBufferLength, (uint32_t)4096));
      reader->ReadBytes(moreData);
			auto existingSize = buffer.size();
			buffer.resize(existingSize + moreData->Length);
			memcpy(buffer.data() + existingSize, moreData->Data, moreData->Length);
		}
	}

	auto egiLength = buffer.size();

	if (position == egiLength) return 0;
	if (position + length == egiLength) length = egiLength - position;
	memcpy(buf, buffer.data() + position, length);
	position += length;

	return length;
}

struct bgraColor
{
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t alpha;
};

void mapRasterBits(uint8_t* rasterBits, std::unique_ptr<uint32_t[]>& targetFrame, ColorMapObject& colorMap, int top, int left, int bottom, int right, int width, int32_t transparencyColor)
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
				*colorTarget = colorMap.Colors[index];
			}
			i++;
		}
	}
}

template<typename GIFTYPE>
void loadGifFrame(GIFTYPE& gifFile, const std::vector<GifFrame>& frames, std::unique_ptr<uint32_t[]>& buffer, size_t currentFrame, size_t targetFrame)
{
	uint32_t width = gifFile->SWidth;
	uint32_t height = gifFile->SHeight;

	bgraColor bgColor;
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

	for (auto i = currentFrame; i < gifFile->SavedImages.size() && i <= targetFrame; i++)
	{
		auto& frame = frames[i];
		auto& decodeFrame = gifFile->SavedImages[i];
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

    mapRasterBits(decodeFrame.RasterBits.get(), buffer, colorMap, frame.top, frame.left, frame.bottom, frame.right, width, frame.transparentColor);
	}
}
template<typename GIFTYPE>
void loadGifFrames(GIFTYPE& gifFile, std::vector<GifFrame>& frames)
{
	uint32_t width = gifFile->SWidth;
	uint32_t height = gifFile->SHeight;

	for (auto i = frames.size(); i < gifFile->SavedImages.size(); i++)
	{
		uint32_t delay = 100;
		DISPOSAL_METHODS disposal = DISPOSAL_METHODS::DM_NONE;
		int32_t transparentColor = -1;

		auto extensionBlocks = gifFile->SavedImages[i].ExtensionBlocks;
    for (size_t ext = 0; ext < gifFile->SavedImages[i].ExtensionBlocks.size(); ext++)
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
	if (frames.size() != gifFile->SavedImages.size())
		throw ref new Platform::InvalidArgumentException("image count didnt match frame size");
}

GifLoader::GifLoader(Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream)
{
	auto dataReader = ref new Windows::Storage::Streams::DataReader(inputStream);
	dataReader->InputStreamOptions = Windows::Storage::Streams::InputStreamOptions::ReadAhead;
  _loaderData = { 0, std::vector<uint8_t>(begin(initialData), end(initialData)), dataReader, false, false, false };
  _loaderData.readSome();
	
  try
  {
    _gifFile = std::make_unique<GifFileType<gif_user_data>>(_loaderData);

    _loaderData.buffer.erase(_loaderData.buffer.begin(), _loaderData.buffer.begin() + _loaderData.position);
    _loaderData.position = 0;
    try
    {
      _gifFile->Slurp(_loaderData);
      _isLoaded = true;
      _loaderData.buffer.clear();
      if (_loaderData.reader != nullptr)
        delete _loaderData.reader;
      _loaderData.reader = nullptr;
      _loaderData.position = 0;
    }
    catch (...)
    {
      if (_loaderData.finishedData)
      {
        _isLoaded = true;
        _loaderData.buffer.clear();
        if (_loaderData.reader != nullptr)
          delete _loaderData.reader;
        _loaderData.reader = nullptr;
        _loaderData.position = 0;
      }
    }
    
    uint32_t width = (_gifFile->SWidth % 2) + _gifFile->SWidth;
    uint32_t height = (_gifFile->SHeight % 2) + _gifFile->SHeight;

    _gifFile->SHeight = height;
    _gifFile->SWidth = width;
    loadGifFrames(_gifFile, _frames);
  }
  catch (...)
	{
		throw ref new Platform::InvalidArgumentException("invalid gif asset");
	}
}

GifLoader::~GifLoader() 
{
	if (_loaderData.reader != nullptr)
  {
    delete _loaderData.reader;
    _loaderData.reader = nullptr;
  }
}

bool GifLoader::IsLoaded() const { return _isLoaded || _loaderData.finishedLoad; }
bool GifLoader::LoadMore()
{
  try
  {
    _gifFile->Slurp(_loaderData);
    loadGifFrames(_gifFile, _frames);
    _isLoaded = true;
    _loaderData.buffer.clear();
    if (_loaderData.reader != nullptr)
      delete _loaderData.reader;
    _loaderData.reader = nullptr;
    _loaderData.position = 0;
    return false;
	}
  catch (...)
	{
		loadGifFrames(_gifFile, _frames);
		if (_loaderData.finishedData)
		{
      _isLoaded = true;
			_loaderData.finishedLoad = true;
      _loaderData.buffer.clear();
      if (_loaderData.reader != nullptr)
        delete _loaderData.reader;
      _loaderData.reader = nullptr;
      _loaderData.position = 0;
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
  if (_frames.size() != _gifFile->SavedImages.size())
		throw ref new Platform::InvalidArgumentException("image count didnt match frame size");
	return _frames.size(); 
}

std::unique_ptr<uint32_t[]>& GifLoader::GetFrame(size_t currentIndex, size_t targetIndex)
{
	loadGifFrame(_gifFile, _frames, _renderBuffer, currentIndex, targetIndex);
	return _renderBuffer;
}