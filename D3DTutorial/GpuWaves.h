#ifndef __GPUWAVES_H__
#define __GPUWAVES_H__
#include "d3dUtils.h"


class GpuWaves
{
public:
    // Note that m,n should be divisible by 16 so there is no 
    // remainder when we divide into thread groups.
    GpuWaves(
        ID3D12Device *pd3dDevice,
        ID3D12GraphicsCommandList *pCommandList,
        int m,
        int n,
        float dx,
        float dt,
        float speed,
        float damping
        );
    GpuWaves(const GpuWaves &) = delete;
    GpuWaves& operator = (const GpuWaves &) = delete;
    ~GpuWaves();

    void InitCleanup();

    UINT VertexCount() const;
    UINT TriangleCount() const;
    UINT ColumnCount() const;
    UINT RowCount() const;
    FLOAT SpatialStep() const;

    UINT DescriptorHeapCount() const;

    D3D12_GPU_DESCRIPTOR_HANDLE DisplacementMap() const;

    HRESULT BuildDescriptors(
        D3D12_CPU_DESCRIPTOR_HANDLE hCpuHeapStart,
        D3D12_GPU_DESCRIPTOR_HANDLE hGpuHeapStart,
        UINT uCbvSrvUavDescriptorIncrementSize
        );

    void Update(
        FLOAT fTime,
        FLOAT fElaspedTime,
        ID3D12GraphicsCommandList *pCommandList,
        ID3D12RootSignature *pRootSignature,
        ID3D12PipelineState *pso
    );

    void Disturb(
        ID3D12GraphicsCommandList *pCommandList,
        ID3D12RootSignature *pRootSignature,
        ID3D12PipelineState *pso,
        UINT i, UINT j, FLOAT fMagnitude
    );

private:
    HRESULT BuildResources(ID3D12GraphicsCommandList *pd3dCommandList);

    UINT m_uNumRows;
    UINT m_uNumCols;
    UINT m_uVertexCount;
    UINT m_uTriangleCount;

    FLOAT m_fK[3];
    FLOAT m_fTimeStep;
    FLOAT m_fSpatialStep;

    ID3D12Device *m_pd3dDevice;

    ID3D12Resource *m_pPrevSol;
    ID3D12Resource *m_pCurrSol;
    ID3D12Resource *m_pNextSol;

    ID3D12Resource *m_pPrevUploadBuffer;
    ID3D12Resource *m_pCurrUploadBuffer;

    CD3DX12_GPU_DESCRIPTOR_HANDLE m_hPrevSolSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_hCurrSolSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_hNextSolSrv;

    CD3DX12_GPU_DESCRIPTOR_HANDLE m_hPrevSolUav;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_hCurrSolUav;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_hNextSolUav;
};



#endif /* __GPUWAVES_H__ */
