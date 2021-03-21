#include "RootSignatureGenerator.h"

RootSignatureGenerator::RootSignatureGenerator() {
  Reset(8, 8);
}

VOID RootSignatureGenerator::Reset(
  _In_opt_ UINT maxParamCount/* = 8*/,
  _In_opt_ UINT maxDescriptorRangeCount/* = 8*/
) {
  m_aRootParameters.clear();
  m_aDescriptorRangeStorage.clear();
  m_aRootParameters.reserve(maxParamCount);
  m_aDescriptorRangeStorage.reserve(maxDescriptorRangeCount);

  m_aStaticSamples.clear();
}

VOID RootSignatureGenerator::AddRootConstants(
  _In_ UINT Num32BitValues,
  _In_ UINT ShaderRegister,
  _In_opt_ UINT RegisterSpace,
  _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility
) {
  D3D12_ROOT_PARAMETER1 rootParameter = {};
  rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  rootParameter.Constants.ShaderRegister = ShaderRegister;
  rootParameter.Constants.RegisterSpace = RegisterSpace;
  rootParameter.Constants.Num32BitValues = Num32BitValues;
  rootParameter.ShaderVisibility = ShaderVisibility;

  m_aRootParameters.push_back(rootParameter);
}

VOID RootSignatureGenerator::AddConstBufferView(
  _In_ UINT ShaderRegister,
  _In_opt_ UINT RegisterSpace,
  _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility,
  _In_opt_ D3D12_ROOT_DESCRIPTOR_FLAGS flags
) {
  D3D12_ROOT_PARAMETER1 rootParameter = {};
  rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameter.Descriptor.ShaderRegister = ShaderRegister;
  rootParameter.Descriptor.RegisterSpace = RegisterSpace;
  rootParameter.ShaderVisibility = ShaderVisibility;
  rootParameter.Descriptor.Flags = flags;

  m_aRootParameters.push_back(rootParameter);
}

VOID RootSignatureGenerator::AddShaderResourceView(
  _In_ UINT ShaderRegister,
  _In_opt_ UINT RegisterSpace,
  _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility,
  _In_opt_ D3D12_ROOT_DESCRIPTOR_FLAGS flags
) {
  D3D12_ROOT_PARAMETER1 rootParameter = {};
  rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  rootParameter.Descriptor.ShaderRegister = ShaderRegister;
  rootParameter.Descriptor.RegisterSpace = RegisterSpace;
  rootParameter.ShaderVisibility = ShaderVisibility;
  rootParameter.Descriptor.Flags = flags;

  m_aRootParameters.push_back(rootParameter);
}

VOID RootSignatureGenerator::AddUnorderedAccessView(
  _In_ UINT ShaderRegister,
  _In_opt_ UINT RegisterSpace,
  _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility,
  _In_opt_ D3D12_ROOT_DESCRIPTOR_FLAGS flags
) {
  D3D12_ROOT_PARAMETER1 rootParameter = {};
  rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  rootParameter.Descriptor.ShaderRegister = ShaderRegister;
  rootParameter.Descriptor.RegisterSpace = RegisterSpace;
  rootParameter.ShaderVisibility = ShaderVisibility;
  rootParameter.Descriptor.Flags = flags;

  m_aRootParameters.push_back(rootParameter);
}

VOID RootSignatureGenerator::AddDescriptorTable(
  _In_ std::initializer_list<D3D12_DESCRIPTOR_RANGE1> aDescriptorRange,
  _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility
) {
  if (aDescriptorRange.size() != 0) {
    D3D12_ROOT_PARAMETER1 rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = (UINT)aDescriptorRange.size();
    rootParameter.DescriptorTable.pDescriptorRanges = aDescriptorRange.begin();
    rootParameter.ShaderVisibility = ShaderVisibility;

    m_aRootParameters.push_back(rootParameter);
    m_aDescriptorRangeStorage.insert(m_aDescriptorRangeStorage.end(), aDescriptorRange);
  }
}

VOID RootSignatureGenerator::AddDescriptorTable(
  _In_ UINT uNumDescriptorRange,
  _In_ const D3D12_DESCRIPTOR_RANGE1 *pDescriptorRange,
  _In_opt_ D3D12_SHADER_VISIBILITY ShaderVisibility
) {
  if (uNumDescriptorRange != 0) {
    D3D12_ROOT_PARAMETER1 rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = uNumDescriptorRange;

    rootParameter.DescriptorTable.pDescriptorRanges = pDescriptorRange;
    rootParameter.ShaderVisibility = ShaderVisibility;

    m_aRootParameters.push_back(rootParameter);
    m_aDescriptorRangeStorage.insert(m_aDescriptorRangeStorage.end(),
      pDescriptorRange, pDescriptorRange + uNumDescriptorRange);
  }
}

