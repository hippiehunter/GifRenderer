//
// ZoomableImageControl.xaml.cpp
// Implementation of the ZoomableImageControl class
//

#include "pch.h"
#include "ZoomableImageControl.xaml.h"

#include <robuffer.h>
#include <chrono>
#include "task_helper.h"

using namespace GifRenderer;
using namespace task_helper;
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

				if (loadPercent > 0 && (int) X == 12 && (int) Y == 24)
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

		std::function<void(int, int, Windows::UI::Xaml::Media::ImageSource^)> fn([=](int width, int height, Windows::UI::Xaml::Media::ImageSource^ source)
		{
			image->Height = _height = height;
			image->Width = _width = width;
			_initialSizeChanged = true;

			delayed_ui_task(std::chrono::milliseconds(10), [=]()
			{
				if (!_canceled)
				{
					// If the image is larger than the screen, zoom it out
					image->Source = source;
					auto zoomFactor = (float)std::min(scrollViewer->ViewportWidth / width, scrollViewer->ViewportHeight / height);
					scrollViewer->MinZoomFactor = std::max(zoomFactor, 0.1f);
					scrollViewer->MaxZoomFactor = 20;
					scrollViewer->ChangeView(nullptr, nullptr, zoomFactor, true);
					FadeIn->Begin();
				}
			});
		});

		if (IsGif(initialData))
		{
			_gifRenderer = ref new ::GifRenderer::GifRenderer(initialData, inputStream, errorHandler, loadCallback, fn, cancelSource.get_token());
		}
		else
		{
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
			image->Source = nullptr;
		}
		_targetUrl = targetUrl;
		_initialSizeChanged = false;
		_virtualSurfaceRenderer = nullptr;
		_gifRenderer = nullptr;

		if (_targetUrl != nullptr)
			Load();
	}
}

void ::GifRenderer::ZoomableImageControl::UserControl_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	cancelSource.cancel();
	cancelSource = cancellation_token_source();
	_virtualSurfaceRenderer = nullptr;
	_gifRenderer = nullptr;
	image->Source = nullptr;
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
	scrollViewer->Visibility = Windows::UI::Xaml::Visibility::Visible;
	Load();
}

void ::GifRenderer::ZoomableImageControl::Load()
{
	auto errorHandler = [=](Exception^ ex)
	{
		ErrorHandler(ex->Message);
		return task_from_result();
	};
	auto client = ref new Windows::Web::Http::HttpClient();
	auto cancelToken = cancelSource.get_token();
	finish_task(continue_task(client->GetAsync(ref new Uri(_targetUrl), HttpCompletionOption::ResponseHeadersRead),
		[=](HttpResponseMessage^ response)
	{
		auto contentLengthBox = response->Content->Headers->ContentLength;
		_expectedByteCount = contentLengthBox != nullptr ? contentLengthBox->Value : 0;
		return continue_task(response->Content->ReadAsInputStreamAsync(),
							 [=](IInputStream^ responseStream)
		{
			return continue_task(responseStream->ReadAsync(ref new Buffer(4096), 4096, InputStreamOptions::ReadAhead),
								 [=](IBuffer^ buffer)
			{
				if (buffer->Length == 0)
					return task_from_exception<void>(ref new Exception(E_FAIL, L"failed to read initial bytes of image"));

				ComPtr<Windows::Storage::Streams::IBufferByteAccess> pBufferByteAccess;
				ComPtr<IUnknown> pBuffer((IUnknown*) buffer);
				pBuffer.As(&pBufferByteAccess);
				byte* bufferData;
				pBufferByteAccess->Buffer(&bufferData);

				auto bufferBytes = ref new Platform::Array<std::uint8_t>(buffer->Length);
				memcpy(bufferBytes->Data, bufferData, bufferBytes->Length);
				AfterInitialLoad(bufferBytes, responseStream);
				return task_from_result();
			}, errorHandler, cancelToken);
		}, errorHandler, cancelToken);
	}, errorHandler, cancelToken), errorHandler);
}

void ::GifRenderer::ZoomableImageControl::ErrorHandler(Platform::String^ errorText)
{
	progressStack->Opacity = 1.0;
	retryButton->Opacity = 1.0;
	loadText->Text = errorText;
	scrollViewer->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
}

void ::GifRenderer::ZoomableImageControl::scrollViewer_DoubleTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::DoubleTappedRoutedEventArgs^ e)
{
	if (_initialSizeChanged)
	{
		delayed_ui_task(std::chrono::milliseconds(100), [=]()
		{
			auto point = e->GetPosition(this);

			auto viewportWidth = scrollViewer->ViewportWidth;
			auto viewportHeight = scrollViewer->ViewportHeight;
			auto scrollXOffset = scrollViewer->HorizontalOffset;
			auto scrollYOffset = scrollViewer->VerticalOffset;
			auto scrollZoom = scrollViewer->ZoomFactor;

			auto baseZoomFactor = (float)std::min(viewportWidth / _width, viewportHeight / _height);

			if (scrollViewer->ZoomFactor > baseZoomFactor * 2.0)
				scrollViewer->ChangeView(nullptr, nullptr, baseZoomFactor, false);
			else
				scrollViewer->ChangeView(static_cast<double>(point.X) + scrollXOffset, static_cast<double>(point.Y) + scrollYOffset, scrollZoom * 1.7f, false);
		});
	}
}
