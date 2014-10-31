#include <cstdint>
#include <GifRenderer.h>
#include <VirtualSurfaceRenderer.h>
#include <ppltasks.h>

namespace GifRenderer
{
  public ref class GifPayload sealed
  {
  public:
    property Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData;
    property Windows::Storage::Streams::IInputStream^ inputStream;
    property Platform::String^ url;
  };

  public ref class StillImage sealed
  {
  public:
    StillImage(Platform::String^ url)
    {
      ImageSource = ref new Windows::UI::Xaml::Media::Imaging::BitmapImage(ref new Windows::Foundation::Uri(url));
    }
    property Windows::UI::Xaml::Media::Imaging::BitmapImage^ ImageSource;
  };

  public ref class GifConverter sealed : Windows::UI::Xaml::Data::IValueConverter
  {
  private:
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
  public:
    virtual Platform::Object^ Convert(Platform::Object^ value, Windows::UI::Xaml::Interop::TypeName targetType,
      Platform::Object^ parameter, Platform::String^ language)
    {
      auto dataValue = dynamic_cast<GifPayload^>(value);
      if (dataValue != nullptr && dataValue->initialData != nullptr && dataValue->inputStream != nullptr)
      {
        if (IsGif(dataValue->initialData))
          return ref new ::GifRenderer::GifRenderer(dataValue->initialData, dataValue->inputStream);
        else
        {
			/*if (dataValue->initialData != nullptr)
				dataValue->initialData->Clear();
			if (dataValue->inputStream != nullptr)
			{
				delete dataValue->inputStream;
				dataValue->inputStream = nullptr;
			}

			return ref new StillImage(dataValue->url);*/
          return ref new VirtualSurfaceRenderer(dataValue->initialData, dataValue->inputStream);
        }

      }
      else if (dataValue != nullptr && dataValue->url != nullptr)
      {
        return ref new StillImage(dataValue->url);
      }
      else
        return nullptr;
    }

    // No need to implement converting back on a one-way binding 
    virtual Platform::Object^ ConvertBack(Platform::Object^ value, Windows::UI::Xaml::Interop::TypeName targetType,
      Platform::Object^ parameter, Platform::String^ language)
    {
      throw ref new Platform::NotImplementedException();
    }
  };
}