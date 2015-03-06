#include "pch.h"
#include "GifRenderer.h"
#include "task_helper.h"

using namespace GifRenderer;
using namespace concurrency;
using namespace task_helper;
using Windows::Storage::Streams::IInputStream;
using namespace Platform;
typedef ::GifRenderer::GifRenderer GR;



GR::GifRenderer(Array<std::uint8_t>^ initialData, IInputStream^ inputStream,
    std::function<void(String^)>& errorHandler, std::function<void(int)>& loadCallback, cancellation_token cancelToken) : _cancelToken(cancelToken), _loaderData(cancelToken)
{
    _errorHandler = errorHandler;
    _loadCallback = loadCallback;
    _d3dDevice = nullptr;
    _d2dDevice = nullptr;
    InitialLoad(initialData, inputStream);
    _displayInfo = DisplayInformation::GetForCurrentView();
    _suspendingCookie = (Application::Current->Suspending += ref new SuspendingEventHandler(this, &GifRenderer::OnSuspending));
    _resumingCookie = (Application::Current->Resuming += ref new Windows::Foundation::EventHandler<Object^>(this, &GifRenderer::OnResuming));
    _callback = Make<VirtualSurfaceUpdatesCallbackNative>(this);
    _imageSource = ref new VirtualSurfaceImageSource(Width(), Height());
    reinterpret_cast<IUnknown*>(_imageSource)->QueryInterface(IID_PPV_ARGS(&_sisNative));
    _sisNative->RegisterForUpdatesNeeded(_callback.Get());
    _timer = ref new BasicTimer();
    CreateDeviceResources();
}

void gif_user_data::readSome()
{
	auto errorWrapper = [=](Platform::Exception^ ex)
	{
		if (dynamic_cast<Platform::OperationCanceledException^>(ex) != nullptr)
			errorHandler("Canceled");
		else
			errorHandler(ex->Message);

		return task_from_result();
	};

    finishedReader = false;
    continue_void_task(reader->LoadAsync(256 * 1024),
        [=](uint32_t loadOp)
    {
        if (loadCallback)
            loadCallback(loadOp);
        if (loadOp >= 256 * 1024)
            finishedReader = true;
    }, errorWrapper, cancelToken);

}

int gif_user_data::read(GifByteType * buf, unsigned int length)
{
    if (reader != nullptr && (position == buffer.size() || position + length > buffer.size()))
    {
        if (length > (buffer.size() - position) + reader->UnconsumedBufferLength)
        {
            if (finishedReader)
            {
                if (reader->UnconsumedBufferLength > 0)
                {
                    Platform::Array<uint8_t>^ moreData = ref new Platform::Array<uint8_t>(std::min(reader->UnconsumedBufferLength, (uint32_t)4096));
                    reader->ReadBytes(moreData);
                    auto existingSize = buffer.size();
                    buffer.resize(existingSize + moreData->Length);
                    memcpy(buffer.data() + existingSize, moreData->Data, moreData->Length);
                }
                readSome();
            }
            return -1;
        }
        else
        {
            Platform::Array<uint8_t>^ moreData = ref new Platform::Array<uint8_t>(std::min(reader->UnconsumedBufferLength, (uint32_t)4096));
            reader->ReadBytes(moreData);
            auto existingSize = buffer.size();
            buffer.resize(existingSize + moreData->Length);
            memcpy(buffer.data() + existingSize, moreData->Data, moreData->Length);
        }
    }

    auto egiLength = buffer.size();

    if (position == egiLength) return 0;
    if (position + length == egiLength) length = egiLength - position;
    memcpy(buf, buffer.data() + position, length);
    position += length;

    return length;
}

struct bgraColor
{
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
};

void mapRasterBits(uint8_t* rasterBits, std::unique_ptr<uint32_t[]>& targetFrame, ColorMapObject& colorMap, int top, int left, int bottom, int right, int width, int32_t transparencyColor)
{

    int i = 0;
    for (int y = top; y < bottom; y++)
    {
        for (int x = left; x < right; x++)
        {
            int offset = y * width + x;
            int index = rasterBits[i];

            if (transparencyColor == -1 ||
                transparencyColor != index)
            {
                auto colorTarget = reinterpret_cast<GifColorType*>(targetFrame.get() + offset);
                *colorTarget = colorMap.Colors[index];
            }
            i++;
        }
    }
}