VOID RootSignatureGenerator::AddStaticSamples(
  _In_ UINT uNumStaticSamples,
  _In_ const D3D12_STATIC_SAMPLER_DESC *pStaticSamples
) {
  m_aStaticSamples.insert(m_aStaticSamples.end(), pStaticSamples, pStaticSamples + uNumStaticSamples);
}

VOID RootSignatureGenerator::AddStaticSamples(
  _In_ std::initializer_list<D3D12_STATIC_SAMPLER_DESC> aStaticSamples
) {
  m_aStaticSamples.insert(m_aStaticSamples.end(), aStaticSamples);
}

D3D12_DESCRIPTOR_RANGE1 RootSignatureGenerator::ComposeDesciptorRange(
  _In_ D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
  _In_ UINT numDescriptors,
  _In_ UINT baseShaderRegister,
  _In_opt_ UINT registerSpace,
  _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags,
  _In_opt_ UINT offsetInDescriptorsFromTableStart
) {
  D3D12_DESCRIPTOR_RANGE1 tableRange = {};
  tableRange.RangeType = rangeType;
  tableRange.NumDescriptors = numDescriptors;
  tableRange.BaseShaderRegister = baseShaderRegister;
  tableRange.RegisterSpace = registerSpace;
  tableRange.Flags = flags;
  tableRange.OffsetInDescriptorsFromTableStart = offsetInDescriptorsFromTableStart;

  return tableRange;
}

D3D12_DESCRIPTOR_RANGE1 RootSignatureGenerator::ComposeConstBufferViewRange(
  _In_ UINT numDescriptors,
  _In_ UINT baseShaderRegister,
  _In_opt_ UINT registerSpace,
  _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags,
  _In_opt_ UINT offsetInDescriptorsFromTableStart
) {
  return ComposeDesciptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
    numDescriptors, baseShaderRegister, registerSpace, flags, offsetInDescriptorsFromTableStart);
}

D3D12_DESCRIPTOR_RANGE1 RootSignatureGenerator::ComposeShaderResourceViewRange(
  _In_ UINT numDescriptors,
  _In_ UINT baseShaderRegister,
  _In_opt_ UINT registerSpace,
  _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags,
  _In_opt_ UINT offsetInDescriptorsFromTableStart
) {
  return ComposeDesciptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
    numDescriptors, baseShaderRegister, registerSpace, flags, offsetInDescriptorsFromTableStart);
}

D3D12_DESCRIPTOR_RANGE1 RootSignatureGenerator::ComposeUnorderedAccessViewRange(
  _In_ UINT numDescriptors,
  _In_ UINT baseShaderRegister,
  _In_opt_ UINT registerSpace,
  _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags,
  _In_opt_ UINT offsetInDescriptorsFromTableStart
) {
  return ComposeDesciptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
    numDescriptors, baseShaderRegister, registerSpace, flags, offsetInDescriptorsFromTableStart);
}

D3D12_DESCRIPTOR_RANGE1 RootSignatureGenerator::ComposeSampler(
  _In_ UINT numDescriptors,
  _In_ UINT baseShaderRegister,
  _In_opt_ UINT registerSpace,
  _In_opt_ D3D12_DESCRIPTOR_RANGE_FLAGS flags,
  _In_opt_ UINT offsetInDescriptorsFromTableStart
) {
  return ComposeDesciptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
    numDescriptors, baseShaderRegister, registerSpace, flags, offsetInDescriptorsFromTableStart);
}

D3D12_STATIC_SAMPLER_DESC RootSignatureGenerator::ComposeStaticSampler(
  UINT shaderRegister,
  _In_opt_ D3D12_FILTER filter,
  _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressU,
  _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressV,
  _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressW,
  _In_opt_ FLOAT mipLODBias ,
  _In_opt_ UINT maxAnisotropy,
  _In_opt_ D3D12_COMPARISON_FUNC comparisonFunc,
  _In_opt_ D3D12_STATIC_BORDER_COLOR borderColor,
  _In_opt_ FLOAT minLOD,
  _In_opt_ FLOAT maxLOD,
  _In_opt_ D3D12_SHADER_VISIBILITY shaderVisibility,
  _In_opt_ UINT registerSpace
) {
  D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
  samplerDesc.ShaderRegister = shaderRegister;
  samplerDesc.Filter = filter;
  samplerDesc.AddressU = addressU;
  samplerDesc.AddressV = addressV;
  samplerDesc.AddressW = addressW;
  samplerDesc.MipLODBias = mipLODBias;
  samplerDesc.MaxAnisotropy = maxAnisotropy;
  samplerDesc.ComparisonFunc = comparisonFunc;
  samplerDesc.BorderColor = borderColor;
  samplerDesc.MinLOD = minLOD;
  samplerDesc.MaxLOD = maxLOD;
  samplerDesc.ShaderVisibility = shaderVisibility;
  samplerDesc.RegisterSpace = registerSpace;

  return samplerDesc;
}


