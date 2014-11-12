//
// ZoomableImageControl.xaml.cpp
// Implementation of the ZoomableImageControl class
//

#include "pch.h"
#include "ZoomableImageControl.xaml.h"

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

// The User Control item template is documented at http://go.microsoft.com/fwlink/?LinkId=234236

ZoomableImageControl::ZoomableImageControl()
{
	InitializeComponent();
}

bool IsGif(Windows::Foundation::Collections::IVector<std::uint8_t>^ data)
{
	return
		data->GetAt(0) == 0x47 && // G
		data->GetAt(1) == 0x49 && // I
		data->GetAt(2) == 0x46 && // F
		data->GetAt(3) == 0x38 && // 8
		(data->GetAt(4) == 0x39 || data->GetAt(4) == 0x37) && // 9 or 7
		data->GetAt(5) == 0x61;   // a
}

void ::GifRenderer::ZoomableImageControl::ZoomToContent()
{
	// If the image is larger than the screen, zoom it out
	auto zoomFactor = (float) std::min(scrollViewer->ViewportWidth / image->ActualWidth, scrollViewer->ViewportHeight / image->ActualHeight);
	scrollViewer->MinZoomFactor = std::max(zoomFactor, 0.1f);
	scrollViewer->MaxZoomFactor = 20;
	scrollViewer->ChangeView(nullptr, nullptr, zoomFactor, true);
}

void ::GifRenderer::ZoomableImageControl::UserControl_DataContextChanged(Windows::UI::Xaml::FrameworkElement^ sender, Windows::UI::Xaml::DataContextChangedEventArgs^ args)
{
	_initialSizeChanged = false;
	_virtualSurfaceRenderer = nullptr;
	_gifRenderer = nullptr;

	auto gifPayLoad = dynamic_cast<GifPayload^>(args->NewValue);
	if (gifPayLoad != nullptr && gifPayLoad->initialData != nullptr && gifPayLoad->inputStream != nullptr)
	{
		if (IsGif(gifPayLoad->initialData))
		{
			_gifRenderer = ref new ::GifRenderer::GifRenderer(gifPayLoad->initialData, gifPayLoad->inputStream);
			image->Source = _gifRenderer->ImageSource;
      FadeIn->Begin();
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
          FadeIn->Begin();
				}
			});

      _loadedByteCount = gifPayLoad->initialData->Size;
      std::function<void(int)> loadCallback([=](int loadedBytes)
      {
        if (gifPayLoad->expectedSize > 0)
        {
          _loadedByteCount += loadedBytes;
          double loadPercent = static_cast<double>(_loadedByteCount) / static_cast<double>(gifPayLoad->expectedSize);
          double Angle = 2 * 3.14159265 * loadPercent;

          double X = 12 - std::sin(Angle) * 12;
          double Y = 12 + std::cos(Angle) * 12;

          if (loadPercent > 0 && (int)X == 12 && (int)Y == 24)
            X += 0.01; // Never make the end the same as the start!

          TheSegment->IsLargeArc = Angle >= 3.14159265;
          TheSegment->Point = Point(X, Y);
        }
      });
      _virtualSurfaceRenderer = ref new VirtualSurfaceRenderer(gifPayLoad->initialData, gifPayLoad->inputStream, fn, loadCallback);
		}
	}
}

void ::GifRenderer::ZoomableImageControl::image_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e)
{
	if (!_initialSizeChanged)
	{
		_initialSizeChanged = true;
		ZoomToContent();
	}
}

void ::GifRenderer::ZoomableImageControl::UserControl_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	_virtualSurfaceRenderer = nullptr;
	_gifRenderer = nullptr;
}

void ::GifRenderer::ZoomableImageControl::scrollViewer_ViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e)
{
	if (_virtualSurfaceRenderer != nullptr)
		_virtualSurfaceRenderer->ViewChanged(sender, e);
}