template<typename GIFTYPE>
void loadGifFrame(GIFTYPE& gifFile, const std::vector<GifFrame>& frames, std::unique_ptr<uint32_t[]>& buffer, size_t currentFrame, size_t targetFrame)
{
    uint32_t width = gifFile->SWidth;
    uint32_t height = gifFile->SHeight;

    bgraColor bgColor = { 0, 0, 0, 0 };
    if (gifFile->SColorMap.Colors.size() != 0 && gifFile->SBackGroundColor > 0)
    {
        auto color = gifFile->SColorMap.Colors[gifFile->SBackGroundColor];
        bgColor.red = color.Red;
        bgColor.green = color.Green;
        bgColor.blue = color.Blue;
        bgColor.alpha = 255;
    }

    std::unique_ptr<uint32_t[]> lastFrame = nullptr;

    if (buffer == nullptr || targetFrame == 0 || currentFrame > targetFrame)
    {
        if (buffer == nullptr)
        {
            buffer = std::unique_ptr<uint32_t[]>(new uint32_t[width * height]);
            currentFrame = 0;
        }

        if (currentFrame > targetFrame)
            currentFrame = 0;

        for (decltype(height) y = 0; y < height; y++)
        {
            for (decltype(width) x = 0; x < width; x++)
            {
                auto offset = y * width + x;
                memcpy(buffer.get() + offset, &bgColor, 4);
            }
        }
    }

    for (auto i = currentFrame; i < gifFile->SavedImages.size() && i <= targetFrame; i++)
    {
        auto& frame = frames[i];
        auto& decodeFrame = gifFile->SavedImages[i];
        auto disposal = frame.disposal;
        auto colorMap = (decodeFrame.ImageDesc.ColorMap.Colors.size() != 0 ? decodeFrame.ImageDesc.ColorMap : (gifFile->SColorMap.Colors.size() != 0 ? gifFile->SColorMap : ColorMapObject{}));

        if (disposal == DISPOSAL_METHODS::DM_PREVIOUS)
        {
            if (lastFrame == nullptr)
                lastFrame = std::unique_ptr<uint32_t[]>(new uint32_t[width * height]);

            memcpy(lastFrame.get(), buffer.get(), width * height * sizeof(uint32_t));
        }

        switch (disposal)
        {
            case DISPOSAL_METHODS::DM_BACKGROUND:
                for (decltype(height) y = 0; y < height; y++)
                {
                    for (decltype(width) x = 0; x < width; x++)
                    {
                        int offset = y * width + x;
                        memcpy(buffer.get() + offset, &bgColor, 4);
                    }
                }
                break;
            case DISPOSAL_METHODS::DM_PREVIOUS:
                memcpy(buffer.get(), lastFrame.get(), width * height * sizeof(uint32_t));
                break;
        }

        mapRasterBits(decodeFrame.RasterBits.get(), buffer, colorMap, frame.top, frame.left, frame.bottom, frame.right, width, frame.transparentColor);
    }
}
template<typename GIFTYPE>
void loadGifFrames(GIFTYPE& gifFile, std::vector<GifFrame>& frames)
{
    uint32_t width = gifFile->SWidth;
    uint32_t height = gifFile->SHeight;

    for (auto i = frames.size(); i < gifFile->SavedImages.size(); i++)
    {
        uint32_t delay = 100;
        DISPOSAL_METHODS disposal = DISPOSAL_METHODS::DM_NONE;
        int32_t transparentColor = -1;

        auto extensionBlocks = gifFile->SavedImages[i].ExtensionBlocks;
        for (size_t ext = 0; ext < gifFile->SavedImages[i].ExtensionBlocks.size(); ext++)
        {
            if (extensionBlocks[ext].Function == 0xF9)
            {
                GraphicsControlBlock gcb(extensionBlocks[ext]);

                delay = gcb.DelayTime * 10;

                if (delay < 20)
                {
                    delay = 100;
                }

                disposal = (DISPOSAL_METHODS)gcb.DisposalMode;
                transparentColor = gcb.TransparentColor;
            }
        }
        auto& imageDesc = gifFile->SavedImages[i].ImageDesc;
        int right = imageDesc.Left + imageDesc.Width;
        int bottom = imageDesc.Top + imageDesc.Height;
        int top = imageDesc.Top;
        int left = imageDesc.Left;

        frames.push_back(GifFrame());
        auto& frame = frames.back();
        frame.transparentColor = transparentColor;
        frame.height = height;
        frame.width = width;
        frame.delay = delay;
        frame.top = top;
        frame.bottom = bottom;
        frame.right = right;
        frame.left = left;
        frame.disposal = disposal;
    }
    if (frames.size() != gifFile->SavedImages.size())
        throw ref new Platform::InvalidArgumentException("image count didnt match frame size");
}

