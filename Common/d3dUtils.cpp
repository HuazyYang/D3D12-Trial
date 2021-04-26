#include "d3dUtils.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <comdef.h>
#include <fstream>
#include <sstream>

#define BUFFER_SIZE 3000

static LPCWSTR DXGetErrorStringW(HRESULT hr) {
    static WCHAR s_wcsError[1024];

    _com_error error(hr);

    wcscpy_s(s_wcsError, _countof(s_wcsError), error.ErrorMessage());

    return s_wcsError;
}

//-----------------------------------------------------------------------------
HRESULT WINAPI DXTraceW(_In_z_ const WCHAR* strFile, _In_ DWORD dwLine, _In_ HRESULT hr,
    _In_opt_ const WCHAR* strMsg, _In_ bool bPopMsgBox)
{
    WCHAR strBufferFile[MAX_PATH];
    WCHAR strBufferLine[128];
    WCHAR strBufferError[256];
    WCHAR strBufferMsg[1024];
    WCHAR strBuffer[BUFFER_SIZE];

    swprintf_s(strBufferLine, 128, L"%lu", dwLine);
    if (strFile)
    {
        swprintf_s(strBuffer, BUFFER_SIZE, L"%ls(%ls): ", strFile, strBufferLine);
        OutputDebugStringW(strBuffer);
    }

    size_t nMsgLen = (strMsg) ? wcsnlen_s(strMsg, 1024) : 0;
    if (nMsgLen > 0)
    {
        OutputDebugStringW(strMsg);
        OutputDebugStringW(L" ");
    }

    swprintf_s(strBufferError, 256, L"%ls (0x%0.8x)", DXGetErrorStringW(hr), hr);
    swprintf_s(strBuffer, BUFFER_SIZE, L"hr=%ls", strBufferError);
    OutputDebugStringW(strBuffer);

    OutputDebugStringW(L"\n");

    if (bPopMsgBox)
    {
        wcscpy_s(strBufferFile, MAX_PATH, L"");
        if (strFile)
            wcscpy_s(strBufferFile, MAX_PATH, strFile);

        wcscpy_s(strBufferMsg, 1024, L"");
        if (nMsgLen > 0)
            swprintf_s(strBufferMsg, 1024, L"Calling: %ls\n", strMsg);

        swprintf_s(strBuffer, BUFFER_SIZE, L"File: %ls\nLine: %ls\nError Code: %ls\n%lsDo you want to debug the application?",
            strBufferFile, strBufferLine, strBufferError, strBufferMsg);

        /*int nResult = */MessageBoxW(GetForegroundWindow(), strBuffer, L"Unexpected error encountered", MB_ICONERROR);
        //if (nResult == IDYES)
        //    DebugBreak();
    }

    return hr;
}

void DXOutputDebugStringA(LPCSTR fmt, ...) {
    va_list ap;
    CHAR buff[256];
    CHAR *pb;
    int cch, cchRet;

    va_start(ap, fmt);
    pb = buff;
    cch = _countof(buff);
    do {
        cchRet = vsnprintf(pb, cch, fmt, ap);
        if (cchRet >= cch) {
            cch = cchRet + 1;
            pb = new CHAR[cch];
        } else
            break;
    } while (1);
    OutputDebugStringA(pb);
    va_end(ap);

    if (pb != buff)
        delete[]pb;
}


void DXOutputDebugStringW(LPCWSTR fmt, ...) {
    va_list ap;
    WCHAR buff[256];
    WCHAR *pb;
    int cch, cchRet;

    va_start(ap, fmt);
    pb = buff;
    cch = _countof(buff);
    do {
        cchRet = vswprintf_s(pb, cch, fmt, ap);
        if (cchRet >= cch) {
            cch = cchRet + 1;
            pb = new WCHAR[cch];
        } else
            break;
    } while (1);
    OutputDebugStringW(pb);
    va_end(ap);

    if (pb != buff)
        delete[]pb;
}

Unknown12::Unknown12()
{
    m_uRefcnt = 1;
}

Unknown12::~Unknown12() {}

ULONG Unknown12::AddRef() {

    HRESULT hr;
    ULONG rc = 0;
    if (!this) {
        V(E_INVALIDARG);
    } else {
        rc = InterlockedIncrement(&m_uRefcnt);
    }

    return rc;
}

ULONG Unknown12::Release() {
    ULONG rc = 0;

    if (this && (rc = InterlockedDecrement(&m_uRefcnt)) == 0) {
        delete this;
    }
    return rc;
}

namespace d3dUtils {

  DxcCompilerWrapper::DxcCompilerWrapper() {
    m_pDxcCompiler = nullptr;
    m_pDxcLibrary = nullptr;
  }

  DxcCompilerWrapper::~DxcCompilerWrapper() {
    SAFE_RELEASE(m_pDxcCompiler);
    SAFE_RELEASE(m_pDxcLibrary);
  }

