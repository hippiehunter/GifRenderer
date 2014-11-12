#pragma once
#include "VirtualSurfaceRenderer.h"

namespace GifRenderer
{
	public ref class GifPayload sealed
	{
	public:
		property Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData;
		property Windows::Storage::Streams::IInputStream^ inputStream;
    property int expectedSize;
		property Platform::String^ url;
	};

	//[Windows::UI::Xaml::Data::Bindable]
	//public ref class StillImage sealed : public Windows::UI::Xaml::Data::INotifyPropertyChanged
	//{
	//private:
	//	VirtualSurfaceRenderer^ _renderer;
	//	//Windows::UI::Xaml::Controls::ScrollViewer^ _scrollViewer;
	//public:
	//	virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged;

	//	StillImage(VirtualSurfaceRenderer^ renderer)
	//	{
	//		_renderer = renderer;
	//		renderer->RegisterUpdate(std::function<void()>([=]()
	//		{
	//			ImageSource = renderer->ImageSource;
	//			PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs("ImageSource"));
	//		}));
	//	}
	//	virtual ~StillImage()
	//	{
	//		_renderer = nullptr;
	//	}
	//	property Platform::Object^ ImageSource;
	//};
}