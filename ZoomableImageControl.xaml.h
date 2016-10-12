//
// ZoomableImageControl.xaml.h
// Declaration of the ZoomableImageControl class
//

#pragma once

#include "ZoomableImageControl.g.h"
#include "ImageFactory.h"
#include "IImageRenderer.h"
namespace GifRenderer
{
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class ZoomableImageControl sealed
	{
	public:
		ZoomableImageControl();
	private:
		concurrency::cancellation_token_source cancelSource;
		std::shared_ptr<IImageRenderer> _renderer;
		bool _initialSizeChanged;
		int _loadedByteCount;
		int _expectedByteCount;
		bool _canceled;
		int _height;
		int _width;
		float _currentZoom;
		Platform::String^ _targetUrl;

		void UserControl_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void Retry_Clicked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void scrollViewer_ViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e);

		void Load();
		void ErrorHandler(Platform::String^ errorText);
		void scrollViewer_DoubleTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::DoubleTappedRoutedEventArgs^ e);
	private:
		static void OnUrlChanged(DependencyObject^ d, Windows::UI::Xaml::DependencyPropertyChangedEventArgs^ e);
		static Windows::UI::Xaml::DependencyProperty^ _urlProperty;
        static Windows::UI::Xaml::DependencyProperty^ _resizeToFitHorizontalProperty;
	public:
		property Platform::String^ Url
		{
			Platform::String^ get() { return (Platform::String^)GetValue(_urlProperty); }
			void set(Platform::String^ value) { SetValue(_urlProperty, value); }
		}

		static property Windows::UI::Xaml::DependencyProperty^ UrlProperty
		{
			Windows::UI::Xaml::DependencyProperty^ get() { return _urlProperty; }
		}

        property bool ResizeToFitHorizontal
        {
            bool get() { return (bool)GetValue(_resizeToFitHorizontalProperty); }
            void set(bool value) { SetValue(_resizeToFitHorizontalProperty, value); }
        }

        static property Windows::UI::Xaml::DependencyProperty^ ResizeToFitHorizontalProperty
        {
            Windows::UI::Xaml::DependencyProperty^ get() { return _resizeToFitHorizontalProperty; }
        }
	};
}