  HRESULT DxcCompilerWrapper::CreateCompiler() {

    HRESULT hr;

    V_RETURN(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_pDxcCompiler)));
    V_RETURN(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&m_pDxcLibrary)));

    return hr;
  }

  HRESULT DxcCompilerWrapper::CreateIncludeHandler(IDxcIncludeHandler **ppIncludeHandler) {

    HRESULT hr;

    if (!m_pDxcLibrary)
      V_RETURN(E_INVALIDARG);

    V_RETURN(m_pDxcLibrary->CreateIncludeHandler(ppIncludeHandler));

    return hr;
  }

  HRESULT DxcCompilerWrapper::CompileFromFile(
    _In_ LPCWSTR pFileName,
    _In_ LPCWSTR pEntryPoint,
    _In_ LPCWSTR pTargetProfile,
    _In_count_(argCount) LPCWSTR *pArguments,
    _In_ UINT32 argCount,
    _In_count_(defineCount) const DxcDefine *pDefines,
    _In_ UINT32 defineCount,
    _In_opt_ IDxcIncludeHandler *pIncludeHandler,
    _COM_Outptr_ IDxcBlob **ppResult,
    _COM_Outptr_ IDxcBlob **ppErrorBlob
  ) {

    HRESULT hr;
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> pEncodingBlob;
    UINT32 uCodePage = 0;
    WCHAR szFilePath[MAX_PATH];

    if (ppResult) SAFE_RELEASE(*ppResult);
    if (ppErrorBlob) SAFE_RELEASE(*ppErrorBlob);

    V_RETURN(FindDemoMediaFileAbsPath(pFileName, _countof(szFilePath), szFilePath));

    V(m_pDxcLibrary->CreateBlobFromFile(szFilePath, &uCodePage, &pEncodingBlob));

    /// Compile
    Microsoft::WRL::ComPtr<IDxcOperationResult> pResult = nullptr;
    V_RETURN(m_pDxcCompiler->Compile(pEncodingBlob.Get(), szFilePath, pEntryPoint, pTargetProfile, pArguments,
      argCount, pDefines, defineCount, pIncludeHandler, pResult.GetAddressOf()));

    /// Verify the result.
    HRESULT resultCode;
    V_RETURN(pResult->GetStatus(&resultCode));
    if (FAILED(resultCode)) {
      Microsoft::WRL::ComPtr<IDxcBlobEncoding> pErrorBlob;
      pResult->GetErrorBuffer(pErrorBlob.GetAddressOf());
      std::string strErrBuffer;

      strErrBuffer.copy((char *)pErrorBlob->GetBufferPointer(), pErrorBlob->GetBufferSize());
      strErrBuffer.push_back(0);

      DXOutputDebugStringA("Shader compile error: %s\n", strErrBuffer.c_str());

      pErrorBlob->QueryInterface(IID_PPV_ARGS(ppErrorBlob));

      hr = resultCode;
      return hr;
    }

    if (ppResult)
      pResult->GetResult(ppResult);

    return hr;
  }

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
  ) {
    WCHAR szFullPath[MAX_PATH];
    HRESULT hr;

    V_RETURN(FindDemoMediaFileAbsPath(pFileName, _countof(szFullPath), szFullPath));

    return D3DCompileFromFile(
      szFullPath,
      pDefines,
      pInclude,
      pEntrypoint,
      pTarget,
      Flags1,
      Flags2,
      ppCode,
      ppErrorMsgs
    );
  }

    HRESULT CreateDefaultBuffer(
        ID3D12Device *pd3dDevice,
        ID3D12GraphicsCommandList *pCmdList,
        const void *pInitData,
        UINT_PTR uByteSize,
        ID3D12Resource **ppUploadBuffer,
        ID3D12Resource **ppDefaultBuffer
    ) {
        HRESULT hr;

        SAFE_RELEASE(*ppUploadBuffer);
        SAFE_RELEASE(*ppDefaultBuffer);

        /// Create the actual default buffer resource.
        V_RETURN(pd3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uByteSize),
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(ppDefaultBuffer)
        ));

        /// In oder to copy CPU memory into our default buffer,
        /// we need to create an intermediate upload heap.
        V(pd3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uByteSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(ppUploadBuffer)
        ));
        if (FAILED(hr)) {
            SAFE_RELEASE(*ppDefaultBuffer);
            *ppDefaultBuffer = nullptr;
            return hr;
        }

        /// Describe the data we want to copy into the
        /// default buffer.
        D3D12_SUBRESOURCE_DATA subResourceData = {};
        subResourceData.pData = pInitData;
        subResourceData.RowPitch = uByteSize;
        subResourceData.SlicePitch = subResourceData.RowPitch;

        /// Schedule to copy the data to the default buffer
        /// resource.
        /// At a high level, the helper function UpdateSubresources
        /// will copy the CPU memory into the intermediate upload
        /// heap.
        /// Then, using ID3D12CommandList::CopySubresourceRegion,
        /// the intermediate upload heap will be copied into mBuffer.

        pCmdList->ResourceBarrier(1,
            &CD3DX12_RESOURCE_BARRIER::Transition(*ppDefaultBuffer,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_COPY_DEST));

        UpdateSubresources<1>(pCmdList, *ppDefaultBuffer, *ppUploadBuffer,
            0, 0, 1, &subResourceData);
        pCmdList->ResourceBarrier(1,
            &CD3DX12_RESOURCE_BARRIER::Transition(*ppDefaultBuffer,
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_GENERIC_READ));

        /// Node: *ppUploadBuffer has to be kept alive after the
        /// above function calls because the command list has not
        /// been executed yey that performs the actual copy.
        /// The caller can Release the *ppUploadBuffer after it
        /// knows the copy has been executed.
        return hr;
    }


    UINT CalcConstantBufferByteSize(UINT byteSize)
    {
        return (UINT)AlignUp(byteSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    }
}