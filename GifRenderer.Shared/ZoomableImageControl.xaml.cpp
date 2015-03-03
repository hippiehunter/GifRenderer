//
// ZoomableImageControl.xaml.cpp
// Implementation of the ZoomableImageControl class
//

#include "pch.h"
#include "ZoomableImageControl.xaml.h"
#include <robuffer.h>

using namespace GifRenderer;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::Web::Http;
using namespace Windows::Storage::Streams;
using namespace concurrency;

// The User Control item template is documented at http://go.microsoft.com/fwlink/?LinkId=234236

ZoomableImageControl::ZoomableImageControl()
{
    InitializeComponent();
}

bool IsGif(Platform::Array<std::uint8_t>^ data)
{
    return
        data[0] == 0x47 && // G
        data[1] == 0x49 && // I
        data[2] == 0x46 && // F
        data[3] == 0x38 && // 8
        (data[4] == 0x39 || data[4] == 0x37) && // 9 or 7
        data[5] == 0x61;   // a
}

void ::GifRenderer::ZoomableImageControl::ZoomToContent()
{
    // If the image is larger than the screen, zoom it out

    auto imageWidth = image->Width != image->Width ? image->ActualWidth : image->Width;
    auto imageHeight = image->Height != image->Height ? image->ActualHeight : image->Height;

    auto zoomFactor = (float) std::min(scrollViewer->ViewportWidth / imageWidth, scrollViewer->ViewportHeight / imageHeight);
    scrollViewer->MinZoomFactor = std::max(zoomFactor, 0.1f);
    scrollViewer->MaxZoomFactor = 20;
    scrollViewer->ChangeView(nullptr, nullptr, zoomFactor, true);
}

void ::GifRenderer::ZoomableImageControl::AfterInitialLoad(Platform::Array<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream)
{
    if (initialData != nullptr && inputStream != nullptr)
    {
        _loadedByteCount = initialData->Length;
        std::function<void(int)> loadCallback([=](int loadedBytes)
        {
            if (_expectedByteCount > 0)
            {
                _loadedByteCount += loadedBytes;
                double loadPercent = static_cast<double>(_loadedByteCount) / static_cast<double>(_expectedByteCount);
                double Angle = 2 * 3.14159265 * loadPercent;

                double X = 12 - std::sin(Angle) * 12;
                double Y = 12 + std::cos(Angle) * 12;

                if (loadPercent > 0 && (int)X == 12 && (int)Y == 24)
                    X += 0.01; // Never make the end the same as the start!

                TheSegment->IsLargeArc = Angle >= 3.14159265;
                TheSegment->Point = Point(X, Y);
            }
        });
        std::function<void(Platform::String^)> errorHandler([=](Platform::String^ errorText)
        {
            progressStack->Opacity = 1.0;
            retryButton->Opacity = 1.0;
            loadText->Text = errorText;
        });

        if (IsGif(initialData))
        {
            _gifRenderer = ref new ::GifRenderer::GifRenderer(initialData, inputStream, errorHandler, loadCallback);
            image->Source = _gifRenderer->ImageSource;
        }
        else
        {
            std::function<void(int, int)> fn([=](int width, int height)
            {
                if (_virtualSurfaceRenderer != nullptr)
                {
                    image->Height = height;
                    image->Width = width;
                    image->Source = _virtualSurfaceRenderer->ImageSource;
                }
            });
            
            _virtualSurfaceRenderer = ref new VirtualSurfaceRenderer(initialData, _targetUrl, inputStream, fn, loadCallback, errorHandler, cancelSource.get_token());
        }
    }
}

void ::GifRenderer::ZoomableImageControl::UserControl_DataContextChanged(Windows::UI::Xaml::FrameworkElement^ sender, Windows::UI::Xaml::DataContextChangedEventArgs^ args)
{
    auto targetUrl = dynamic_cast<String^>(args->NewValue);
    if (_targetUrl != targetUrl)
    {
        if (_targetUrl != nullptr)
        {
            cancelSource.cancel();
            cancelSource = cancellation_token_source();
        }
        _targetUrl = targetUrl;
        _initialSizeChanged = false;
        _virtualSurfaceRenderer = nullptr;
        _gifRenderer = nullptr;

        if (_targetUrl != nullptr)
            Load();
    }
}

void ::GifRenderer::ZoomableImageControl::image_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e)
{
    if (!_initialSizeChanged)
    {
        _initialSizeChanged = true;
        ZoomToContent();
        FadeIn->Begin();
    }
}

void ::GifRenderer::ZoomableImageControl::UserControl_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    cancelSource.cancel();
    cancelSource = cancellation_token_source();
    _virtualSurfaceRenderer = nullptr;
    _gifRenderer = nullptr;
}

void ::GifRenderer::ZoomableImageControl::scrollViewer_ViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e)
{
    if (_virtualSurfaceRenderer != nullptr)
        _virtualSurfaceRenderer->ViewChanged(sender, e);
}

void ::GifRenderer::ZoomableImageControl::Retry_Clicked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    _initialSizeChanged = false;
    _virtualSurfaceRenderer = nullptr;
    _gifRenderer = nullptr;
    Load();
}

void ::GifRenderer::ZoomableImageControl::Load()
{
    auto client = ref new Windows::Web::Http::HttpClient();
    create_task(client->GetAsync(ref new Uri(_targetUrl), HttpCompletionOption::ResponseHeadersRead), cancelSource.get_token())
        .then([=](task<HttpResponseMessage^> responseTask)
    {
        try
        {
            auto contentLengthBox = responseTask.get()->Content->Headers->ContentLength;
            _expectedByteCount = contentLengthBox != nullptr ? contentLengthBox->Value : 0;
            create_task(responseTask.get()->Content->ReadAsInputStreamAsync(), cancelSource.get_token())
                .then([=](task<IInputStream^> responseStreamTask)
            {
                try
                {
                    create_task(responseStreamTask.get()->ReadAsync(ref new Buffer(4096), 4096, InputStreamOptions::ReadAhead), cancelSource.get_token())
                        .then([=](task<IBuffer^> bufferTask)
                    {
                        try
                        {
                            if (bufferTask.get()->Length == 0)
                                throw ref new Exception(E_FAIL, L"failed to read initial bytes of image");

                            ComPtr<Windows::Storage::Streams::IBufferByteAccess> pBufferByteAccess;
                            ComPtr<IUnknown> pBuffer((IUnknown*)bufferTask.get());
                            pBuffer.As(&pBufferByteAccess);
                            byte* bufferData;
                            pBufferByteAccess->Buffer(&bufferData);

                            auto bufferBytes = ref new Platform::Array<std::uint8_t>(bufferTask.get()->Length);
                            memcpy(bufferBytes->Data, bufferData, bufferBytes->Length);
                            AfterInitialLoad(bufferBytes, responseStreamTask.get());
                        }
                        catch (Exception^ ex)
                        {
                            ErrorHandler(ex->Message);
                        }
                    });
                }
                catch (Exception^ ex)
                {
                    ErrorHandler(ex->Message);
                }
            });
        }
        catch (Exception^ ex)
        {
            ErrorHandler(ex->Message);
        }
    });
}

void ::GifRenderer::ZoomableImageControl::ErrorHandler(Platform::String^ errorText)
{
    progressStack->Opacity = 1.0;
    retryButton->Opacity = 1.0;
    loadText->Text = errorText;
}