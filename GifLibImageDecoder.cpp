#include "pch.h"
#include "GiflibImageDecoder.h"
#include "task_helper.h"
#include "ResourceLoader.h"

using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace std;
using namespace concurrency;
using namespace task_helper;

Size GiflibImageDecoder::MaxSize()
{
	return Size(static_cast<float>(_gifFile->SWidth), static_cast<float>(_gifFile->SHeight));
}

Size GiflibImageDecoder::DefaultSize()
{
	return Size(static_cast<float>(_gifFile->SWidth), static_cast<float>(_gifFile->SHeight));
}

void GiflibImageDecoder::RenderSize(Size size)
{
	_renderSize = size;
}

int gif_user_data::read(GifByteType * buf, unsigned int plength)
{
  auto targetEnd = position + plength;

  if (targetEnd > length)
    return 0;
  if (position == length)
    return 0;

  if (targetEnd == length)
    plength = length - position;

  unsigned int startBufferIndex = 0;
  unsigned int endBufferIndex = 0;
  unsigned int currentCopied = 0;
  for (unsigned int foundPos = 0;endBufferIndex < buffer.size() && foundPos < targetEnd; ) 
  {
    foundPos += buffer[endBufferIndex]->Length;
    if (buffer.size() > endBufferIndex && foundPos < targetEnd)
      endBufferIndex++;

    if (foundPos < position)
      startBufferIndex++;
  }

  if (endBufferIndex >= buffer.size())
    return 0;
  //need to deal with block #2
  int targetBufferStartPos = position;
  for (unsigned int currentIndex = 0; currentIndex <= endBufferIndex; currentIndex++)
  {
    if (currentIndex >= startBufferIndex)
    {
      ResourceLoader::GetBytesFromBuffer(buffer[currentIndex], [&](uint8_t* bytes, uint32_t count)
      {
        unsigned int copyLength = 0;
        copyLength = ((((plength) < (count - targetBufferStartPos)) ? (plength) : (count - targetBufferStartPos))) - currentCopied;
        memcpy(buf + currentCopied, bytes + targetBufferStartPos, copyLength);
        currentCopied += copyLength;
      });
      targetBufferStartPos = 0;
    }
    else
    {
      targetBufferStartPos -= buffer[currentIndex]->Length;
    }
    
  }

  if (currentCopied != plength)
    throw std::runtime_error("read failed");

  position += plength;

  return plength;
}

void gif_user_data::addData(IBuffer^ pBuffer)
{
  for (auto buf : buffer)
  {
      if (pBuffer == buf)
          return; //dirty way to ensure no duplicate buffers
  }
  length += pBuffer->Length;
  buffer.push_back(pBuffer);
}


//handler is passed the decoded bytes and a bool to indicate whether or not to put it back in the queue to render again
task<Windows::Foundation::Rect> GiflibImageDecoder::DecodeRectangleAsync(Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext)
{
	throw ref new Platform::NotImplementedException();
}

bool GiflibImageDecoder::CanDecode(Windows::Foundation::Rect rect)
{
	return true;
}

bool GiflibImageDecoder::Update(float total, float delta)
{
	double msDelta = ((double)total) * 1000;
	double accountedFor = 0;
	size_t i = 0;
	for (; accountedFor < msDelta; i++)
	{
		if (i >= _frames.size())
		{
			if (_isLoaded)
				i = 0;
			else
			{
				i = _frames.size() - 1;
				break;
			}
		}
		accountedFor += GetFrameDelay(i);
	}
	auto newFrame = std::max<int>((int)i - 1, 0);
	if (newFrame != _currentFrame || _currentFrame == 0)
	{
		_currentFrame = newFrame;
		_startedRendering = true;
		return true;
	}
	else
	{
		if (!_startedRendering)
		{
			_startedRendering = true;
			return true;
		}
		else
			return true;
	}
}

uint32_t GiflibImageDecoder::GetFrameDelay(size_t index) const { return _frames[index].delay; }
size_t GiflibImageDecoder::FrameCount() const
{
	if (_frames.size() != _decodedImages.size())
		throw ref new Platform::InvalidArgumentException("image count didnt match frame size");
	return _frames.size();
}

