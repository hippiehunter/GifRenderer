#pragma once

#include <cstdint>
#include <wrl.h>
#include <wrl\client.h>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include "windows.ui.xaml.media.dxinterop.h"
#include "BasicTimer.h"
#include <ppltasks.h>

namespace GifRenderer
{
  using Windows::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource;
  using Windows::Graphics::Display::DisplayInformation;
  using Windows::UI::Xaml::Application;
  using Windows::UI::Xaml::SuspendingEventHandler;
  using Windows::ApplicationModel::SuspendingEventArgs;
  using namespace Microsoft::WRL;

  [Windows::Foundation::Metadata::WebHostHidden]
  public ref class VirtualSurfaceRenderer sealed
  {
  private:
    void Update();

    struct VirtualSurfaceUpdatesCallbackNative : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IVirtualSurfaceUpdatesCallbackNative>
    {
    private:
      Platform::WeakReference _rendererReference;
	public:
      VirtualSurfaceUpdatesCallbackNative(VirtualSurfaceRenderer^ renderer) : _rendererReference(renderer)
      {
      }
      virtual HRESULT STDMETHODCALLTYPE UpdatesNeeded()
      {
		  try
		  {
			  VirtualSurfaceRenderer^ renderer = _rendererReference.Resolve<VirtualSurfaceRenderer>();
			  if (renderer != nullptr && !renderer->_suspended)
			  {
				  renderer->Update();
				  return S_OK;
			  }
		  }
		  catch (...) { }
		  return E_FAIL;
      }
    };

    enum FilterState
    {
      WAIT,
      APPLY,
      SCHEDULE
    };

    Microsoft::WRL::ComPtr<VirtualSurfaceUpdatesCallbackNative> _callback;
    Microsoft::WRL::ComPtr<IVirtualSurfaceImageSourceNative> _sisNative;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> _d2dContext;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> _renderBitmap;
	Microsoft::WRL::ComPtr<ID2D1Bitmap> _originalBitmap;
    Windows::Foundation::Size _imageSize;
	std::function<void(int,int)> _updateCallback;
	Windows::Storage::Streams::IRandomAccessStream^ _fileStream;
	float _overallImageScale;
	float _specificImageScale;
	RECT _specificRender;
	RECT _lastRender;
	int _currentWidth;
	int _currentHeight;
	float _maxRenderDimension;
	Windows::UI::Xaml::Media::ImageSource^ _imageSource;
    DisplayInformation^ _displayInfo;
    Windows::Foundation::EventRegistrationToken _suspendingCookie;
    Windows::Foundation::EventRegistrationToken _resumingCookie;
    bool _suspended;
    FilterState _filterState;
    // Direct3D device
    Microsoft::WRL::ComPtr<ID3D11Device> _d3dDevice;
    // Direct2D object
    Microsoft::WRL::ComPtr<ID2D1Device> _d2dDevice;

	concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> VirtualSurfaceRenderer::GetImageSource(
      Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData,
      Windows::Storage::Streams::IInputStream^ inputStream);

    concurrency::task<void> LoadSome(Windows::Storage::Streams::IInputStream^ inputStream, Windows::Storage::Streams::DataWriter^ target);
    void OnSuspending(Object ^sender, SuspendingEventArgs ^e);
    void OnResuming(Object ^sender, Object ^e);
    void CreateDeviceResources();
    void BeginDraw(POINT& offset, RECT& updateNativeRect);
	bool DrawRequested(POINT offset, RECT requestedRegion, RECT overallRequested);
    void EndDraw();

    inline void ThrowIfFailed(HRESULT hr)
    {
      if (FAILED(hr))
      {
        throw Platform::Exception::CreateException(hr);
      }
    }

  internal:
	  VirtualSurfaceRenderer(Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream, std::function<void(int,int)>& fn);
	  void ViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e);

  public:
    property Windows::UI::Xaml::Media::ImageSource^ ImageSource
    {
		Windows::UI::Xaml::Media::ImageSource^ get();
    }
	virtual ~VirtualSurfaceRenderer();
  };
}