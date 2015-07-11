#include "pch.h"
#include "ResourceLoader.h"
#include "task_helper.h"

#include <functional>
#include <tuple>
#include <robuffer.h>
#include <chrono>
#include <wrl.h>
#include <wrl\client.h>

using std::function;

using namespace concurrency;
using namespace task_helper;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;
using namespace Windows::Web::Http;
using namespace Microsoft::WRL;
using namespace Windows::Security::Cryptography::Core;
using namespace Windows::Security::Cryptography;

bool starts_with(std::wstring const &fullString, std::wstring const &start)
{
  if (fullString.length() >= start.length())
  {
    return (0 == fullString.compare(0, start.length(), start));
  }
  else
  {
    return false;
  }
}

task<IRandomAccessStream^> ResourceLoader::GetResource(String^ resourceLocator, std::function<std::tuple<bool, bool>(Windows::Storage::Streams::IBuffer^, uint32_t)> initialRead,
  function<void(IBuffer^, bool, uint32_t)> dataReadHook, cancellation_token canceledToken)
{
  std::wstring resourceStr(resourceLocator->Data(), resourceLocator->Length());
  auto loader = new ResourceLoader(canceledToken);
  loader->_initialReadHook = initialRead;
  loader->_dataReadHook = dataReadHook;
  loader->_resourceLocator = resourceLocator;
  //need to ensure the lifetime of loader by cleaning it up when we either fail or finish successfully
  function<task<IRandomAccessStream^>(task<IRandomAccessStream^>)> finisher = [=](task<IRandomAccessStream^> result)
  {
    return result.then(
      [=](task<IRandomAccessStream^> streamTask)
    {
      try
      {
        auto stream = streamTask.get();
        delete loader;
        return task_from_result(stream);
      }
      catch (concurrency::task_canceled)
      {
        delete loader;
        return task_from_exception<IRandomAccessStream^>(ref new Platform::OperationCanceledException());
      }
      catch (Exception^ ex)
      {
        delete loader;
        return task_from_exception<IRandomAccessStream^>(ex);
      }
    }, loader->_cancelToken);
  };

  if (starts_with(resourceStr, L"https://") || starts_with(resourceStr, L"http://"))
  {
    return finisher(loader->GetHttpUri());
  }
  else if (starts_with(resourceStr, L"ms-appdata:///") ||
    starts_with(resourceStr, L"ms-appx:///") ||
    starts_with(resourceStr, L"ms-appx-web:///") ||
    starts_with(resourceStr, L"//"))
  {
    return finisher(loader->GetLocalUri());
  }
  else
  {
    return finisher(loader->GetPath());
  }
}

task<IRandomAccessStream^> ResourceLoader::LoadStorageFile(StorageFile^ storageFile)
{
  return continue_task(storageFile->OpenReadAsync(),
    [=](Windows::Storage::Streams::IRandomAccessStreamWithContentType^ accessStream)
  {
    return continue_task_arbitrary(accessStream->ReadAsync(ref new Buffer(64 * 1024), 64 * 1024, InputStreamOptions::None),
      [=](Windows::Storage::Streams::IBuffer^ bufferResult)
    {
      //we're only willing to deal with images that are smaller than 4gb
      auto readHookResult = _initialReadHook(bufferResult, static_cast<uint32_t>(accessStream->Size));
      _dontBufferReadHook = std::get<0>(readHookResult);
      _cacheResult = std::get<1>(readHookResult);
      if (_dontBufferReadHook)
      {
        accessStream->Seek(0);
        return task_from_result(dynamic_cast<Windows::Storage::Streams::IRandomAccessStream^>(accessStream));
      }
      else
      {
        return BufferRandomAccessStream(dynamic_cast<Windows::Storage::Streams::IRandomAccessStream^>(accessStream));
      }
    }, &default_error_handler<Windows::Storage::Streams::IRandomAccessStream^, Platform::Exception^>, _cancelToken);

  }, &default_error_handler<Windows::Storage::Streams::IRandomAccessStream^, Platform::Exception^>, _cancelToken);
}

