GifRenderer
===========

Universal Windows(XAML) Image viewer for very high resolution images and GIFs


Usage
===========
First add the vcxproj's and shared project to your solution then, in the target xaml file assuming your xmlns looks like this:
```xml
    xmlns:gif="using:GifRenderer"
```
Usage will look something like this:
```xml
    <gif:ZoomableImageControl DataContext="{Binding Url}" />
```
# Do not use the NuGet package. It is out of date and I've not figured out how to correctly package this

Limitations
===========
* strings displayed to user are not localized
* Zoom options are not exposed

Awesome Things
===========
* Works with super large images on low mem devices
* Works with super large GIFs on low mem devices
* Loads GIFs incrementally from the network, displaying them as they are ready
* Only renders when in view
* Ultra High Performance C++ with Direct2D
* Compatible with nearly every GIF (uses a modified GIFLIB)
* Double tap to zoom
