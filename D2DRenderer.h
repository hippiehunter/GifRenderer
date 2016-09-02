#pragma once

#include "IImageRenderer.h"
#include "IImageDecoder.h"

#include <wrl.h>
#include <wrl\client.h>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include "windows.ui.xaml.media.dxinterop.h"

class D2DRenderer : public IImageRenderer
{
private:
	enum FilterState
	{
		WAIT,
		APPLY,
		SCHEDULE
	};
	concurrency::cancellation_token _cancelToken;
	Microsoft::WRL::ComPtr<ImageRendererUpdatesCallbackNative> _callback;
	Microsoft::WRL::ComPtr<IVirtualSurfaceImageSourceNative> _sisNative;
	std::shared_ptr<IImageDecoder> _decoder;
	RECT _lastRequested;
	bool _lastRequestedRequeue;
	int _currentWidth;
	int _currentHeight;
	bool _suspended;
	FilterState _filterState;
	// Direct3D device
	static Microsoft::WRL::ComPtr<ID3D11Device> _d3dDevice;
	// Direct2D object
	static Microsoft::WRL::ComPtr<ID2D1Device> _d2dDevice;
	Microsoft::WRL::ComPtr<ID2D1DeviceContext> _d2dContext;
	bool _sisBound;
	Windows::Graphics::Display::DisplayInformation^ _displayInfo;
	inline void ThrowIfFailed(HRESULT hr)
	{
		if (FAILED(hr))
		{
			throw Platform::Exception::CreateException(hr);
		}
	}
	void CreateDeviceResources();
	void BeginDraw(POINT& offset, RECT& updateNativeRect, Microsoft::WRL::ComPtr<IDXGISurface>& surface);
	void EndDraw();
	bool DrawRequested(POINT offset, RECT requestedRegion, RECT overallRequested, Microsoft::WRL::ComPtr<ID2D1Bitmap1>& renderBitmap);
public:
	D2DRenderer(std::shared_ptr<IImageDecoder> decoder, Microsoft::WRL::ComPtr<IVirtualSurfaceImageSourceNative> sisNative, 
		Windows::Foundation::Size currentSize, concurrency::cancellation_token cancelToken);
	static concurrency::task<std::tuple<std::shared_ptr<IImageRenderer>, Windows::UI::Xaml::Media::ImageSource^>>
		MakeRenderer(std::shared_ptr<IImageDecoder> decoder, Windows::UI::Core::CoreDispatcher^ dispatcher, concurrency::cancellation_token cancelToken);
	virtual Windows::Foundation::Size Size();
	virtual Windows::Foundation::Size MaxSize();
	virtual void Draw();
	virtual void Suspend();
	virtual void Resume();
	virtual void ViewChanged(float zoomFactor);
	virtual ~D2DRenderer() 
	{
		if (_sisNative != nullptr)
		{
			_sisNative->Invalidate(RECT{ 0, 0, _currentWidth, _currentHeight });
		}
	};
};