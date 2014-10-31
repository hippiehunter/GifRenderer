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

  [Windows::UI::Xaml::Data::Bindable]
  public ref class VirtualSurfaceRenderer sealed : public Windows::UI::Xaml::Data::INotifyPropertyChanged
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
        VirtualSurfaceRenderer^ renderer = _rendererReference.Resolve<VirtualSurfaceRenderer>();
        if (renderer != nullptr && !renderer->_suspended)
          renderer->Update();
        return S_OK;
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
	Microsoft::WRL::ComPtr<ID2D1Bitmap> _specificBitmap;
    Windows::Foundation::Size _imageSize;
    Nokia::Graphics::Imaging::RandomAccessStreamImageSource^ _streamImageSource;
	float _overallImageScale;
	float _specificImageScale;
	RECT _specificRender;
    Nokia::Graphics::Imaging::Bitmap^ _overallBitmap;
    Object^ _imageSource;
    DisplayInformation^ _displayInfo;
    Windows::Foundation::EventRegistrationToken _suspendingCookie;
    Windows::Foundation::EventRegistrationToken _resumingCookie;
    bool _needsRender;
    bool _suspended;
    FilterState _filterState;
    // Direct3D device
    Microsoft::WRL::ComPtr<ID3D11Device> _d3dDevice;
    // Direct2D object
    Microsoft::WRL::ComPtr<ID2D1Device> _d2dDevice;

    concurrency::task<Nokia::Graphics::Imaging::RandomAccessStreamImageSource^> VirtualSurfaceRenderer::GetImageSource(
      Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData,
      Windows::Storage::Streams::IInputStream^ inputStream);

    concurrency::task<void> LoadSome(Windows::Storage::Streams::IInputStream^ inputStream, Windows::Storage::Streams::DataWriter^ target);

    void OnSuspending(Object ^sender, SuspendingEventArgs ^e);

    void OnResuming(Object ^sender, Object ^e);

   
    void CreateDeviceResources();
    void BeginDraw(POINT& offset, RECT& updateNativeRect);
    void EndDraw();
    bool Update(float total, float delta);

    inline void ThrowIfFailed(HRESULT hr)
    {
      if (FAILED(hr))
      {
        throw Platform::Exception::CreateException(hr);
      }
    }

  public:
	virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged;
    VirtualSurfaceRenderer(Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream);
    property Object^ ImageSource
    {
		Object^ get();
    }
    virtual ~VirtualSurfaceRenderer();
  };
}