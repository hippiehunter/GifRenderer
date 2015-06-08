#pragma once

#include <ppltasks.h>
#include <memory>
#include <wrl.h>
#include <wrl\client.h>
#include "windows.ui.xaml.media.dxinterop.h"

class IImageRenderer
{
public:
	virtual Windows::Foundation::Size Size() = 0;
	virtual Windows::Foundation::Size MaxSize() = 0;
	virtual void Draw() = 0;
	virtual void Suspend() = 0;
	virtual void Resume() = 0;
	virtual void ViewChanged(float zoomFactor) = 0;
	virtual ~IImageRenderer() {}
};

struct ImageRendererUpdatesCallbackNative : public Microsoft::WRL::RuntimeClass < Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IVirtualSurfaceUpdatesCallbackNative >
{
private:
	std::weak_ptr<IImageRenderer> _rendererReference;
public:
	ImageRendererUpdatesCallbackNative(std::shared_ptr<IImageRenderer> renderer) : _rendererReference(renderer)
	{
	}
	virtual HRESULT STDMETHODCALLTYPE UpdatesNeeded()
	{
		try
		{
			auto renderer = _rendererReference.lock();
			if (renderer != nullptr)
			{
				renderer->Draw();
				return S_OK;
			}
		}
		catch (...) {}
		return E_FAIL;
	}
};