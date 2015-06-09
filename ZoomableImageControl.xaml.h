﻿//
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

		void UserControl_DataContextChanged(Windows::UI::Xaml::FrameworkElement^ sender, Windows::UI::Xaml::DataContextChangedEventArgs^ args);
		void ZoomToContent();
		void UserControl_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void Retry_Clicked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void scrollViewer_ViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e);

		void Load();
		void AfterInitialLoad(Platform::Array<uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream);
		void ErrorHandler(Platform::String^ errorText);
		void scrollViewer_DoubleTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::DoubleTappedRoutedEventArgs^ e);
	};
}