task<IRandomAccessStream^> ResourceLoader::GetLocalUri()
{
  return continue_task(Windows::Storage::StorageFile::GetFileFromApplicationUriAsync(ref new Windows::Foundation::Uri(_resourceLocator)),
    [=](Windows::Storage::StorageFile^ storageFile)
  {
    return LoadStorageFile(storageFile);
  }, &default_error_handler<Windows::Storage::Streams::IRandomAccessStream^, Platform::Exception^>, _cancelToken);
}

task<IRandomAccessStream^> ResourceLoader::GetPath()
{
  return continue_task(Windows::Storage::StorageFile::GetFileFromPathAsync(_resourceLocator),
    [=](Windows::Storage::StorageFile^ storageFile)
  {
    return LoadStorageFile(storageFile);
  }, &default_error_handler<Windows::Storage::Streams::IRandomAccessStream^, Platform::Exception^>, _cancelToken);
}

task<IRandomAccessStream^> ResourceLoader::BufferRandomAccessStream(IRandomAccessStream^ stream)
{
  auto errorHandler = make_error_handler<IRandomAccessStream^, Exception^>();
  return continue_task(stream->ReadAsync(ref new Buffer(64 * 1024), 64 * 1024, InputStreamOptions::None),
    [=](Windows::Storage::Streams::IBuffer^ bufferResult)
  {
    if (bufferResult->Length >= 64 * 1024)
    {
      _dataReadHook(bufferResult, false, _expectedByteCount);
      return BufferRandomAccessStream(stream);
    }
    else
    {
      _dataReadHook(bufferResult, true, _expectedByteCount);
      stream->Seek(0);
      return task_from_result(stream);
    }
  }, errorHandler, _cancelToken);
}

task<IRandomAccessStream^> ResourceLoader::GetHttpUri()
{
  auto targetFileName = L"deleteme" + ComputeMD5(_resourceLocator) + L".cacheDownload";
  auto targetFilePath = ApplicationData::Current->TemporaryFolder->Path + L"\\deleteme" + ComputeMD5(_resourceLocator) + L".cacheDownload";
  struct _stat64i32 statVar;
  if (_wstat(targetFilePath->Data(), &statVar) == 0)
  {
    return continue_task(Windows::Storage::StorageFile::GetFileFromPathAsync(targetFilePath),
      [=](Windows::Storage::StorageFile^ storageFile)
    {
      return LoadStorageFile(storageFile);
    }, &default_error_handler<Windows::Storage::Streams::IRandomAccessStream^, Platform::Exception^>, _cancelToken);
  }
  else
  {
    auto errorHandler = make_error_handler<IRandomAccessStream^, Exception^>();
    auto client = ref new HttpClient();
    return continue_task(client->GetAsync(ref new Uri(_resourceLocator), HttpCompletionOption::ResponseHeadersRead),
      [=](HttpResponseMessage^ response)
    {
      auto contentLengthBox = response->Content->Headers->ContentLength;
      _expectedByteCount = contentLengthBox != nullptr ? static_cast<uint32_t>(contentLengthBox->Value) : 0;
      return continue_task(response->Content->ReadAsInputStreamAsync(),
        [=](IInputStream^ responseStream)
      {
        return continue_task(responseStream->ReadAsync(ref new Buffer(4096), 4096, InputStreamOptions::ReadAhead),
          [=](IBuffer^ buffer)
        {
          try
          {
            if (buffer->Length == 0)
              return task_from_exception<IRandomAccessStream^>(ref new Exception(E_FAIL, L"failed to read initial bytes of image"));

            auto readHookResult = _initialReadHook(buffer, _expectedByteCount);
            _dontBufferReadHook = std::get<0>(readHookResult);
            _cacheResult = std::get<1>(readHookResult);

            return continue_void_task(WriteBufferToResultStream(buffer, false),
              [=]()
            {
              return continue_void_task(ReadSomeHttp(responseStream),
                [=]()
              {
                if (_cacheResult)
                {
                  if (_cacheWriter != nullptr)
                  {
                    delete _cacheWriter;
                    _cacheWriter = nullptr;

                    if (_cachePartialFile != nullptr)
                    {
                      return concurrency::create_task(_cachePartialFile->RenameAsync(targetFileName, NameCollisionOption::ReplaceExisting), _cancelToken)
                        .then([=](task<void> tsk)
                      {
                        try
                        {
                          tsk.get();
                          return LoadStorageFile(_cachePartialFile);
                        }
                        catch (Platform::Exception^ ex)
                        {
                          return task_from_exception<IRandomAccessStream^>(ref new Platform::InvalidArgumentException(L"cache file failed to rename"));
                        }
                      });
                    }
                    else
                      return task_from_exception<IRandomAccessStream^>(ref new Platform::InvalidArgumentException("cache file was null"));
                  }
                  else
                    return task_from_exception<IRandomAccessStream^>(ref new Platform::InvalidArgumentException("cache writer was null"));
                }
                else
                {
                  if (_inMemoryWriter != nullptr)
                    _inMemoryWriter->Seek(0);
                  return task_from_result(dynamic_cast<IRandomAccessStream^>(_inMemoryWriter));
                }
              }, errorHandler, _cancelToken);
            }, errorHandler, _cancelToken);
          }
          catch (...)
          {
            return task_from_exception<IRandomAccessStream^>(ref new Exception(E_FAIL, L"failed to load initial image"));
          }
        }, errorHandler, _cancelToken);
      }, errorHandler, _cancelToken);
    }, errorHandler, _cancelToken);
  }
}

