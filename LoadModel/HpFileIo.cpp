#include <windows.h>
#include <cstdint>
#include <algorithm>
#include "HpFileIo.h"

#undef min
#undef max

#define _ALIGN_UP(ptr, alignment)                                                                                      \
  (ULONG_PTR)(((ULONG_PTR)(ptr) + (ULONG_PTR)(alignment)-1) & ~((ULONG_PTR)(alignment)-1))
#define _ALIGN_DOWN(ptr, alignment) (ULONG_PTR)((ULONG_PTR)(ptr) & ~((ULONG_PTR)(alignment)-1))

#define _DIRECTIO_NO_BUFFERING 1

namespace HpFileIo {

enum IO_RESULT_TYPE
{
  IO_RESULT_TYPE_NONE,
  IO_RESULT_TYPE_MAPPING,
  IO_RESULT_TYPE_DIRECT_IO
};

struct FileDataBlobImpl: public IFileDataBlob {

  static FileDataBlobImpl *Create() {
    return new FileDataBlobImpl;
  }

  ULONG Release() override {
    LONG refcnt = InterlockedDecrement(&Refcnt);
    if (refcnt == 0) {
      switch (IoType) {
      case IO_RESULT_TYPE_MAPPING:
        CloseHandle(Mapped.hFileMapping);
        break;
      case IO_RESULT_TYPE_DIRECT_IO:
        VirtualFree(Pages.pAlloc, 0, MEM_FREE);
        break;
      }

      delete this;
    }
    return refcnt;
  }
  ULONG AddRef() override {
    return InterlockedIncrement(&Refcnt);
  }
  void *GetBufferPointer() const override {
    return Data;
  }
  size_t GetBufferSize() const override {
    return Size;
  }

