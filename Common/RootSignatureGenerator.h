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

  static D3D12_DESCRIPTOR_RANGE1 ComposeConstBufferViewRange(
    _In_ UINT numDescriptors,
    _In_ UINT baseShaderRegister,
    _In_opt_ UINT registerSpace = 0,
    _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
    _In_opt_ UINT offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
  );
  static D3D12_DESCRIPTOR_RANGE1 ComposeShaderResourceViewRange(
    _In_ UINT numDescriptors,
    _In_ UINT baseShaderRegister,
    _In_opt_ UINT registerSpace = 0,
    _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
    _In_opt_ UINT offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
  );
  static D3D12_DESCRIPTOR_RANGE1 ComposeUnorderedAccessViewRange(
    _In_ UINT numDescriptors,
    _In_ UINT baseShaderRegister,
    _In_opt_ UINT registerSpace = 0,
    _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
    _In_opt_ UINT offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
  );
  static D3D12_DESCRIPTOR_RANGE1 ComposeSampler(
    _In_ UINT numDescriptors,
    _In_ UINT baseShaderRegister,
    _In_opt_ UINT registerSpace = 0,
    _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
    _In_opt_ UINT offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
  );
  static D3D12_DESCRIPTOR_RANGE1 ComposeDesciptorRange(
    _In_ D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
    _In_ UINT numDescriptors,
    _In_ UINT baseShaderRegister,
    _In_opt_ UINT registerSpace = 0,
    _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
    _In_opt_ UINT offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
  );

  VOID AddStaticSamples(
    _In_ UINT uNumStaticSamples,
    _In_ const D3D12_STATIC_SAMPLER_DESC *pStaticSamples
  );
  VOID AddStaticSamples(
    _In_ std::initializer_list<D3D12_STATIC_SAMPLER_DESC> aStaticSamples
  );

  static D3D12_STATIC_SAMPLER_DESC ComposeStaticSampler(
    UINT shaderRegister,
    _In_opt_ D3D12_FILTER filter = D3D12_FILTER_ANISOTROPIC,
    _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    _In_opt_ FLOAT mipLODBias = 0,
    _In_opt_ UINT maxAnisotropy = 16,
    _In_opt_ D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
    _In_opt_ D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
    _In_opt_ FLOAT minLOD = 0.f,
    _In_opt_ FLOAT maxLOD = D3D12_FLOAT32_MAX,
    _In_opt_ D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
    _In_opt_ UINT registerSpace = 0
  );

  static D3D12_STATIC_SAMPLER_DESC ComposeStaticSampler2(
    UINT shaderRegister,
    _In_opt_ UINT registerSpace = 0,
    _In_opt_ D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
    _In_opt_ D3D12_FILTER filter = D3D12_FILTER_ANISOTROPIC,
    _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    _In_opt_ FLOAT mipLODBias = 0,
    _In_opt_ UINT maxAnisotropy = 16,
    _In_opt_ D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
    _In_opt_ D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
    _In_opt_ FLOAT minLOD = 0.f,
    _In_opt_ FLOAT maxLOD = D3D12_FLOAT32_MAX
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

