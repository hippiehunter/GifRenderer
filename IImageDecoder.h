#pragma once

#include <ppltasks.h>
#include <d2d1_1.h>

class IImageDecoder
{
protected:
	uint32_t _maxRenderDimension;
	concurrency::task_completion_event<void> _readySource;
	IImageDecoder() 
	{ 
#if WINDOWS_PHONE_APP
		_maxRenderDimension = Windows::System::MemoryManager::AppMemoryUsageLimit > 300 * 1024 * 1024 ? 1024.0 : 400.0;
#elif UWP
		_maxRenderDimension = Windows::System::MemoryManager::AppMemoryUsageLimit > 300 * 1024 * 1024 ? 1024.0 : 400.0;
#else
		_maxRenderDimension = 2048;
#endif
	}
public:
	virtual void LoadHandler(Windows::Storage::Streams::IBuffer^ buffer, bool finished, uint32_t expectedSize) { /*default to doing nothing */ };
	virtual concurrency::task<void> Ready() { return concurrency::task<void>(_readySource); }
	virtual Windows::Foundation::Size MaxSize() = 0;
	virtual Windows::Foundation::Size DefaultSize() = 0;
	virtual void RenderSize(Windows::Foundation::Size size) = 0;
	//handler is the rendered rect to invalidate
	virtual concurrency::task<Windows::Foundation::Rect> DecodeRectangleAsync(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext) = 0;
	virtual bool CanDecode(Windows::Foundation::Rect rect) = 0;
	virtual Windows::Foundation::Rect DecodeRectangle(Windows::Foundation::Rect requestedRect, Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext, Microsoft::WRL::ComPtr<ID2D1Bitmap1>& copyDestination, bool& requeue) = 0;
	virtual void Suspend() = 0;
	virtual void Resume() = 0;
	virtual ~IImageDecoder() {}
};