  ULONG Refcnt;
  IO_RESULT_TYPE IoType;
  void *Data;
  size_t Size;
  union {
    struct {
      HANDLE hFileMapping;
      VOID *pMappedView;
    } Mapped;
    struct {
      VOID *pAlloc;
    } Pages;
  };

private:
  FileDataBlobImpl() {
    Refcnt = 1;
    IoType = IO_RESULT_TYPE_NONE;
    Data   = nullptr;
    Size   = 0;
  }
  ~FileDataBlobImpl() {}
};

HRESULT _MapFileDirectly(LPCWSTR pFileName, ptrdiff_t iOffsetInBytes, size_t iReqSizeInBytes,
                         IFileDataBlob **ppResult) {

  HANDLE hFile;
  size_t fileSize;
  SYSTEM_INFO sysInfo;
  LARGE_INTEGER startOffset, endOffset;
  LARGE_INTEGER reqSize;
  HANDLE hFileMapping = NULL;
  VOID *pMappedView   = NULL;
  FileDataBlobImpl *pResult;

  hFile = CreateFileW(pFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return HRESULT_FROM_WIN32(GetLastError());

  // validate arguments
  GetFileSizeEx(hFile, (PLARGE_INTEGER)&fileSize);
  if (iOffsetInBytes < 0 || (size_t)iOffsetInBytes > fileSize ||
      ((size_t)iOffsetInBytes + iReqSizeInBytes) > fileSize) {
    CloseHandle(hFile);
    return E_FAIL;
  }

  GetSystemInfo(&sysInfo);
  startOffset.QuadPart = _ALIGN_DOWN(iOffsetInBytes, sysInfo.dwAllocationGranularity);
  if (iReqSizeInBytes) {
    endOffset.QuadPart = iOffsetInBytes + iReqSizeInBytes;
  } else {
    GetFileSizeEx(hFile, &endOffset);
    iReqSizeInBytes = endOffset.QuadPart - iOffsetInBytes;
  }

  reqSize.QuadPart = (endOffset.QuadPart - startOffset.QuadPart);

  hFileMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY, reqSize.HighPart, reqSize.LowPart, NULL);
  CloseHandle(hFile);
  if (hFileMapping == NULL)
    return HRESULT_FROM_WIN32(GetLastError());

  pMappedView = MapViewOfFile(hFileMapping, FILE_MAP_READ, startOffset.HighPart, startOffset.LowPart,
                              _ALIGN_UP(reqSize.QuadPart, sysInfo.dwPageSize));
  if (pMappedView == NULL) {
    CloseHandle(hFileMapping);
    return HRESULT_FROM_WIN32(GetLastError());
  }

  pResult                      = FileDataBlobImpl::Create();
  pResult->Data                = (uint8_t *)pMappedView + (iOffsetInBytes - startOffset.QuadPart);
  pResult->Size                = iReqSizeInBytes;
  pResult->IoType              = IO_RESULT_TYPE_MAPPING;
  pResult->Mapped.hFileMapping = hFileMapping;
  pResult->Mapped.pMappedView  = pMappedView;
  *ppResult                    = pResult;

  return S_OK;
}

HRESULT _ReadFileDirectly(LPCWSTR pFileName, ptrdiff_t iOffsetInBytes, size_t iReqSizeInBytes,
                          IFileDataBlob **ppResult) {
  HRESULT hr = S_OK;
  HANDLE hFile;
  size_t fileSize;
  SYSTEM_INFO sysInfo;
  LARGE_INTEGER startOffset, endOffset;
  LARGE_INTEGER reqSize;
  LARGE_INTEGER offset;
  uint8_t *pv;
  LONGLONG blockSize;
  OVERLAPPED ov[3];
  HANDLE hEvents[3];
  int i, ovCount;
  int endBits;
  int rc;
  FileDataBlobImpl *pResult;

  GetSystemInfo(&sysInfo);
  blockSize = 2 * sysInfo.dwPageSize;

  hFile = CreateFileW(pFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
#if _DIRECTIO_NO_BUFFERING
                      FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED
#else
                      FILE_FLAG_OVERLAPPED
#endif
                      ,
                      NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return HRESULT_FROM_WIN32(GetLastError());

  // validate arguments
  GetFileSizeEx(hFile, (PLARGE_INTEGER)&fileSize);
  if (iOffsetInBytes < 0 || (size_t)iOffsetInBytes > fileSize ||
      ((size_t)iOffsetInBytes + iReqSizeInBytes) > fileSize) {
    CloseHandle(hFile);
    return E_FAIL;
  }

#if _DIRECTIO_NO_BUFFERING
  startOffset.QuadPart = _ALIGN_DOWN(iOffsetInBytes, sysInfo.dwPageSize);
  if (iReqSizeInBytes) {
    endOffset.QuadPart = _ALIGN_UP(iOffsetInBytes + (ptrdiff_t)iReqSizeInBytes, sysInfo.dwPageSize);
  } else {
    endOffset.QuadPart = fileSize;
    iReqSizeInBytes    = endOffset.QuadPart - iOffsetInBytes;
    endOffset.QuadPart = _ALIGN_UP(endOffset.QuadPart, sysInfo.dwPageSize);
  }
#else
  startOffset.QuadPart = iOffsetInBytes;
  if (iReqSizeInBytes) {
    endOffset.QuadPart = iOffsetInBytes + (ptrdiff_t)iReqSizeInBytes;
  } else {
    endOffset.QuadPart = fileSize;
    iReqSizeInBytes = endOffset.QuadPart - iOffsetInBytes;
  }
#endif

  reqSize.QuadPart = _ALIGN_UP(endOffset.QuadPart - startOffset.QuadPart, sysInfo.dwPageSize);

  pv = (uint8_t *)VirtualAlloc(NULL, reqSize.QuadPart, MEM_COMMIT, PAGE_READWRITE);
  if (pv == NULL) {
    CloseHandle(hFile);
    return HRESULT_FROM_WIN32(GetLastError());
  }

  ZeroMemory(ov, sizeof(ov));
  ZeroMemory(hEvents, sizeof(hEvents));

  offset  = startOffset;
  ovCount = 0;
  endBits = 0;
  for (i = 0; i < 3; ++i, ++ovCount) {
    if (offset.QuadPart >= endOffset.QuadPart)
      break;

    if ((ov[i].hEvent = CreateEventExW(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS)) == NULL) {
      hr = HRESULT_FROM_WIN32(GetLastError());
      goto rollback;
    }
    hEvents[i]       = ov[i].hEvent;
    ov[i].Offset     = offset.LowPart;
    ov[i].OffsetHigh = offset.HighPart;

    if (!ReadFile(hFile, pv + (offset.QuadPart - startOffset.QuadPart),
                  (DWORD)std::min(blockSize, endOffset.QuadPart - offset.QuadPart), NULL, &ov[i]) &&
        (rc = GetLastError()) != ERROR_IO_PENDING) {
      CloseHandle(hEvents[i]);
      hr = HRESULT_FROM_WIN32(rc);
      goto rollback;
    }

    offset.QuadPart += blockSize;
    endBits |= (1 << i);
  }

  while (endBits) {

    rc = WaitForMultipleObjects(ovCount, hEvents, FALSE, INFINITE);
    if (rc == WAIT_TIMEOUT) {
      return E_FAIL;
    } else if (rc == WAIT_FAILED) {
      hr = HRESULT_FROM_WIN32(GetLastError());
      goto rollback;
    }

    i = rc - WAIT_OBJECT_0;
    for (; i < ovCount; ++i) {
      rc = WaitForSingleObject(hEvents[i], 0);
      if (rc != WAIT_OBJECT_0)
        continue;
      if (!HasOverlappedIoCompleted(&ov[i]))
        return E_FAIL;

      ResetEvent(hEvents[i]);
      offset.LowPart  = ov[i].Offset;
      offset.HighPart = ov[i].OffsetHigh;
      offset.QuadPart += ovCount * blockSize;

      if (offset.QuadPart < endOffset.QuadPart) {
        ov[i].Offset     = offset.LowPart;
        ov[i].OffsetHigh = offset.HighPart;

        if (!ReadFile(hFile, pv + (offset.QuadPart - startOffset.QuadPart),
                      (DWORD)std::min(blockSize, endOffset.QuadPart - offset.QuadPart), NULL, &ov[i]) &&
            (rc = GetLastError()) != ERROR_IO_PENDING) {
          hr = HRESULT_FROM_WIN32(rc);
          goto rollback;
        }
      } else {
        endBits &= ~(1 << i);
      }
    }
  }

  pResult = FileDataBlobImpl::Create();
#if _DIRECTIO_NO_BUFFERING
  pResult->Data = pv + (startOffset.QuadPart - iOffsetInBytes);
#else
  pResult->Data = pv;
#endif
  pResult->Size         = iReqSizeInBytes;
  pResult->IoType       = IO_RESULT_TYPE_DIRECT_IO;
  pResult->Pages.pAlloc = pv;
  *ppResult             = pResult;

  for (i = 0; i < ovCount; ++i)
    CloseHandle(hEvents[i]);
  CloseHandle(hFile);

  return hr;

rollback:
  VirtualFree(pv, 0, MEM_FREE);
  for (i = 0; i < ovCount; ++i)
    CloseHandle(hEvents[i]);
  CloseHandle(hFile);
  return hr;
}

_Use_decl_annotations_
HRESULT ReadFileDirectly(_In_ const wchar_t *pFileName, _In_ ptrdiff_t iOffsetInBytes, _In_ size_t iRequestSizeInBytes,
                         IFileDataBlob **ppResult) {

  HRESULT hr;

  if (ppResult == nullptr)
    return E_INVALIDARG;

  hr = _MapFileDirectly(pFileName, iOffsetInBytes, iRequestSizeInBytes, ppResult);
  if (FAILED(hr))
    hr = _ReadFileDirectly(pFileName, iOffsetInBytes, iRequestSizeInBytes, ppResult);
  return hr;
}

} // namespace HpFileIo