task<void> ResourceLoader::ReadSomeHttp(IInputStream^ inputStream)
{
  auto errorHandler = make_error_handler<void, Exception^>();
  return continue_task(inputStream->ReadAsync(ref new Buffer(32 * 1024), 32 * 1024, InputStreamOptions::None),
    [=](Windows::Storage::Streams::IBuffer^ bufferResult)
  {
    return continue_void_task(WriteBufferToResultStream(bufferResult, bufferResult->Length < 32 * 1024),
      [=]()
    {
      if (bufferResult->Length >= 32 * 1024)
        return ReadSomeHttp(inputStream);
      else
        return task_from_result();
    }, errorHandler, _cancelToken);

  }, errorHandler, _cancelToken);
}

void ResourceLoader::GetBytesFromBuffer(Windows::Storage::Streams::IBuffer^ buffer,
  std::function<void(uint8_t*, uint32_t)> handler)
{
  ComPtr<Windows::Storage::Streams::IBufferByteAccess> pBufferByteAccess;
  ComPtr<IUnknown> pBuffer((IUnknown*)buffer);
  pBuffer.As(&pBufferByteAccess);
  byte* bufferData;
  pBufferByteAccess->Buffer(&bufferData);
  handler(bufferData, buffer->Length);
}

task<void> ResourceLoader::WriteBufferToResultStream(IBuffer^ buffer, bool finished)
{
  _dataReadHook(_dontBufferReadHook ? nullptr : buffer, finished, _expectedByteCount);
  if (_cacheResult)
  {
    //if we've not opened an output file start writing the stream out to disk with a filename based on the MD5 of the resourceLocator
    //otherwise just do a writeAsync

    if (_cacheWriter == nullptr)
    {
      auto targetFileName = L"deleteme" + ComputeMD5(_resourceLocator) + L".partialDownload";
      return continue_task(ApplicationData::Current->TemporaryFolder->CreateFileAsync(targetFileName, CreationCollisionOption::GenerateUniqueName),
        [=](StorageFile^ storeFile)
      {
        _cachePartialFile = storeFile;
        return continue_task(storeFile->OpenAsync(FileAccessMode::ReadWrite),
          [=](IRandomAccessStream^ openedStream)
        {
          _cacheWriter = openedStream;
          return continue_task(_cacheWriter->WriteAsync(buffer),
            [=](unsigned int count) { return task_from_result(); },
            make_error_handler<void, Exception^>(), _cancelToken);
        }, make_error_handler<void, Exception^>(), _cancelToken);
      }, make_error_handler<void, Exception^>(), _cancelToken);
    }
    else
    {
      return continue_task(_cacheWriter->WriteAsync(buffer),
        [=](unsigned int count) { return task_from_result(); },
        make_error_handler<void, Exception^>(), _cancelToken);
    }
  }
  else
  {
    if (_inMemoryWriter == nullptr)
    {
      _inMemoryWriter = ref new InMemoryRandomAccessStream();
    }
    return continue_task(_inMemoryWriter->WriteAsync(buffer),
      [=](unsigned int count) { return task_from_result(); },
      make_error_handler<void, Exception^>(), _cancelToken);
  }
}


