//
// ZoomableImageControl.xaml.cpp
// Implementation of the ZoomableImageControl class
//

#include "pch.h"
#include "ZoomableImageControl.xaml.h"
#include <ppltasks.h>
#include <ppl.h>
#include "task_helper.h"
#include <boost\format.hpp>

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
using namespace concurrency;
using namespace task_helper;
using std::tuple;
using std::shared_ptr;
// The User Control item template is documented at http://go.microsoft.com/fwlink/?LinkId=234236

::GifRenderer::ZoomableImageControl::ZoomableImageControl()
{
    ResizeToFitHorizontal = false;
	InitializeComponent();
}

DependencyProperty^ ::GifRenderer::ZoomableImageControl::_urlProperty = DependencyProperty::Register("Url",
	String::typeid, ::GifRenderer::ZoomableImageControl::typeid, ref new PropertyMetadata(nullptr,
		ref new Windows::UI::Xaml::PropertyChangedCallback(&::GifRenderer::ZoomableImageControl::OnUrlChanged)));

DependencyProperty^ ::GifRenderer::ZoomableImageControl::_resizeToFitHorizontalProperty = DependencyProperty::Register("ResizeToFitHorizontal",
    bool::typeid, ::GifRenderer::ZoomableImageControl::typeid, nullptr);

void ::GifRenderer::ZoomableImageControl::OnUrlChanged(DependencyObject^ d, DependencyPropertyChangedEventArgs^ e)
{
	try
	{
		auto thisp = dynamic_cast<::GifRenderer::ZoomableImageControl^>(d);
		auto targetUrl = dynamic_cast<String^>(e->NewValue);
		if (thisp->_targetUrl != targetUrl)
		{
			if (thisp->_targetUrl != nullptr)
			{
				thisp->cancelSource.cancel();
				thisp->cancelSource = cancellation_token_source();
				thisp->image->Source = nullptr;
			}
			thisp->_targetUrl = targetUrl;
			thisp->_initialSizeChanged = false;
			thisp->_renderer = nullptr;
			if (thisp->_targetUrl != nullptr)
				thisp->Load();
		}
	}
	catch (...)
	{
		OutputDebugString(L"unknown error replacing data context");
	}
}

void ::GifRenderer::ZoomableImageControl::UserControl_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	try
	{
		FadeIn->Stop();
		cancelSource.cancel();
		cancelSource = cancellation_token_source();
		_renderer = nullptr;
		image->Source = nullptr;
	}
	catch (...)
	{
		OutputDebugString(L"unknown error unloading control");
	}
}

void ::GifRenderer::ZoomableImageControl::ErrorHandler(Platform::String^ errorText)
{
	try
	{
		progressStack->Opacity = 1.0;
		retryButton->Opacity = 1.0;
		loadText->Text = errorText;
		scrollViewer->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}
	catch (...)
	{
	}
}

void ::GifRenderer::ZoomableImageControl::scrollViewer_ViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e)
{
	if (_renderer != nullptr)
	{
		auto targetZoom = dynamic_cast<ScrollViewer^>(sender)->ZoomFactor;
		if (targetZoom != _currentZoom && !e->IsIntermediate)
		{
			_currentZoom = targetZoom;
			_renderer->ViewChanged(targetZoom);

			/*auto renderSize = _renderer->Size();
			image->Width = renderSize.Width;
			image->Height = renderSize.Height;*/
		}
	}
}

void ::GifRenderer::ZoomableImageControl::Retry_Clicked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	_initialSizeChanged = false;
	_renderer = nullptr;
	scrollViewer->Visibility = Windows::UI::Xaml::Visibility::Visible;
	Load();
}

void ::GifRenderer::ZoomableImageControl::Load()
{
	auto coreDispatcher = this->Dispatcher;
	auto cancelToken = cancelSource.get_token();
	auto handle_errors = [=](Platform::Exception^ ex) 
	{ 
		ui_task(coreDispatcher, [=]()
		{
			progressStack->Opacity = 0.0;
			retryButton->Opacity = 1.0;
			loadText->Text = ex->Message;
		});
		return task_from_result();
	};
	finish_task(
		continue_task(ImageFactory::MakeRenderer(_targetUrl, [=](float pct)
		{
			ui_task(coreDispatcher, [=]()
			{
				double Angle = 2 * 3.14159265 * pct;

				double X = 12 - std::sin(Angle) * 12;
				double Y = 12 + std::cos(Angle) * 12;

				if (pct > 0 && (int)X == 12 && (int)Y == 24)
					X += 0.01; // Never make the end the same as the start!

				TheSegment->IsLargeArc = Angle >= 3.14159265;
				TheSegment->Point = Point(static_cast<float>(X), static_cast<float>(Y));
			});
		}, coreDispatcher, cancelToken),
			[=](tuple<shared_ptr<IImageRenderer>, ImageSource^> result)
		{
			_renderer = std::get<0>(result);
			auto imageSource = std::get<1>(result);
			//wait long enough for a layout pass to have occured
			ui_task(coreDispatcher, [=]()
			{
				if (!cancelToken.is_canceled())
				{
					_initialSizeChanged = true;
					// If the image is larger than the screen, zoom it out
					image->Source = imageSource;
				
					auto maxSize = _renderer->MaxSize();
					if (ActualWidth != ActualWidth || ActualHeight != ActualHeight)
						throw ref new Platform::Exception(0);

					image->Width = maxSize.Width;
					image->Height = maxSize.Height;

					auto fillZoom = ResizeToFitHorizontal ?  
                        (ActualWidth / maxSize.Width) :
                        (float)min((ActualWidth / maxSize.Width), (ActualHeight / maxSize.Height));

                    if (ResizeToFitHorizontal)
                    {
                        Height = fillZoom * maxSize.Height;
                    }

					_currentZoom = min(20, max(fillZoom, 0.1f));
					scrollViewer->MinZoomFactor = max(_currentZoom * .5f, .1f);
					scrollViewer->MaxZoomFactor = 20;
					delayed_ui_task(std::chrono::milliseconds(100), [=]()
					{
						if (scrollViewer->ChangeView(maxSize.Width / 2.0, maxSize.Width / 2.0, _currentZoom, true))
						{
							FadeIn->Begin();
						}
						else
						{
							delayed_ui_task(std::chrono::milliseconds(100), [=]()
							{
								if (!scrollViewer->ChangeView(maxSize.Width / 2.0, maxSize.Width / 2.0, _currentZoom))
									OutputDebugString(L"failed setting view");
								FadeIn->Begin();
							});
						}
					});
				}
			});
			return task_from_result();
		}, handle_errors), handle_errors);
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

			auto baseZoomFactor = (float)min(viewportWidth / _width, viewportHeight / _height);

			if (scrollViewer->ZoomFactor > baseZoomFactor * 2.0)
				scrollViewer->ChangeView(nullptr, nullptr, baseZoomFactor, false);
			else
				scrollViewer->ChangeView(static_cast<double>(point.X) + scrollXOffset, static_cast<double>(point.Y) + scrollYOffset, scrollZoom * 1.7f, false);
		});
	}
}
