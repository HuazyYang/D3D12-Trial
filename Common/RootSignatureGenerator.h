#pragma once
#include "d3dUtils.h"
#include <vector>

///
/// Assemble the root signature using root parameters.
///
class RootSignatureGenerator: private NonCopyable
{
public:
  /// Reserve some buffer to hold the root parameters and descriptor ranges for
  /// further usage.
  RootSignatureGenerator();

  VOID Reset(
    _In_opt_ UINT maxParamCount = 8,
    _In_opt_ UINT maxDescriptorRangeCount = 8
  );

  VOID AddRootConstants(
    _In_ UINT Num32BitValues,
    _In_ UINT ShaderRegister,
    _In_opt_ UINT RegisterSpace = 0,
    _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
  );
  VOID AddConstBufferView(
    _In_ UINT ShaderRegister,
    _In_opt_ UINT RegisterSpace = 0,
    _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
    _In_opt_ D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE
  );
  VOID AddShaderResourceView(
    _In_ UINT ShaderRegister,
    _In_opt_ UINT RegisterSpace = 0,
    _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
    _In_opt_ D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE
  );
  VOID AddUnorderedAccessView(
    _In_ UINT ShaderRegister,
    _In_opt_ UINT RegisterSpace = 0,
    _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
    _In_opt_ D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE
  );

  VOID AddDescriptorTable(
    _In_ std::initializer_list<D3D12_DESCRIPTOR_RANGE1> aDescriptorRange,
    _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
  );

  VOID AddDescriptorTable(
    _In_ UINT uNumDescriptorRange,
    _In_ const D3D12_DESCRIPTOR_RANGE1 *pDescriptorRange,
    _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
  );

  VOID AddStaticSamples(
    _In_ UINT uNumStaticSamples,
    _In_ const D3D12_STATIC_SAMPLER_DESC *pStaticSamples
  );
  VOID AddStaticSamples(
    _In_ std::initializer_list<D3D12_STATIC_SAMPLER_DESC> aStaticSamples
  );

  HRESULT Generate(
    _In_ ID3D12Device *pd3dDevice,
    _In_ D3D12_ROOT_SIGNATURE_FLAGS flags,
    _COM_Outptr_ ID3D12RootSignature **ppRootSignature
  );

  HRESULT Generate(
    _In_ ID3D12Device *pd3dDevice,
    _In_ LPCSTR pszSource,
    _In_ DWORD dwSourceSizeInBytes,
    _In_ LPCSTR pszDefineEntry, /// The directive "define" the macro entry point.
    _COM_Outptr_ ID3D12RootSignature **ppRootSignature
  );

private:
  std::vector<D3D12_ROOT_PARAMETER1> m_aRootParameters;
  std::vector<D3D12_DESCRIPTOR_RANGE1> m_aDescriptorRangeStorage;
  std::vector<D3D12_STATIC_SAMPLER_DESC> m_aStaticSamples;
};

