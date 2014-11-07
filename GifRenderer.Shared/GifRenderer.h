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

#include "GifLoader.h"
#include "BasicTimer.h"

namespace GifRenderer
{
  using Windows::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource;
  using Windows::Graphics::Display::DisplayInformation;
  using Windows::UI::Xaml::Application;
  using Windows::UI::Xaml::SuspendingEventHandler;
  using Windows::ApplicationModel::SuspendingEventArgs;
  using namespace Microsoft::WRL;
  [Windows::Foundation::Metadata::WebHostHidden]
  public ref class GifRenderer sealed
  {
  private:
    void Update();

    struct VirtualSurfaceUpdatesCallbackNative : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IVirtualSurfaceUpdatesCallbackNative>
    {
    private:
      Platform::WeakReference _rendererReference;
    public:
      VirtualSurfaceUpdatesCallbackNative(GifRenderer^ renderer) : _rendererReference(renderer)
      {
      }

      virtual HRESULT STDMETHODCALLTYPE UpdatesNeeded()
      {
        GifRenderer^ renderer = _rendererReference.Resolve<GifRenderer>();
        if (renderer != nullptr && !renderer->_suspended)
          renderer->Update();
        return S_OK;
      }
    };

    Microsoft::WRL::ComPtr<VirtualSurfaceUpdatesCallbackNative> _callback;
    Microsoft::WRL::ComPtr<IVirtualSurfaceImageSourceNative> _sisNative;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> _d2dContext;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> _renderBitmap;
    GifLoader^ _gifLoader;
    BasicTimer^ _timer;
    VirtualSurfaceImageSource^ _imageSource;
    DisplayInformation^ _displayInfo;
    Windows::Foundation::EventRegistrationToken _suspendingCookie;
    Windows::Foundation::EventRegistrationToken _resumingCookie;
    int	_currentFrame;
    int	_lastFrame;
    bool _startedRendering;
    bool _suspended;
    // Direct3D device
    Microsoft::WRL::ComPtr<ID3D11Device> _d3dDevice;
    // Direct2D object
    Microsoft::WRL::ComPtr<ID2D1Device> _d2dDevice;

    inline void ThrowIfFailed(HRESULT hr)
    {
      if (FAILED(hr))
      {
        throw Platform::Exception::CreateException(hr);
      }
    }

  public:
    GifRenderer(Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream);

    void OnSuspending(Object ^sender, SuspendingEventArgs ^e);

    void OnResuming(Object ^sender, Object ^e);

    property VirtualSurfaceImageSource^ ImageSource
    {
      VirtualSurfaceImageSource^ get();
    }
    virtual ~GifRenderer();

  private:

    void GifRenderer::CreateDeviceResources();
    void GifRenderer::BeginDraw(POINT& offset);
    void GifRenderer::EndDraw();
    bool GifRenderer::Update(float total, float delta);
  };
}