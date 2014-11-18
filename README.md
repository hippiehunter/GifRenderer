GifRenderer
===========

Universal Windows(XAML) Image viewer for very high resolution images and GIFs


Usage
===========
assuming your xmlns looks like this

    xmlns:gif="using:GifRenderer"

usage will look something like this

    <gif:ZoomableImageControl DataContext="{Binding Url}" />

Limitations
===========
* Only works with network url sources
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
