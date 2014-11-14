//
// ZoomableImageControl.xaml.h
// Declaration of the ZoomableImageControl class
//

#pragma once

#include "ZoomableImageControl.g.h"
#include "VirtualSurfaceRenderer.h"
#include "GifRenderer.h"

namespace GifRenderer
{
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class ZoomableImageControl sealed
	{
	public:
		ZoomableImageControl();
	private:
		::GifRenderer::GifRenderer^ _gifRenderer;
		::GifRenderer::VirtualSurfaceRenderer^ _virtualSurfaceRenderer;
		bool _initialSizeChanged;
    int _loadedByteCount;

		void UserControl_DataContextChanged(Windows::UI::Xaml::FrameworkElement^ sender, Windows::UI::Xaml::DataContextChangedEventArgs^ args);
		void ZoomToContent();
		void UserControl_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void Retry_Clicked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void image_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
		void scrollViewer_ViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e);
	};
}
