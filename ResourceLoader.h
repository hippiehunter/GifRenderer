#pragma once

#include <ppltasks.h>
#include <functional>
#include <tuple>

class ResourceLoader
{
private:
	ResourceLoader(concurrency::cancellation_token token) : _cancelToken(token) { }
	Platform::String^ _resourceLocator;
	concurrency::cancellation_token _cancelToken;
	std::function<void(Windows::Storage::Streams::IBuffer^, bool, uint32_t)> _dataReadHook;
	std::function<std::tuple<bool, bool>(Windows::Storage::Streams::IBuffer^, uint32_t)> _initialReadHook;
	uint32_t _expectedByteCount;
	bool _cacheResult;
	bool _dontBufferReadHook;
	Windows::Storage::Streams::IRandomAccessStream^ _cacheWriter;
	Windows::Storage::Streams::InMemoryRandomAccessStream^ _inMemoryWriter;
	concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> LoadStorageFile(Windows::Storage::StorageFile^ storageFile);
	concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> GetLocalUri();
	concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> GetPath();
	concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> GetHttpUri();
	concurrency::task<void> ReadSomeHttp(Windows::Storage::Streams::IInputStream^ inputStream);
	concurrency::task<void> WriteBufferToResultStream(Windows::Storage::Streams::IBuffer^ buffer, bool finished);
	concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> BufferRandomAccessStream(Windows::Storage::Streams::IRandomAccessStream^ stream);
	Platform::String^ ComputeMD5(Platform::String^ str);
	concurrency::task<void> FailureCacheCleanup();
	template<typename RESULT_TYPE, typename EXCEPTION_TYPE>
	std::function<concurrency::task<RESULT_TYPE>(EXCEPTION_TYPE)> make_error_handler()
	{
		return [=](EXCEPTION_TYPE ex)
		{
			return FailureCacheCleanup().then([=](task<void> tsk)
			{
				return concurrency::task_from_exception<RESULT_TYPE>(ex);
			});
		};
	}
public:
	static void StridedCopy(uint8_t* src, Windows::Foundation::Size sourceSize, uint32_t elementSize, uint8_t* dst, Windows::Foundation::Rect desiredRect);
	static void GetBytesFromBuffer(Windows::Storage::Streams::IBuffer^ buffer, std::function<void(uint8_t* bytes, uint32_t byteCount)> handler);
	static concurrency::task<void> CleanOldTemps();
	static concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> GetResource(Platform::String^ resourceLocator, 
		std::function<std::tuple<bool, bool>(Windows::Storage::Streams::IBuffer^, uint32_t)> initialRead,
		std::function<void(Windows::Storage::Streams::IBuffer^, bool, uint32_t)> dataReadHook, concurrency::cancellation_token canceledToken);
};