static void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw Platform::Exception::CreateException(hr);
	}
}

Rect GiflibImageDecoder::DecodeRectangle(Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext, Microsoft::WRL::ComPtr<ID2D1Bitmap1>& copyDestination, bool& requeue)
{
	std::lock_guard<std::mutex> readGuard(_frameMutex);
	requeue = true;
	if (_decodedImages.size() > 0)
	{
		if (!_startedRendering)
			_timer->Reset();
		else
			_timer->Update();

		if (Update(_timer->Total, _timer->Delta))
		{
			LoadGifFrame(_gifFile, _frames, _renderBuffer, _lastFrame, _currentFrame);
			_lastFrame = _currentFrame;
			auto displayInfo = Windows::Graphics::Display::DisplayInformation::GetForCurrentView();
			D2D1_SIZE_U size = { static_cast<uint32_t>(_gifFile->SWidth), static_cast<uint32_t>(_gifFile->SHeight) };
			D2D1_BITMAP_PROPERTIES1 properties;
			memset(&properties, 0, sizeof(D2D1_BITMAP_PROPERTIES1));
			properties.dpiX = displayInfo->RawDpiX;
			properties.dpiY = displayInfo->RawDpiY;
			properties.pixelFormat = D2D1_PIXEL_FORMAT{ DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE };
			ThrowIfFailed(d2dContext->CreateBitmap(size, reinterpret_cast<const void*>(_renderBuffer.get()), _gifFile->SWidth * 4, properties, copyDestination.ReleaseAndGetAddressOf()));
			return Rect(0, 0, static_cast<float>(_gifFile->SWidth), static_cast<float>(_gifFile->SHeight));
		}
	}
	return Rect();
}

void GiflibImageDecoder::Suspend()
{

}

void GiflibImageDecoder::Resume()
{

}

void GiflibImageDecoder::LoadHandler(IBuffer^ buffer, bool finished, uint32_t expectedSize)
{
	try
	{
		std::lock_guard<std::mutex> readGuard(_loadMutex);

    _loaderData.addData(buffer);

		if (_loaderData.buffer.size() == 0)
			return;

		try
		{
			_gifFile->Slurp(_loaderData);
			_isLoaded = true;
			_loaderData.buffer.clear();
		}
		catch (...)
		{
			if (finished)
			{
				_isLoaded = true;
				_loaderData.finishedLoad = true;
				_loaderData.buffer.clear();
			}
		}

		LoadGifFrames(_gifFile, _frames);

		if (_frames.size() > 0)
			_readySource.set();
	}
	catch (Platform::Exception^ ex)
	{
		OutputDebugString(ex->Message->Data());
	}
	catch (...)
	{
		OutputDebugString(L"unknown error handling gif load");
	}
}


void GiflibImageDecoder::MapRasterBits(uint8_t* rasterBits, std::unique_ptr<uint32_t[]>& targetFrame, ColorMapObject& colorMap, int top, int left, int bottom, int right, int width, int32_t transparencyColor)
{
	int i = 0;
	for (int y = top; y < bottom; y++)
	{
		for (int x = left; x < right; x++)
		{
			int offset = y * width + x;
			uint8_t index = rasterBits[i];

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

GiflibImageDecoder::GiflibImageDecoder(IBuffer^ initialBuffer, cancellation_token canceledToken) : _loaderData(canceledToken), _cancelToken(canceledToken)
{
	_currentFrame = 0;
	_lastFrame = 0;
	_loaderData.init(0, initialBuffer);
	_gifFile = make_unique<GifFileType<gif_user_data>>(_loaderData);
	_renderBuffer = nullptr;
	_loaderData;
	_isLoaded = false;
	_startedRendering = false;
	_timer = ref new BasicTimer();
}

std::shared_ptr<IImageDecoder> GiflibImageDecoder::MakeImageDecoder(IBuffer^ initialBuffer, cancellation_token canceledToken)
{
	return std::dynamic_pointer_cast<IImageDecoder>(make_shared<GiflibImageDecoder>(initialBuffer, canceledToken));
}