bool GR::IsLoaded() const { return _isLoaded || _loaderData.finishedLoad; }
bool GR::LoadMore()
{
    try
    {
        _gifFile->Slurp(_loaderData);
        loadGifFrames(_gifFile, _frames);
        _isLoaded = true;
        _loaderData.buffer.clear();
        if (_loaderData.reader != nullptr)
            delete _loaderData.reader;
		_loaderData.errorHandler = {};
		_loaderData.loadCallback = {};
        return false;
    }
    catch (...)
    {
        loadGifFrames(_gifFile, _frames);
        if (_loaderData.finishedData)
        {
            _isLoaded = true;
            _loaderData.finishedLoad = true;
            _loaderData.buffer.clear();
            if (_loaderData.reader != nullptr)
                delete _loaderData.reader;

			_loaderData.errorHandler = {};
			_loaderData.loadCallback = {};
            return false;
        }
        else
            return true;
    }
}
uint32_t GR::GetFrameDelay(size_t index) const { return _frames[index].delay; }
uint32_t GR::Height() const { return _gifFile->SHeight; }
uint32_t GR::Width() const { return _gifFile->SWidth; }
size_t GR::FrameCount() const
{
    if (_frames.size() != _gifFile->SavedImages.size())
        throw ref new Platform::InvalidArgumentException("image count didnt match frame size");
    return _frames.size();
}

std::unique_ptr<uint32_t[]>& GR::GetFrame(size_t currentIndex, size_t targetIndex)
{
    loadGifFrame(_gifFile, _frames, _renderBuffer, currentIndex, targetIndex);
    return _renderBuffer;
}

void GR::InitialLoad(Platform::Array<std::uint8_t>^ initialData, Windows::Storage::Streams::IInputStream^ inputStream)
{
    auto dataReader = ref new Windows::Storage::Streams::DataReader(inputStream);
    dataReader->InputStreamOptions = Windows::Storage::Streams::InputStreamOptions::ReadAhead;
    _loaderData.init(0, std::vector<uint8_t>(begin(initialData), end(initialData)), dataReader, _loadCallback, _errorHandler);
    _loaderData.readSome();

    try
    {
        _gifFile = std::make_unique<GifFileType<gif_user_data>>(_loaderData);

        _loaderData.buffer.erase(_loaderData.buffer.begin(), _loaderData.buffer.begin() + _loaderData.position);
        _loaderData.position = 0;
        try
        {
            _gifFile->Slurp(_loaderData);
            _isLoaded = true;
            _loaderData.buffer.clear();
            if (_loaderData.reader != nullptr)
                delete _loaderData.reader;

			_loaderData.errorHandler = {};
			_loaderData.loadCallback = {};
        }
        catch (...)
        {
            if (_loaderData.finishedData)
            {
                _isLoaded = true;
                _loaderData.buffer.clear();
                if (_loaderData.reader != nullptr)
                    delete _loaderData.reader;
                
				_loaderData.errorHandler = {};
				_loaderData.loadCallback = {};
            }
        }

        uint32_t width = (_gifFile->SWidth % 2) + _gifFile->SWidth;
        uint32_t height = (_gifFile->SHeight % 2) + _gifFile->SHeight;

        _gifFile->SHeight = height;
        _gifFile->SWidth = width;
        loadGifFrames(_gifFile, _frames);
    }
    catch (...)
    {
        throw ref new Platform::InvalidArgumentException("invalid gif asset");
    }
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
    if (_loaderData.reader != nullptr)
    {
        delete _loaderData.reader;
    }
    _loaderData.buffer.clear();
	_loaderData.errorHandler = {};
	_loaderData.loadCallback = {};
    _callback = nullptr;
    _timer = nullptr;
    _suspended = true;
    _sisNative->RegisterForUpdatesNeeded(nullptr);
    _sisNative = nullptr;
    _callback = nullptr;
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
        if (i >= FrameCount())
        {
            if (IsLoaded())
                i = 0;
            else
                i = FrameCount() - 1;
        }

        accountedFor += GetFrameDelay(i);
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
        if (!IsLoaded())
            LoadMore();

        if (FrameCount() > 0)
        {

            if (!_startedRendering)
                _timer->Reset();
            else
                _timer->Update();

            if (Update(_timer->Total, _timer->Delta))
            {
                RECT visibleBounds;
                _sisNative->GetVisibleBounds(&visibleBounds);
                D2D1_RECT_U rect = { 0, 0, Width(), Height() };


                auto& renderedData = GetFrame(_lastFrame, _currentFrame);
                _lastFrame = _currentFrame;

                D2D1_BITMAP_PROPERTIES properties;
                properties.pixelFormat = D2D1_PIXEL_FORMAT{ DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE };
                properties.dpiX = _displayInfo->RawDpiX;
                properties.dpiY = _displayInfo->RawDpiY;
                D2D1_SIZE_U size = { Width(), Height() };

                if (_d2dContext == nullptr)
                    CreateDeviceResources();

                ThrowIfFailed(_d2dContext->CreateBitmap(size, renderedData.get(), Width() * 4, properties, _renderBitmap.ReleaseAndGetAddressOf()));
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