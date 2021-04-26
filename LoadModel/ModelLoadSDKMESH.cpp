#include <d3dUtils.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "Model.hpp"
#include "HpFileIo.h"
#include "SDKmesh.h"
#include <Texture.h>
#include <filesystem>
#include <map>

using Microsoft::WRL::ComPtr;

namespace Internal {

using namespace DirectX;

enum STATIC_SAMPLER_INDICES {
  STATIC_SAMPLER_INDEX_ANISOINTROPIC_WRAP,
};

enum : unsigned int
{
  PER_VERTEX_COLOR      = 0x1,
  SKINNING              = 0x2,
  DUAL_TEXTURE          = 0x4,
  NORMAL_MAPS           = 0x8,
  BIASED_VERTEX_NORMALS = 0x10,
  USES_OBSOLETE_DEC3N   = 0x20,
};

template<size_t sizeOfBuffer>
inline void ASCIIToWChar(wchar_t (&buffer)[sizeOfBuffer], const char* ascii) {
  MultiByteToWideChar(CP_UTF8, 0, ascii, -1, buffer, sizeOfBuffer);
}

int GetUniqueTextureIndex(const wchar_t* textureName, std::map<std::wstring, int>& textureDictionary) {
  if (textureName == nullptr || !textureName[0])
    return -1;

  auto i = textureDictionary.find(textureName);
  if (i == std::cend(textureDictionary)) {
    int index                      = static_cast<int>(textureDictionary.size());
    textureDictionary[textureName] = index;
    return index;
  } else {
    return i->second;
  }
}

inline XMFLOAT3 GetMaterialColor(float r, float g, float b, bool srgb) noexcept {
  if (srgb) {
    XMVECTOR v = XMVectorSet(r, g, b, 1.f);
    v          = XMColorSRGBToRGB(v);

    XMFLOAT3 result;
    XMStoreFloat3(&result, v);
    return result;
  } else {
    return XMFLOAT3(r, g, b);
  }
}

void InitMaterial(const DXUT::SDKMESH_MATERIAL& mh, unsigned int flags, _Out_ ModelMaterialInfo& m,
                  _Inout_ std::map<std::wstring, int32_t>& textureDictionary, bool srgb) {
  wchar_t matName[DXUT::MAX_MATERIAL_NAME] = {};
  ASCIIToWChar(matName, mh.Name);

  wchar_t diffuseName[DXUT::MAX_TEXTURE_NAME] = {};
  ASCIIToWChar(diffuseName, mh.DiffuseTexture);

  wchar_t specularName[DXUT::MAX_TEXTURE_NAME] = {};
  ASCIIToWChar(specularName, mh.SpecularTexture);

  wchar_t normalName[DXUT::MAX_TEXTURE_NAME] = {};
  ASCIIToWChar(normalName, mh.NormalTexture);

  if ((flags & DUAL_TEXTURE) && !mh.SpecularTexture[0]) {
    DX_TRACE(L"WARNING: Material '%S' has multiple texture coords but not multiple textures\n", mh.Name);
    flags &= ~static_cast<unsigned int>(DUAL_TEXTURE);
  }

  if (flags & NORMAL_MAPS) {
    if (!mh.NormalTexture[0]) {
      flags &= ~static_cast<unsigned int>(NORMAL_MAPS);
      *normalName = 0;
    }
  } else if (mh.NormalTexture[0]) {
    DX_TRACE(L"WARNING: Material '%S' has a normal map, but vertex buffer is missing tangents\n", mh.Name);
    *normalName = 0;
  }

  m                     = {};
  m.name                = matName;
  m.perVertexColor      = (flags & PER_VERTEX_COLOR) != 0;
  m.enableSkinning      = (flags & SKINNING) != 0;
  m.enableDualTexture   = (flags & DUAL_TEXTURE) != 0;
  m.enableNormalMaps    = (flags & NORMAL_MAPS) != 0;
  m.biasedVertexNormals = (flags & BIASED_VERTEX_NORMALS) != 0;

  if (mh.Ambient.x == 0 && mh.Ambient.y == 0 && mh.Ambient.z == 0 && mh.Ambient.w == 0 && mh.Diffuse.x == 0 &&
      mh.Diffuse.y == 0 && mh.Diffuse.z == 0 && mh.Diffuse.w == 0) {
    // SDKMESH material color block is uninitalized; assume defaults
    m.diffuseColor = DirectX::XMFLOAT3(1.f, 1.f, 1.f);
    m.alphaValue   = 1.f;
  } else {
    m.ambientColor  = GetMaterialColor(mh.Ambient.x, mh.Ambient.y, mh.Ambient.z, srgb);
    m.diffuseColor  = GetMaterialColor(mh.Diffuse.x, mh.Diffuse.y, mh.Diffuse.z, srgb);
    m.emissiveColor = GetMaterialColor(mh.Emissive.x, mh.Emissive.y, mh.Emissive.z, srgb);

    if (mh.Diffuse.w != 1.f && mh.Diffuse.w != 0.f) {
      m.alphaValue = mh.Diffuse.w;
    } else
      m.alphaValue = 1.f;

    if (mh.Power > 0) {
      m.specularPower = mh.Power;
      m.specularColor = DirectX::XMFLOAT3(mh.Specular.x, mh.Specular.y, mh.Specular.z);
    }
  }

  m.diffuseTextureIndex  = GetUniqueTextureIndex(diffuseName, textureDictionary);
  m.specularTextureIndex = GetUniqueTextureIndex(specularName, textureDictionary);
  m.normalTextureIndex   = GetUniqueTextureIndex(normalName, textureDictionary);

  m.samplerIndex  = (m.diffuseTextureIndex == -1) ? -1 : static_cast<int>(STATIC_SAMPLER_INDEX_ANISOINTROPIC_WRAP);
  m.samplerIndex2 = (flags & DUAL_TEXTURE) ? static_cast<int>(STATIC_SAMPLER_INDEX_ANISOINTROPIC_WRAP) : -1;
}

void InitMaterial(const DXUT::SDKMESH_MATERIAL_V2& mh, unsigned int flags, _Out_ ModelMaterialInfo& m,
                  _Inout_ std::map<std::wstring, int>& textureDictionary) {
  wchar_t matName[DXUT::MAX_MATERIAL_NAME] = {};
  ASCIIToWChar(matName, mh.Name);

  wchar_t albetoTexture[DXUT::MAX_TEXTURE_NAME] = {};
  ASCIIToWChar(albetoTexture, mh.AlbetoTexture);

  wchar_t normalName[DXUT::MAX_TEXTURE_NAME] = {};
  ASCIIToWChar(normalName, mh.NormalTexture);

  wchar_t rmaName[DXUT::MAX_TEXTURE_NAME] = {};
  ASCIIToWChar(rmaName, mh.RMATexture);

  wchar_t emissiveName[DXUT::MAX_TEXTURE_NAME] = {};
  ASCIIToWChar(emissiveName, mh.EmissiveTexture);

  m                     = {};
  m.name                = matName;
  m.perVertexColor      = false;
  m.enableSkinning      = false;
  m.enableDualTexture   = false;
  m.enableNormalMaps    = true;
  m.biasedVertexNormals = (flags & BIASED_VERTEX_NORMALS) != 0;
  m.alphaValue          = (mh.Alpha == 0.f) ? 1.f : mh.Alpha;

  m.diffuseTextureIndex  = GetUniqueTextureIndex(albetoTexture, textureDictionary);
  m.specularTextureIndex = GetUniqueTextureIndex(rmaName, textureDictionary);
  m.normalTextureIndex   = GetUniqueTextureIndex(normalName, textureDictionary);
  m.emissiveTextureIndex = GetUniqueTextureIndex(emissiveName, textureDictionary);

  m.samplerIndex = m.samplerIndex2 = static_cast<int>(STATIC_SAMPLER_INDEX_ANISOINTROPIC_WRAP);
}

//--------------------------------------------------------------------------------------
// Direct3D 9 Vertex Declaration to Direct3D 12 Input Layout mapping

static_assert(D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT >= 32, "SDKMESH supports decls up to 32 entries");

HRESULT GetInputLayoutDesc(_In_reads_(32) const DXUT::D3DVERTEXELEMENT9 decl[],
                                std::vector<D3D12_INPUT_ELEMENT_DESC>& inputDesc, uint32_t &flags) {
  static const D3D12_INPUT_ELEMENT_DESC s_elements[] = {
      { "SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  using namespace DXUT;

  HRESULT hr;
  uint32_t offset    = 0;
  uint32_t texcoords = 0;
  bool posfound = false;

  hr = S_OK;
  flags = 0;

  for (uint32_t index = 0; index < DXUT::MAX_VERTEX_ELEMENTS; ++index) {
    if (decl[index].Usage == 0xFF)
      break;

    if (decl[index].Type == D3DDECLTYPE_UNUSED)
      break;

    if (decl[index].Offset != offset)
      break;

    if (decl[index].Usage == D3DDECLUSAGE_POSITION) {
      if (decl[index].Type == D3DDECLTYPE_FLOAT3) {
        inputDesc.push_back(s_elements[0]);
        offset += 12;
        posfound = true;
      } else
        break;
    } else if (decl[index].Usage == D3DDECLUSAGE_NORMAL || decl[index].Usage == D3DDECLUSAGE_TANGENT ||
               decl[index].Usage == D3DDECLUSAGE_BINORMAL) {
      size_t base = 1;
      if (decl[index].Usage == D3DDECLUSAGE_TANGENT)
        base = 3;
      else if (decl[index].Usage == D3DDECLUSAGE_BINORMAL)
        base = 4;

      D3D12_INPUT_ELEMENT_DESC desc = s_elements[base];

      bool unk = false;
      switch (decl[index].Type) {
      case D3DDECLTYPE_FLOAT3:
        assert(desc.Format == DXGI_FORMAT_R32G32B32_FLOAT);
        offset += 12;
        break;
      case D3DDECLTYPE_UBYTE4N:
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        flags |= BIASED_VERTEX_NORMALS;
        offset += 4;
        break;
      case D3DDECLTYPE_SHORT4N:
        desc.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
        offset += 8;
        break;
      case D3DDECLTYPE_FLOAT16_4:
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        offset += 8;
        break;
      case D3DDECLTYPE_DXGI_R10G10B10A2_UNORM:
        desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        flags |= BIASED_VERTEX_NORMALS;
        offset += 4;
        break;
      case D3DDECLTYPE_DXGI_R11G11B10_FLOAT:
        desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        flags |= BIASED_VERTEX_NORMALS;
        offset += 4;
        break;
      case D3DDECLTYPE_DXGI_R8G8B8A8_SNORM:
        desc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
        offset += 4;
        break;

#if (defined(_XBOX_ONE) && defined(_TITLE)) || defined(_GAMING_XBOX)
      case D3DDECLTYPE_DEC3N:
        desc.Format = DXGI_FORMAT_R10G10B10_SNORM_A2_UNORM;
        offset += 4;
        break;
      case (32 + DXGI_FORMAT_R10G10B10_SNORM_A2_UNORM):
        desc.Format = DXGI_FORMAT_R10G10B10_SNORM_A2_UNORM;
        offset += 4;
        break;
#else
      case D3DDECLTYPE_DEC3N:
        desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        flags |= USES_OBSOLETE_DEC3N;
        offset += 4;
        break;
#endif

      default:
        unk = true;
        break;
      }

      if (unk)
        break;

      if (decl[index].Usage == D3DDECLUSAGE_TANGENT) {
        flags |= NORMAL_MAPS;
      }

      inputDesc.push_back(desc);
    } else if (decl[index].Usage == D3DDECLUSAGE_COLOR) {
      D3D12_INPUT_ELEMENT_DESC desc = s_elements[2];

      bool unk = false;
      switch (decl[index].Type) {
      case D3DDECLTYPE_FLOAT4:
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        offset += 16;
        break;
      case D3DDECLTYPE_D3DCOLOR:
        assert(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
        offset += 4;
        break;
      case D3DDECLTYPE_UBYTE4N:
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        offset += 4;
        break;
      case D3DDECLTYPE_FLOAT16_4:
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        offset += 8;
        break;
      case D3DDECLTYPE_DXGI_R10G10B10A2_UNORM:
        desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        offset += 4;
        break;
      case D3DDECLTYPE_DXGI_R11G11B10_FLOAT:
        desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        offset += 4;
        break;

      default:
        unk = true;
        break;
      }

      if (unk)
        break;

      flags |= PER_VERTEX_COLOR;

      inputDesc.push_back(desc);
    } else if (decl[index].Usage == D3DDECLUSAGE_TEXCOORD) {
      D3D12_INPUT_ELEMENT_DESC desc = s_elements[5];
      desc.SemanticIndex            = decl[index].UsageIndex;

      bool unk = false;
      switch (decl[index].Type) {
      case D3DDECLTYPE_FLOAT1:
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        offset += 4;
        break;
      case D3DDECLTYPE_FLOAT2:
        assert(desc.Format == DXGI_FORMAT_R32G32_FLOAT);
        offset += 8;
        break;
      case D3DDECLTYPE_FLOAT3:
        desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        offset += 12;
        break;
      case D3DDECLTYPE_FLOAT4:
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        offset += 16;
        break;
      case D3DDECLTYPE_FLOAT16_2:
        desc.Format = DXGI_FORMAT_R16G16_FLOAT;
        offset += 4;
        break;
      case D3DDECLTYPE_FLOAT16_4:
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        offset += 8;
        break;

      default:
        unk = true;
        break;
      }

      if (unk)
        break;

      ++texcoords;

      inputDesc.push_back(desc);
    } else if (decl[index].Usage == D3DDECLUSAGE_BLENDINDICES) {
      if (decl[index].Type == D3DDECLTYPE_UBYTE4) {
        flags |= SKINNING;
        inputDesc.push_back(s_elements[6]);
        offset += 4;
      } else
        break;
    } else if (decl[index].Usage == D3DDECLUSAGE_BLENDWEIGHT) {
      if (decl[index].Type == D3DDECLTYPE_UBYTE4N) {
        flags |= SKINNING;
        inputDesc.push_back(s_elements[7]);
        offset += 4;
      } else
        break;
    } else
      break;
  }

  if (!posfound)
    V_RETURN2("SV_Position is required", E_INVALIDARG);

  if (texcoords == 2) {
    flags |= DUAL_TEXTURE;
  }

  return hr;
}

};

_Use_decl_annotations_
HRESULT Model::CreateFromSDKMESH(_In_ ResourceUploadBatch *pUploadBatch,
                                 _In_ LPCWSTR pszFilePath, _In_ ModelLoaderFlags flags) {

  HRESULT hr;
  ComPtr<HpFileIo::IFileDataBlob> pFileDataBlob;

  V_RETURN(HpFileIo::ReadFileDirectly(pszFilePath, 0, 0, &pFileDataBlob));

  filePath = pszFilePath;

  return CreateFromSDKMESH(pUploadBatch, (const uint8_t*)pFileDataBlob->GetBufferPointer(),
                           pFileDataBlob->GetBufferSize(), flags);
}

_Use_decl_annotations_
HRESULT Model::CreateFromSDKMESH(_In_ ResourceUploadBatch *pUploadBatch,
                                 _In_ const uint8_t* pData, _In_ size_t iDataSize, _In_ ModelLoaderFlags flags) {

  HRESULT hr;

  D3D12MA_ALLOCATION_DESC defaultAllocDesc = {};

  if (pData == nullptr)
    V_RETURN2("Invalid argument!", E_INVALIDARG);

  uint64_t dataSize = iDataSize;
  auto meshData = pData;

  // File Headers
  if (dataSize < sizeof(DXUT::SDKMESH_HEADER))
    V_RETURN2("Bad format: End of file reached!", E_INVALIDARG);

  auto header = reinterpret_cast<const DXUT::SDKMESH_HEADER*>(pData);

  size_t headerSize = sizeof(DXUT::SDKMESH_HEADER) +
                      header->NumVertexBuffers * sizeof(DXUT::SDKMESH_VERTEX_BUFFER_HEADER) +
                      header->NumIndexBuffers * sizeof(DXUT::SDKMESH_INDEX_BUFFER_HEADER);
  if (header->HeaderSize != headerSize)
    V_RETURN2("Not a valid SDKMESH file", E_INVALIDARG);

  if (dataSize < header->HeaderSize)
    V_RETURN2("End of file", E_INVALIDARG);

  if (header->Version != DXUT::SDKMESH_FILE_VERSION && header->Version != DXUT::SDKMESH_FILE_VERSION_V2)
    V_RETURN2("Not a supported SDKMESH version", E_INVALIDARG);

  if (header->IsBigEndian)
    V_RETURN2("Loading BigEndian SDKMESH files not supported", E_INVALIDARG);

  if (!header->NumMeshes)
    V_RETURN2("No meshes found", E_INVALIDARG);

  if (!header->NumVertexBuffers)
    V_RETURN2("No vertex buffers found", E_INVALIDARG);

  if (!header->NumIndexBuffers)
    V_RETURN2("No index buffers found", E_INVALIDARG);

  if (!header->NumTotalSubsets)
    V_RETURN2("No subsets found", E_INVALIDARG);

  if (!header->NumMaterials)
    V_RETURN2("No materials found", E_INVALIDARG);

  // Sub-headers
  if (dataSize < header->VertexStreamHeadersOffset ||
      (dataSize < (header->VertexStreamHeadersOffset +
                   uint64_t(header->NumVertexBuffers) * sizeof(DXUT::SDKMESH_VERTEX_BUFFER_HEADER))))
    V_RETURN2("End of file", E_INVALIDARG);
  auto vbArray =
      reinterpret_cast<const DXUT::SDKMESH_VERTEX_BUFFER_HEADER*>(meshData + header->VertexStreamHeadersOffset);

  if (dataSize < header->IndexStreamHeadersOffset ||
      (dataSize < (header->IndexStreamHeadersOffset +
                   uint64_t(header->NumIndexBuffers) * sizeof(DXUT::SDKMESH_INDEX_BUFFER_HEADER))))
    V_RETURN2("End of file", E_INVALIDARG);
  auto ibArray =
      reinterpret_cast<const DXUT::SDKMESH_INDEX_BUFFER_HEADER*>(meshData + header->IndexStreamHeadersOffset);

  if (dataSize < header->MeshDataOffset ||
      (dataSize < (header->MeshDataOffset + uint64_t(header->NumMeshes) * sizeof(DXUT::SDKMESH_MESH))))
    V_RETURN2("End of file", E_INVALIDARG);
  auto meshArray = reinterpret_cast<const DXUT::SDKMESH_MESH*>(meshData + header->MeshDataOffset);

  if (dataSize < header->SubsetDataOffset ||
      (dataSize < (header->SubsetDataOffset + uint64_t(header->NumTotalSubsets) * sizeof(DXUT::SDKMESH_SUBSET))))
     V_RETURN2("End of file", E_INVALIDARG);
  auto subsetArray = reinterpret_cast<const DXUT::SDKMESH_SUBSET*>(meshData + header->SubsetDataOffset);

  if (dataSize < header->FrameDataOffset ||
      (dataSize < (header->FrameDataOffset + uint64_t(header->NumFrames) * sizeof(DXUT::SDKMESH_FRAME))))
    V_RETURN2("End of file", E_INVALIDARG);
  // TODO - auto frameArray = reinterpret_cast<const DXUT::SDKMESH_FRAME*>( meshData + header->FrameDataOffset );

  if (dataSize < header->MaterialDataOffset ||
      (dataSize < (header->MaterialDataOffset + uint64_t(header->NumMaterials) * sizeof(DXUT::SDKMESH_MATERIAL))))
    V_RETURN2("End of file", E_INVALIDARG);

  const DXUT::SDKMESH_MATERIAL* materialArray       = nullptr;
  const DXUT::SDKMESH_MATERIAL_V2* materialArray_v2 = nullptr;
  if (header->Version == DXUT::SDKMESH_FILE_VERSION_V2) {
    materialArray_v2 = reinterpret_cast<const DXUT::SDKMESH_MATERIAL_V2*>(meshData + header->MaterialDataOffset);
  } else {
    materialArray = reinterpret_cast<const DXUT::SDKMESH_MATERIAL*>(meshData + header->MaterialDataOffset);
  }

  // Buffer data
  uint64_t bufferDataOffset = header->HeaderSize + header->NonBufferDataSize;
  if ((dataSize < bufferDataOffset) || (dataSize < bufferDataOffset + header->BufferDataSize))
    V_RETURN2("End of file", E_INVALIDARG);
  const uint8_t* bufferData = meshData + bufferDataOffset;

  // Create vertex buffers
  std::vector<std::shared_ptr<std::vector<D3D12_INPUT_ELEMENT_DESC>>> vbDecls;
  vbDecls.resize(header->NumVertexBuffers);

  std::vector<unsigned int> materialFlags;
  materialFlags.resize(header->NumVertexBuffers);

  bool dec3nwarning = false;
  for (UINT j = 0; j < header->NumVertexBuffers; ++j) {
    auto& vh = vbArray[j];

    if (vh.SizeBytes > UINT32_MAX)
      V_RETURN2("VB too large", E_INVALIDARG);

    if (!(flags & ModelLoader_AllowLargeModels)) {
      if (vh.SizeBytes > (D3D12_REQ_RESOURCE_SIZE_IN_MEGABYTES_EXPRESSION_A_TERM * 1024u * 1024u))
        V_RETURN2("VB too large for DirectX 12", E_INVALIDARG);
    }

    if (dataSize < vh.DataOffset || (dataSize < vh.DataOffset + vh.SizeBytes))
      V_RETURN2("End of file", E_INVALIDARG);

    auto vbDecl           = std::make_shared<std::vector<D3D12_INPUT_ELEMENT_DESC>>();
    unsigned int ilflags;
    
    V_RETURN(Internal::GetInputLayoutDesc(vh.Decl, *vbDecl.get(), ilflags));
    if(auto vbDeclPos = std::find_if(vbDecls.begin(), vbDecls.end(), [vbDecl](const auto &val) {
      return val && val->size() == vbDecl->size() && memcmp(val->data(), vbDecl->data(), val->size() * sizeof(D3D12_INPUT_ELEMENT_DESC)) == 0;
    }); vbDeclPos == vbDecls.end()) {
      vbDecls[j] = (std::move(vbDecl));
    } else {
      vbDecls[j] = *vbDeclPos;
    }

    if (ilflags & Internal::SKINNING) {
      ilflags &= ~static_cast<unsigned int>(Internal::DUAL_TEXTURE | Internal::NORMAL_MAPS);
    }
    if (ilflags & Internal::DUAL_TEXTURE) {
      ilflags &= ~static_cast<unsigned int>(Internal::NORMAL_MAPS);
    }

    if (ilflags & Internal::USES_OBSOLETE_DEC3N) {
      dec3nwarning = true;
    }

    materialFlags[j] = ilflags;
  }

  if (dec3nwarning) {
    DX_TRACE(L"WARNING: Vertex declaration uses legacy Direct3D 9 D3DDECLTYPE_DEC3N which has no DXGI equivalent\n"
             L"         (treating as DXGI_FORMAT_R10G10B10A2_UNORM which is not a signed format)\n");
  }

  // Validate index buffers
  for (UINT j = 0; j < header->NumIndexBuffers; ++j) {
    auto& ih = ibArray[j];

    if (ih.SizeBytes > UINT32_MAX)
      V_RETURN2("IB too large", E_INVALIDARG);

    if (!(flags & ModelLoader_AllowLargeModels)) {
      if (ih.SizeBytes > (D3D12_REQ_RESOURCE_SIZE_IN_MEGABYTES_EXPRESSION_A_TERM * 1024u * 1024u))
        V_RETURN2("IB too large for DirectX 12", E_INVALIDARG);
    }

    if (dataSize < ih.DataOffset || (dataSize < ih.DataOffset + ih.SizeBytes))
      V_RETURN2("End of file", E_INVALIDARG);

    if (ih.IndexType != DXUT::IT_16BIT && ih.IndexType != DXUT::IT_32BIT)
      V_RETURN2("Invalid index buffer type found", E_INVALIDARG);
  }

  // Create meshes
  std::vector<ModelMaterialInfo> materials;
  materials.resize(header->NumMaterials);

  std::map<std::wstring, int> textureDictionary;

  auto model = std::make_unique<Model>();
  model->meshes.reserve(header->NumMeshes);

  uint32_t partCount = 0;

  for (UINT meshIndex = 0; meshIndex < header->NumMeshes; ++meshIndex) {
    auto& mh = meshArray[meshIndex];

    if (!mh.NumSubsets || !mh.NumVertexBuffers || mh.IndexBuffer >= header->NumIndexBuffers ||
        mh.VertexBuffers[0] >= header->NumVertexBuffers)
      V_RETURN2("Invalid mesh found", E_INVALIDARG);

    // mh.NumVertexBuffers is sometimes not what you'd expect, so we skip validating it

    if (dataSize < mh.SubsetOffset || (dataSize < mh.SubsetOffset + uint64_t(mh.NumSubsets) * sizeof(UINT)))
      V_RETURN2("End of file", E_INVALIDARG);

    auto subsets = reinterpret_cast<const UINT*>(meshData + mh.SubsetOffset);

    if (mh.NumFrameInfluences > 0) {
      if (dataSize < mh.FrameInfluenceOffset ||
          (dataSize < mh.FrameInfluenceOffset + uint64_t(mh.NumFrameInfluences) * sizeof(UINT)))
        V_RETURN2("End of file", E_INVALIDARG);

      // TODO - auto influences = reinterpret_cast<const UINT*>( meshData + mh.FrameInfluenceOffset );
    }

    auto mesh                             = std::make_shared<ModelMesh>();
    wchar_t meshName[DXUT::MAX_MESH_NAME] = {};
    Internal::ASCIIToWChar(meshName, mh.Name);

    mesh->name = meshName;

    // Extents
    mesh->boundingBox.Center  = mh.BoundingBoxCenter;
    mesh->boundingBox.Extents = mh.BoundingBoxExtents;
    DirectX::BoundingSphere::CreateFromBoundingBox(mesh->boundingSphere, mesh->boundingBox);

    // Create subsets
    for (UINT j = 0; j < mh.NumSubsets; ++j) {
      auto sIndex = subsets[j];
      if (sIndex >= header->NumTotalSubsets)
        V_RETURN2("Invalid mesh found", E_INVALIDARG);

      auto& subset = subsetArray[sIndex];

      D3D_PRIMITIVE_TOPOLOGY primType;
      switch (subset.PrimitiveType) {
      case DXUT::PT_TRIANGLE_LIST:
        primType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        break;
      case DXUT::PT_TRIANGLE_STRIP:
        primType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        break;
      case DXUT::PT_LINE_LIST:
        primType = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        break;
      case DXUT::PT_LINE_STRIP:
        primType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        break;
      case DXUT::PT_POINT_LIST:
        primType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        break;
      case DXUT::PT_TRIANGLE_LIST_ADJ:
        primType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
        break;
      case DXUT::PT_TRIANGLE_STRIP_ADJ:
        primType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
        break;
      case DXUT::PT_LINE_LIST_ADJ:
        primType = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
        break;
      case DXUT::PT_LINE_STRIP_ADJ:
        primType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
        break;

      case DXUT::PT_QUAD_PATCH_LIST:
      case DXUT::PT_TRIANGLE_PATCH_LIST:
        V_RETURN2("Direct3D9 era tessellation not supported", E_INVALIDARG);

      default:
        V_RETURN2("Unknown primitive type", E_INVALIDARG);
      }

      if (subset.MaterialID >= header->NumMaterials)
        V_RETURN2("Invalid mesh found", E_INVALIDARG);

      auto& mat = materials[subset.MaterialID];

      const size_t vi = mh.VertexBuffers[0];
      if (materialArray_v2) {
        Internal::InitMaterial(materialArray_v2[subset.MaterialID], materialFlags[vi], mat, textureDictionary);
      } else {
        Internal::InitMaterial(materialArray[subset.MaterialID], materialFlags[vi], mat, textureDictionary,
                     (flags & ModelLoader_MaterialColorsSRGB) != 0);
      }

      auto part = new ModelMeshPart(partCount++);

      const auto& vh = vbArray[mh.VertexBuffers[0]];
      const auto& ih = ibArray[mh.IndexBuffer];

      part->indexCount    = static_cast<uint32_t>(subset.IndexCount);
      part->startIndex    = static_cast<uint32_t>(subset.IndexStart);
      part->vertexOffset  = static_cast<int32_t>(subset.VertexStart);
      part->vertexStride  = static_cast<uint32_t>(vh.StrideBytes);
      part->vertexCount   = static_cast<uint32_t>(subset.VertexCount);
      part->primitiveType = primType;
      part->indexFormat =
          (ibArray[mh.IndexBuffer].IndexType == DXUT::IT_32BIT) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

      D3D12_SUBRESOURCE_DATA subres;

      defaultAllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
      defaultAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

      // Vertex data
      auto verts             = bufferData + (vh.DataOffset - bufferDataOffset);
      auto vbytes            = static_cast<size_t>(vh.SizeBytes);
      part->vertexBufferSize = static_cast<uint32_t>(vh.SizeBytes);

      (*pUploadBatch->GetAllocator())->CreateResource(
        &defaultAllocDesc, &CD3DX12_RESOURCE_DESC::Buffer(vbytes), D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, D3D12MA_IID_PPV_ARGS(&part->staticVertexBuffer)
      );

      subres.pData = verts;
      subres.RowPitch = vbytes;
      subres.SlicePitch = vbytes;

      pUploadBatch->Enqueue(
        part->staticVertexBuffer.Get(),
        0, 1, &subres
      );

      // Index data
      auto indices          = bufferData + (ih.DataOffset - bufferDataOffset);
      auto ibytes           = static_cast<size_t>(ih.SizeBytes);
      part->indexBufferSize = static_cast<uint32_t>(ih.SizeBytes);

      (*pUploadBatch->GetAllocator())->CreateResource(
        &defaultAllocDesc, &CD3DX12_RESOURCE_DESC::Buffer(ibytes), D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, D3D12MA_IID_PPV_ARGS(&part->staticIndexBuffer)
      );

      subres.pData = indices;
      subres.RowPitch = ibytes;
      subres.SlicePitch = ibytes;
      pUploadBatch->Enqueue(
        part->staticIndexBuffer.Get(),
        0, 1, &subres
      );

      CD3DX12_RESOURCE_BARRIER resBarriers[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(part->staticVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(part->staticIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON)
      };
      pUploadBatch->ResourceBarrier(2, resBarriers);

      part->materialIndex = subset.MaterialID;
      part->vbDecl        = vbDecls[mh.VertexBuffers[0]];

      if (mat.alphaValue < 1.0f)
        mesh->alphaMeshParts.emplace_back(part);
      else
        mesh->opaqueMeshParts.emplace_back(part);
    }

    model->meshes.emplace_back(mesh);
  }

  model->inputLayouts = std::move(vbDecls);
  model->inputLayouts.resize(std::unique(model->inputLayouts.begin(), model->inputLayouts.end()) - model->inputLayouts.begin());

  DirectX::TexMetadata texMetaData;
  DirectX::ScratchImage scratchImage;
  std::vector<D3D12_SUBRESOURCE_DATA> subres;
  std::filesystem::path parentPath(filePath), texPath;

  parentPath = parentPath.parent_path();

  // Copy the materials and texture names into contiguous arrays
  model->materials = std::move(materials);
  model->texturesCache.resize(textureDictionary.size());
  for (auto texture = std::cbegin(textureDictionary); texture != std::cend(textureDictionary); ++texture) {

    auto &textureCache = model->texturesCache[static_cast<size_t>(texture->second)];
    textureCache.name = texture->first;

    texPath = parentPath;
    texPath.append(textureCache.name);

    if(SUCCEEDED(DirectX::LoadFromDDSFile(texPath.c_str(), DirectX::DDS_FLAGS_NONE, &texMetaData, scratchImage))) {
      V_RETURN(DirectX::PrepareUpload(pUploadBatch->GetDevice(), scratchImage.GetImages(), scratchImage.GetImageCount(),
                                      texMetaData, subres));
      V_RETURN(DirectX::CreateTexture(pUploadBatch->GetDevice(), pUploadBatch->GetAllocator(), texMetaData,
                                      &textureCache.defaultBuffer));

      pUploadBatch->Enqueue(textureCache.defaultBuffer.Get(), 0, (UINT)subres.size(), subres.data());
      pUploadBatch->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(textureCache.defaultBuffer.Get(),
                                                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                                                             D3D12_RESOURCE_STATE_COMMON));
      textureCache.descriptorIndex = texture->second;
    } else
      textureCache.descriptorIndex = -1;
  }

  std::swap(*this, *model);

  return hr;
}