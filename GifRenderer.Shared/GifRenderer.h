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

#include "giflibpp.h"
#include "BasicTimer.h"

namespace GifRenderer
{
    using Windows::UI::Xaml::Media::Imaging::VirtualSurfaceImageSource;
    using Windows::Graphics::Display::DisplayInformation;
    using Windows::UI::Xaml::Application;
    using Windows::UI::Xaml::SuspendingEventHandler;
    using Windows::ApplicationModel::SuspendingEventArgs;
    using namespace Microsoft::WRL;

    enum DISPOSAL_METHODS
    {
        DM_UNDEFINED = 0,
        DM_NONE = 1,
        DM_BACKGROUND = 2,
        DM_PREVIOUS = 3
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
        unsigned int position;
        std::vector<uint8_t> buffer;
        Windows::Storage::Streams::IDataReader^ reader;
        bool finishedLoad;
        bool finishedReader;
        bool finishedData;
		concurrency::cancellation_token cancelToken;
        std::function<void(int)> loadCallback;
        std::function<void(Platform::String^)> errorHandler;
        int read(GifByteType * buf, unsigned int length);
		gif_user_data(concurrency::cancellation_token pcancelToken) : cancelToken(pcancelToken)
		{
			finishedLoad = false;
			finishedReader = false;
			finishedData = false;
		}

		void init(unsigned int pposition, std::vector<uint8_t> pbuffer, Windows::Storage::Streams::IDataReader^ preader, std::function<void(int)> ploadCallback, std::function<void(Platform::String^)> perrorHandler)
		{
			position = pposition;
			buffer = pbuffer;
			reader = preader;
			loadCallback = ploadCallback;
			errorHandler = perrorHandler;
			finishedLoad = false;
			finishedReader = false;
			finishedData = false;
		}

        void revert()
        {
            position = 0;
        }
        void checkpoint()
        {
            buffer.erase(buffer.begin(), buffer.begin() + position);
            position = 0;
        }
        void readSome();
    };

    ref class GifRenderer sealed
    {
    private:
        void Update();

        struct VirtualSurfaceUpdatesCallbackNative : public Microsoft::WRL::RuntimeClass < Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IVirtualSurfaceUpdatesCallbackNative >
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
        BasicTimer^ _timer;
        VirtualSurfaceImageSource^ _imageSource;
        DisplayInformation^ _displayInfo;
        Windows::Foundation::EventRegistrationToken _suspendingCookie;
        Windows::Foundation::EventRegistrationToken _resumingCookie;
        int	_currentFrame;
        int	_lastFrame;
        bool _startedRendering;
        bool _suspended;
		concurrency::cancellation_token _cancelToken;

        std::function<void(Platform::String^)> _errorHandler;
        std::function<void(int)> _loadCallback;

        gif_user_data _loaderData;
        bool _isLoaded;
        std::unique_ptr<GifFileType<gif_user_data>> _gifFile;
        std::vector<GifFrame> _frames;
        std::unique_ptr<uint32_t[]> _renderBuffer;

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

    internal:
        GifRenderer(Platform::Array<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream,
            std::function<void(Platform::String^)>& errorHandler, std::function<void(int)>& frameLoadedCallback, concurrency::cancellation_token cancelToken);

    public:
        property VirtualSurfaceImageSource^ ImageSource
        {
            VirtualSurfaceImageSource^ get();
        }
        virtual ~GifRenderer();
        
    private:
        void OnSuspending(Object ^sender, SuspendingEventArgs ^e);
        void OnResuming(Object ^sender, Object ^e);

        void InitialLoad(Platform::Array<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream);
        bool IsLoaded() const;
        bool LoadMore();
        uint32_t Height() const;
        uint32_t Width() const;
        size_t FrameCount() const;
        uint32_t GetFrameDelay(size_t index) const;
        std::unique_ptr<uint32_t[]>& GetFrame(size_t currentIndex, size_t targetIndex);

        void GifRenderer::CreateDeviceResources();
        void GifRenderer::BeginDraw(POINT& offset);
        void GifRenderer::EndDraw();
        bool GifRenderer::Update(float total, float delta);
    };
}