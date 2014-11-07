#include "pch.h"
#include "GifRenderer.h"

using namespace GifRenderer;
typedef ::GifRenderer::GifRenderer GR;


GR::GifRenderer(Windows::Foundation::Collections::IVector<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream)
{
  _d3dDevice = nullptr;
  _d2dDevice = nullptr;
  _gifLoader = ref new GifLoader(initialData, inputStream);
  _displayInfo = DisplayInformation::GetForCurrentView();
  _suspendingCookie = (Application::Current->Suspending += ref new SuspendingEventHandler(this, &GifRenderer::OnSuspending));
  _resumingCookie = (Application::Current->Resuming += ref new Windows::Foundation::EventHandler<Object^>(this, &GifRenderer::OnResuming));
  _callback = Make<VirtualSurfaceUpdatesCallbackNative>(this);
  _imageSource = ref new VirtualSurfaceImageSource(_gifLoader->Width(), _gifLoader->Height());
  reinterpret_cast<IUnknown*>(_imageSource)->QueryInterface(IID_PPV_ARGS(&_sisNative));
  _sisNative->RegisterForUpdatesNeeded(_callback.Get());
  _timer = ref new BasicTimer();
  CreateDeviceResources();
}

void GR::OnSuspending(Object ^sender, SuspendingEventArgs ^e)
{
  _suspended = true;

  _renderBitmap = nullptr;
  _d2dContext = nullptr;

  if (_d3dDevice != nullptr)
  {
    ComPtr<IDXGIDevice3> dxgiDevice;
    _d3dDevice.As(&dxgiDevice);

    // Hints to the driver that the app is entering an idle state and that its memory can be used temporarily for other apps.
    dxgiDevice->Trim();
    _d3dDevice = nullptr;
    _d2dDevice = nullptr;
  }

}

void GR::OnResuming(Object ^sender, Object ^e)
{
  _suspended = false;
  _startedRendering = false;

  RECT invalidateRect{ 0, 0, 1, 1 };
  _sisNative->Invalidate(invalidateRect);
}

VirtualSurfaceImageSource^ GR::ImageSource::get()
{
  return _imageSource;
}

GR::~GifRenderer()
{
  _callback = nullptr;
  _timer = nullptr;
  _suspended = true;
  _sisNative->RegisterForUpdatesNeeded(nullptr);
  _sisNative = nullptr;
  _callback = nullptr;
  _gifLoader = nullptr;
  _renderBitmap = nullptr;
  _d2dContext = nullptr;
  Application::Current->Suspending -= _suspendingCookie;
  Application::Current->Resuming -= _resumingCookie;
}

void GR::CreateDeviceResources()
{
  if (_d3dDevice == nullptr)
  {
    // This flag adds support for surfaces with a different color channel ordering
    // than the API default. It is required for compatibility with Direct2D.
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)    
    // If the project is in a debug build, enable debugging via SDK Layers.
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // This array defines the set of DirectX hardware feature levels this app will support.
    // Note the ordering should be preserved.
    // Don't forget to declare your application's minimum required feature level in its
    // description.  All applications are assumed to support 9.1 unless otherwise stated.
    const D3D_FEATURE_LEVEL featureLevels[] =
    {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_2,
      D3D_FEATURE_LEVEL_9_1,
    };

    // Create the Direct3D 11 API device object.
    ThrowIfFailed(
      D3D11CreateDevice(
      nullptr,                        // Specify nullptr to use the default adapter.
      D3D_DRIVER_TYPE_HARDWARE,
      nullptr,
      creationFlags,                  // Set debug and Direct2D compatibility flags.
      featureLevels,                  // List of feature levels this app can support.
      ARRAYSIZE(featureLevels),
      D3D11_SDK_VERSION,              // Always set this to D3D11_SDK_VERSION for Metro style apps.
      &_d3dDevice,                   // Returns the Direct3D device created.
      nullptr,
      nullptr
      )
      );

    // Get the Direct3D 11.1 API device.
    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(
      _d3dDevice.As(&dxgiDevice)
      );

    // Create the Direct2D device object and a corresponding context.
    ThrowIfFailed(
      D2D1CreateDevice(
      dxgiDevice.Get(),
      nullptr,
      &_d2dDevice
      )
      );
  }

  ThrowIfFailed(
    _d2dDevice->CreateDeviceContext(
    D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
    &_d2dContext
    )
    );

  // Set DPI to the display's current DPI.
  _d2dContext->SetDpi(_displayInfo->RawDpiX, _displayInfo->RawDpiY);

  // Get the Direct3D 11.1 API device.
  ComPtr<IDXGIDevice> dxgiDevice2;
  ThrowIfFailed(
    _d3dDevice.As(&dxgiDevice2)
    );

  // Associate the DXGI device with the SurfaceImageSource.
  ThrowIfFailed(
    _sisNative->SetDevice(dxgiDevice2.Get())
    );
}
void GR::BeginDraw(POINT& offset)
{
  ComPtr<IDXGISurface> surface;

  // Express target area as a native RECT type.
  RECT updateRectNative;
  _sisNative->GetVisibleBounds(&updateRectNative);

  // Begin drawing - returns a target surface and an offset to use as the top left origin when drawing.
  HRESULT beginDrawHR = _sisNative->BeginDraw(updateRectNative, &surface, &offset);

  if (beginDrawHR == DXGI_ERROR_DEVICE_REMOVED || beginDrawHR == DXGI_ERROR_DEVICE_RESET)
  {
    // If the device has been removed or reset, attempt to recreate it and continue drawing.
    CreateDeviceResources();
    BeginDraw(offset);
  }
  else
  {
    // Notify the caller by throwing an exception if any other error was encountered.
    ThrowIfFailed(beginDrawHR);
  }


  // Create render target.
  ComPtr<ID2D1Bitmap1> bitmap;

  ThrowIfFailed(
    _d2dContext->CreateBitmapFromDxgiSurface(
    surface.Get(),
    nullptr,
    &bitmap
    )
    );

  // Set context's render target.
  _d2dContext->SetTarget(bitmap.Get());


  // Begin drawing using D2D context.
  _d2dContext->BeginDraw();
  // Apply a clip and transform to constrain updates to the target update area.
  // This is required to ensure coordinates within the target surface remain
  // consistent by taking into account the offset returned by BeginDraw, and
  // can also improve performance by optimizing the area that is drawn by D2D.
  // Apps should always account for the offset output parameter returned by 
  // BeginDraw, since it may not match the passed updateRect input parameter's location.

  _d2dContext->SetUnitMode(D2D1_UNIT_MODE::D2D1_UNIT_MODE_PIXELS);

  _d2dContext->PushAxisAlignedClip(
    D2D1::RectF(
    static_cast<float>(offset.x),
    static_cast<float>(offset.y),
    static_cast<float>(offset.x + (updateRectNative.right - updateRectNative.left)),
    static_cast<float>((offset.y) + (updateRectNative.bottom - updateRectNative.top))
    ),
    D2D1_ANTIALIAS_MODE_ALIASED
    );

  _d2dContext->SetTransform(
    D2D1::Matrix3x2F::Translation(
    static_cast<float>(offset.x - updateRectNative.left),
    static_cast<float>((offset.y) - updateRectNative.top)
    )
    );
}