D3D12_STATIC_SAMPLER_DESC RootSignatureGenerator::ComposeStaticSampler2(
  UINT shaderRegister,
  _In_opt_ UINT registerSpace,
  _In_opt_ D3D12_SHADER_VISIBILITY shaderVisibility,
  _In_opt_ D3D12_FILTER filter,
  _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressU,
  _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressV,
  _In_opt_ D3D12_TEXTURE_ADDRESS_MODE addressW,
  _In_opt_ FLOAT mipLODBias,
  _In_opt_ UINT maxAnisotropy,
  _In_opt_ D3D12_COMPARISON_FUNC comparisonFunc,
  _In_opt_ D3D12_STATIC_BORDER_COLOR borderColor,
  _In_opt_ FLOAT minLOD,
  _In_opt_ FLOAT maxLOD
) {
  D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
  samplerDesc.ShaderRegister = shaderRegister;
  samplerDesc.Filter = filter;
  samplerDesc.AddressU = addressU;
  samplerDesc.AddressV = addressV;
  samplerDesc.AddressW = addressW;
  samplerDesc.MipLODBias = mipLODBias;
  samplerDesc.MaxAnisotropy = maxAnisotropy;
  samplerDesc.ComparisonFunc = comparisonFunc;
  samplerDesc.BorderColor = borderColor;
  samplerDesc.MinLOD = minLOD;
  samplerDesc.MaxLOD = maxLOD;
  samplerDesc.ShaderVisibility = shaderVisibility;
  samplerDesc.RegisterSpace = registerSpace;

  return samplerDesc;
}

HRESULT RootSignatureGenerator::Generate(
  ID3D12Device *pd3dDevice,
  D3D12_ROOT_SIGNATURE_FLAGS Flags,
  ID3D12RootSignature **ppRootSignature
) {
  HRESULT hr;
  UINT i;
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc;
  Microsoft::WRL::ComPtr<ID3DBlob> pSignatureBlob, pErrorBlob;

  i = 0;
  for (auto &rootParameter : m_aRootParameters) {
    if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE &&
      rootParameter.DescriptorTable.NumDescriptorRanges != 0) {
      rootParameter.DescriptorTable.pDescriptorRanges = &m_aDescriptorRangeStorage[i];
      i += rootParameter.DescriptorTable.NumDescriptorRanges;
    }
  }

  signatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  signatureDesc.Desc_1_1.Flags = Flags;
  signatureDesc.Desc_1_1.NumParameters = (UINT)m_aRootParameters.size();
  signatureDesc.Desc_1_1.pParameters = m_aRootParameters.data();
  signatureDesc.Desc_1_1.NumStaticSamplers = (UINT)m_aStaticSamples.size();
  signatureDesc.Desc_1_1.pStaticSamplers = m_aStaticSamples.data();

  V_RETURN(D3D12SerializeVersionedRootSignature(&signatureDesc,
    pSignatureBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    std::string  errMsg((const char *)pErrorBlob->GetBufferPointer(), pErrorBlob->GetBufferSize() + 1);
    errMsg.back() = 0;
    DXOutputDebugStringA(errMsg.c_str());
    V_RETURN2("compile shader/library error!", E_FAIL);
  }

  V_RETURN(pd3dDevice->CreateRootSignature(
    0,
    pSignatureBlob->GetBufferPointer(),
    pSignatureBlob->GetBufferSize(),
    IID_PPV_ARGS(ppRootSignature)
  ));

  return hr;
}

HRESULT RootSignatureGenerator::Generate(
  _In_ ID3D12Device *pd3dDevice,
  _In_ LPCSTR pszSource,
  _In_ DWORD dwSourceSizeInBytes,
  _In_ LPCSTR pszDefineEntry, /// The directive "define" the macro entry point.
  _COM_Outptr_ ID3D12RootSignature **ppRootSignature
) {
  HRESULT hr;
  Microsoft::WRL::ComPtr<ID3DBlob> pResult, pErrorBlob;

  hr = D3DCompile2(
    pszSource,
    dwSourceSizeInBytes,
    nullptr,
    nullptr,
    nullptr,
    pszDefineEntry,
    "rootsig_1_1",
    0,
    0,
    0,
    nullptr,
    0,
    pResult.GetAddressOf(),
    pErrorBlob.GetAddressOf()
  );
  if (FAILED(hr)) {
    if (pErrorBlob) {
      std::string errMsg((const char *)pErrorBlob->GetBufferPointer(), pErrorBlob->GetBufferSize() + 1);
      errMsg.back() = 0;
      DXOutputDebugStringA(errMsg.c_str());
    }
    V_RETURN2("Compile root signature error!", hr);
  }

  V_RETURN(pd3dDevice->CreateRootSignature(
    0, pResult->GetBufferPointer(), pResult->GetBufferSize(),
    IID_PPV_ARGS(ppRootSignature)));

  return hr;
}