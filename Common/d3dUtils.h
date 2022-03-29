#ifndef __D3D_UTILS_HPP__
#define __D3D_UTILS_HPP__

// d3d12 common header.
#include <dxgi1_6.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <comdef.h> /// For _com_ptr
#include <wrl.h> /// For ComPtr

#include <cassert>
#include <memory>

#include "Common.h"

class D3D12MAAllocator;
class D3D12MAResourceSPtr;

#if defined(_DEBUG) || defined(DEBUG)
#define RT_ASSERT(expr) assert(expr)
#ifndef V
#define V(x)           (FAILED(hr = (x)) && (DebugBreak(), 1) ? DXTraceW(__FILEW__, __LINE__, hr, L#x, true) : (HRESULT)0)
#define V2(x, ...)     (FAILED(hr = ((x), ##__VA_ARGS__)) && (DebugBreak(), 1) ? DXTraceW(__FILEW__, __LINE__, hr, L#x###__VA_ARGS__, true) : (HRESULT)0)
#endif /* V */

#ifndef V_RETURN
#define V_RETURN(x)    do { if(FAILED(hr = (x)) && (DebugBreak(), 1)) { DXTraceW(__FILEW__, __LINE__, hr, L#x, true); return hr; } } while(0)
#define V_RETURN2(x, ...) do { if(FAILED(hr = ((x), ##__VA_ARGS__)) && (DebugBreak(), 1)) { DXTraceW(__FILEW__, __LINE__, hr, L#x###__VA_ARGS__, true); return hr; } } while(0)
#endif /* V_RETURN2 */
#else
#define RT_ASSERT(expr) ((void)0)
#ifndef V
#define V(x)                (hr = (x))
#define V2(x, ...)           (hr = (x, ##__VA_ARGS__))
#endif
#ifndef V_RETURN
#define V_RETURN(x)         do { if(FAILED(hr = (x))) {return hr; } } while(0)
#define V_RETURN2(x, ...)   do { if(FAILED(hr=((x), ##__VA_ARGS__))) { return hr; } } while(0)
#endif
#endif

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)        ((p) ? delete (p), p = nullptr : nullptr)
#endif
#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) ((p) ? delete [](p), p = nullptr: nullptr)
#endif
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      ((p) ? (p)->Release(), p = nullptr : nullptr)
#endif

#ifndef SAFE_ADDREF
#define SAFE_ADDREF(p)       ((p) ? (p)->AddRef() : (ULONG)0)
#endif

// Use DXUT_SetDebugName() to attach names to D3D objects for use by 
// SDKDebugLayer, PIX's object table, etc.
#if defined(_DEBUG) || defined(DEBUG)
inline void DX_SetDebugName(IDXGIObject* pObj, const CHAR* pstrName) {
    if (pObj)
        pObj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(pstrName), pstrName);
}
inline void DX_SetDebugName(ID3D12Device *pObj, const CHAR *pstrName) {
    if (pObj) {
        pObj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(pstrName), pstrName);
    }
}
inline void DX_SetDebugName(ID3D12DeviceChild *pObj, const CHAR *pstrName) {
    if (pObj) {
        pObj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(pstrName), pstrName);
    }
}

#else
#define DX_SetDebugName( pObj, pstrName )
#endif

#define DX_TRACEA(fmt, ...) DXOutputDebugStringA(fmt, ##__VA_ARGS__)
#define DX_TRACEW(fmt, ...) DXOutputDebugStringW(fmt, ##__VA_ARGS__)

#define DX_TRACE DX_TRACEW

extern HRESULT WINAPI DXTraceW(_In_z_ const WCHAR* strFile, _In_ DWORD dwLine, _In_ HRESULT hr,
    _In_opt_ const WCHAR* strMsg, _In_ bool bPopMsgBox);

extern void DXOutputDebugStringA(LPCSTR fmt, ...);
extern void DXOutputDebugStringW(LPCWSTR fmt, ...);

inline ptrdiff_t AlignUp(ptrdiff_t ptr, size_t alignment) {
  size_t mask = alignment - 1;
  RT_ASSERT((alignment & mask) == 0 && "alignment must be pow of 2");
  if((ptr & mask) != 0)
    ptr = (ptr + mask) & ~mask;
  return ptr;
}

inline ptrdiff_t AlignDown(ptrdiff_t ptr, size_t alignment) {
  size_t mask = alignment - 1;
  RT_ASSERT((alignment & mask) == 0 && "alignment must be pow of 2");
  if((ptr & mask) != 0)
    ptr = ptr & ~mask;
  return ptr;
}

struct Unknown12
{
  ULONG AddRef();
  ULONG Release();

protected:
  Unknown12();
  virtual ~Unknown12();
private:
  Unknown12(const Unknown12 &) = delete;
  Unknown12(Unknown12 &&) = delete;
  Unknown12 &operator= (const Unknown12 &) = delete;
  Unknown12 &operator= (Unknown12&&) = delete;

  volatile ULONG m_uRefcnt;
};

struct NonCopyable {
  NonCopyable() = default;
  NonCopyable(const NonCopyable &) = delete;
  NonCopyable(NonCopyable &&) = delete;
  NonCopyable operator = (const NonCopyable &) = delete;
  NonCopyable& operator = (NonCopyable &&) = delete;
};

namespace d3dUtils {

  class DxcCompilerWrapper {
  public:
    DxcCompilerWrapper();
    ~DxcCompilerWrapper();

    HRESULT CreateCompiler();

    HRESULT CompileFromFile(
      _In_ LPCWSTR pFileNname,                      // Source text to compile
      _In_ LPCWSTR pEntryPoint,                     // entry point name
      _In_ LPCWSTR pTargetProfile,                  // shader profile to compile
      _In_count_(argCount) LPCWSTR *pArguments,     // Array of pointers to arguments
      _In_ UINT32 argCount,                         // Number of arguments
      _In_count_(defineCount) const DxcDefine *pDefines,  // Array of defines
      _In_ UINT32 defineCount,                      // Number of defines
      _In_opt_ IDxcIncludeHandler *pIncludeHandler, // user-provided interface to handle #include directives (optional)
      _COM_Outptr_ IDxcBlob **ppResult,              // Result buffer
      _COM_Outptr_opt_ IDxcBlob **ppErrorBlob            // Store possible error.
    );

  private:
    IDxcCompiler *m_pDxcCompiler;
    IDxcLibrary *m_pDxcLibrary;
  };

  extern
  HRESULT CompileShaderFromFile(
    _In_ LPCWSTR                pFileName,
    _In_ const D3D_SHADER_MACRO *pDefines,
    _In_ ID3DInclude            *pInclude,
    _In_ LPCSTR                 pEntrypoint,
    _In_ LPCSTR                 pTarget,
    _In_ UINT                   Flags1,
    _In_ UINT                   Flags2,
    _Out_ ID3DBlob               **ppCode,
    _Out_ ID3DBlob               **ppErrorMsgs
  );

  extern HRESULT CreateDefaultBuffer(
      ID3D12Device *pd3dDevice,
      ID3D12GraphicsCommandList *pCmdList,
      const void *pInitData,
      UINT_PTR uByteSize,
      ID3D12Resource **ppUploadBuffer,
      ID3D12Resource **ppDefaultBuffer
  );

  extern UINT CalcConstantBufferByteSize(UINT uByteSize);
};

#endif /* __D3D_UTILS_HPP__ */