String^ ResourceLoader::ComputeMD5(String^ str)
{
  auto alg = HashAlgorithmProvider::OpenAlgorithm("MD5");
  auto buff = CryptographicBuffer::ConvertStringToBinary(str, BinaryStringEncoding::Utf8);
  auto hashed = alg->HashData(buff);
  auto res = CryptographicBuffer::EncodeToHexString(hashed);
  return res;
}

task<void> ResourceLoader::FailureCacheCleanup()
{
  if (_cacheWriter != nullptr)
  {
    delete _cacheWriter;
    _cacheWriter = nullptr;
  }
  
  if (_cachePartialFile != nullptr)
  {
    return create_task(_cachePartialFile->DeleteAsync())
      .then([=](task<void> tsk2)
    {
      try
      {
        tsk2.get();
      }
      catch (...)
      {
      }
      return task_from_result();
    });
  }
  else
    return task_from_result();
}

void ResourceLoader::StridedCopy(uint8_t* src, Windows::Foundation::Size sourceSize, uint32_t elementSize, uint8_t* dst, Windows::Foundation::Rect desiredRect)
{
  if (desiredRect.Height == sourceSize.Height && desiredRect.Width == sourceSize.Height)
    memcpy(dst, src, static_cast<int>(desiredRect.Height) * static_cast<int>(desiredRect.Width) * elementSize);

  auto stride = static_cast<int>(sourceSize.Width) * elementSize;
  auto writeCursor = dst;
  for (int row = static_cast<int>(desiredRect.Top); row < static_cast<int>(desiredRect.Bottom); row++)
  {
    auto column = static_cast<int>(desiredRect.Left);
    auto srcOffset = (((stride * row) + column) * elementSize);
    memcpy(writeCursor, src + srcOffset, static_cast<int>(desiredRect.Width) * elementSize);
    writeCursor += static_cast<int>(desiredRect.Width) * elementSize;
  }
}

std::chrono::seconds toDuration(DateTime dt)
{
  return std::chrono::seconds((dt.UniversalTime - 116444736000000000LL) / 10000000ULL);
}

bool ends_with(std::wstring const &fullString, std::wstring const &ending)
{
  if (fullString.length() >= ending.length())
  {
    return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
  }
  else
  {
    return false;
  }
}

task<void> ResourceLoader::CleanOldTemps()
{
  return continue_task(ApplicationData::Current->TemporaryFolder->GetFilesAsync(),
    [=](Windows::Foundation::Collections::IVectorView<StorageFile^>^ files)
  {

    auto cutoff = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch() - std::chrono::hours(96));
    auto deletemeCutoff = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch() - std::chrono::hours(8));
    for (auto file : files)
    {
      auto dateCreated = toDuration(file->DateCreated);
      if (dateCreated < cutoff || (dateCreated < deletemeCutoff && starts_with(std::wstring(file->Name->Data()), L"deleteme")))
        create_task(file->DeleteAsync()).then([](task<void> deleteResult) { try { deleteResult.get(); } catch (...) {} });
    }

    //clean up the live tiles
    return continue_task(ApplicationData::Current->LocalFolder->GetFilesAsync(),
      [=](Windows::Foundation::Collections::IVectorView<StorageFile^>^ files)
    {
      for (auto file : files)
      {
        auto fileName = std::wstring(file->Name->Data());
        if (ends_with(fileName, L".jpg") && starts_with(fileName, L"LiveTile"))
        {
          auto dateCreated = toDuration(file->DateCreated);
          if (dateCreated < cutoff)
            create_task(file->DeleteAsync()).then([](task<void> deleteResult) { try { deleteResult.get(); } catch (...) {} });
        }
      }
      return task_from_result();
    });
  });
}