#include "Texture.h"
#include <DDSTextureLoader12.h>


Texture::Texture(const char *pszName) {
    if(pszName)
        Name = pszName;

    Resource = nullptr;
    UploadHeap = nullptr;
    SrvHeapIndex = -1;
}

Texture::~Texture() {
    SAFE_RELEASE(Resource);
    SAFE_RELEASE(UploadHeap);
}

VOID Texture::DisposeUploaders() {
    SAFE_RELEASE(UploadHeap);
}

HRESULT Texture::CreateTextureFromDDSFile(
    ID3D12Device *pd3dDevice,
    ID3D12GraphicsCommandList *pd3dCommandList,
    const wchar_t *pszFileName) {

    HRESULT hr;

    SAFE_RELEASE(Resource);
    SAFE_RELEASE(UploadHeap);

#if 0
    Microsoft::WRL::ComPtr<ID3D12Resource> texture, textureUpload;

    V_RETURN(DirectX::CreateDDSTextureFromFile12(
        pd3dDevice,
        pd3dCommandList,
        pszFileName,
        texture,
        textureUpload
    ));
    Resource = texture.Get();
    Resource->AddRef();
    UploadHeap = textureUpload.Get();
    UploadHeap->AddRef();

#else
    std::unique_ptr<uint8_t[]> ddsData;
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    WCHAR szFilePath[MAX_PATH];

    V_RETURN(FindDemoMediaFileAbsPath(pszFileName, _countof(szFilePath), szFilePath));

    V_RETURN(DirectX::LoadDDSTextureFromFile(pd3dDevice,
        szFilePath, &Resource,
        ddsData, subresources));

    UINT64 uploadBufferSize = GetRequiredIntermediateSize(Resource, 0, (UINT)subresources.size());

    V(pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&UploadHeap)
    ));
    if (FAILED(hr)) {
        SAFE_RELEASE(Resource);
        return hr;
    }

    /// The default buffer state is D3D12_RESOURCE_STATE_COPY_DEST, so no barrier is need here.
    //pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Resource,
    //    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

    UpdateSubresources(pd3dCommandList, Resource, UploadHeap, 0, 0, (UINT)subresources.size(),
        subresources.data());

    pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Resource,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE| D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

#endif

    return hr;
}


HRESULT Texture::CreateTextureFromDDSMemory(
    ID3D12Device *pd3dDevice,
    ID3D12GraphicsCommandList *pd3dCommandList,
    const void *pData, size_t len
) {
#if 0

    return S_OK;
#else
    SAFE_RELEASE(Resource);
    SAFE_RELEASE(UploadHeap);

    std::unique_ptr<uint8_t[]> ddsData;
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    HRESULT hr;

    V_RETURN(DirectX::LoadDDSTextureFromMemory(pd3dDevice,
        (const uint8_t *)pData, len, &Resource, subresources));

    UINT64 uploadBufferSize = GetRequiredIntermediateSize(Resource, 0, (UINT)subresources.size());

    V(pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&UploadHeap)
    ));
    if (FAILED(hr)) {
        SAFE_RELEASE(Resource);
        return hr;
    }

    /// The default buffer state is D3D12_RESOURCE_STATE_COPY_DEST, so no barrier is need here.
    //pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Resource,
    //    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

    UpdateSubresources(pd3dCommandList, Resource, UploadHeap, 0, 0, (UINT)subresources.size(),
        subresources.data());

    pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Resource,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    return hr;
#endif
}