void GR::EndDraw()
{
  // Remove the transform and clip applied in BeginDraw since
  // the target area can change on every update.

  //_d2dContext->PopAxisAlignedClip();

  // Remove the render target and end drawing.
  ThrowIfFailed(
    _d2dContext->EndDraw()
    );

  _d2dContext->SetTarget(nullptr);

  ThrowIfFailed(
    _sisNative->EndDraw()
    );
}

bool GR::Update(float total, float delta)
{
  double msDelta = ((double)total) * 1000;
  double accountedFor = 0;
  size_t i = 0;
  for (; accountedFor < msDelta; i++)
  {
    if (i >= _gifLoader->FrameCount())
    {
      if (_gifLoader->IsLoaded())
        i = 0;
      else
        i = _gifLoader->FrameCount() - 1;
    }

    accountedFor += _gifLoader->GetFrameDelay(i);
  }
  auto newFrame = std::max<int>((int)i - 1, 0);
  if (newFrame != _currentFrame || _currentFrame == 0)
  {
    _currentFrame = newFrame;
    _startedRendering = true;
    return true;
  }
  else
  {
    if (!_startedRendering)
    {
      _startedRendering = true;
      return true;
    }
    else
      return true;
  }
}

void GR::Update()
{
  try
  {
    if (!_gifLoader->IsLoaded())
      _gifLoader->LoadMore();

    if (_gifLoader->FrameCount() > 0)
    {

      if (!_startedRendering)
        _timer->Reset();
      else
        _timer->Update();

      if (Update(_timer->Total, _timer->Delta))
      {
        RECT visibleBounds;
        _sisNative->GetVisibleBounds(&visibleBounds);
        D2D1_RECT_U rect = { 0, 0, _gifLoader->Width(), _gifLoader->Height() };


        auto& renderedData = _gifLoader->GetFrame(_lastFrame, _currentFrame);
        _lastFrame = _currentFrame;

        D2D1_BITMAP_PROPERTIES properties;
        properties.pixelFormat = D2D1_PIXEL_FORMAT{ DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE };
        properties.dpiX = _displayInfo->RawDpiX;
        properties.dpiY = _displayInfo->RawDpiY;
        D2D1_SIZE_U size = { _gifLoader->Width(), _gifLoader->Height() };

        if (_d2dContext == nullptr)
          CreateDeviceResources();

        ThrowIfFailed(_d2dContext->CreateBitmap(size, renderedData.get(), _gifLoader->Width() * 4, properties, _renderBitmap.ReleaseAndGetAddressOf()));
        POINT offset;
        BeginDraw(offset);
        _d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::White));
        _d2dContext->DrawBitmap(_renderBitmap.Get());
        _d2dContext->SetTransform(D2D1::IdentityMatrix());
        _d2dContext->PopAxisAlignedClip();
        _d2dContext->Flush();
        EndDraw();
      }
    }
  }
  catch (...) {}

  if (!_suspended)
  {
    RECT visibleBounds;
    _sisNative->GetVisibleBounds(&visibleBounds);
    _sisNative->Invalidate(visibleBounds);
  }
}