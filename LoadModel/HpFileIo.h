#pragma once
#include <cstdlib>

namespace HpFileIo {

struct IFileDataBlob {
  virtual ULONG  Release()                = 0;
  virtual ULONG  AddRef()                 = 0;
  virtual void * GetBufferPointer() const = 0;
  virtual size_t GetBufferSize() const    = 0;
};

HRESULT ReadFileDirectly(_In_ const wchar_t *pFileName,
                         _In_ ptrdiff_t      iOffsetInBytes, // File offset in content block,
                         _In_ size_t iRequestSizeInBytes,    // File content size, when 0 is specified, read to file end
                         _Out_ IFileDataBlob **ppResult);

}; // namespace HpFileIo