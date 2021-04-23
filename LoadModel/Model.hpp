#pragma once
#include <D3D12MemAllocator.hpp>
#include <functional>
#include <DirectXMath.h>
#include <DirectXCollision.h>

//----------------------------------------------------------------------------------
// Model loading options
enum ModelLoaderFlags : uint32_t
{
  ModelLoader_Default            = 0x0,
  ModelLoader_MaterialColorsSRGB = 0x1,
  ModelLoader_AllowLargeModels   = 0x2,
};

//----------------------------------------------------------------------------------
// Each mesh part is a submesh with a single effect
class ModelMeshPart {
public:
  ModelMeshPart(uint32_t partIndex) noexcept;

  virtual ~ModelMeshPart();

  uint32_t partIndex;     // Unique index assigned per-part in a model; used to
                          // index effects.
  uint32_t materialIndex; // Index of the material spec to use
  uint32_t indexCount;
  uint32_t startIndex;
  int32_t vertexOffset;
  uint32_t vertexStride;
  uint32_t vertexCount;
  uint32_t indexBufferSize;
  uint32_t vertexBufferSize;
  D3D_PRIMITIVE_TOPOLOGY primitiveType;
  DXGI_FORMAT indexFormat;
  D3D12MAResourceSPtr indexBuffer;
  D3D12MAResourceSPtr vertexBuffer;

  D3D12MAResourceSPtr staticIndexBuffer;
  D3D12MAResourceSPtr staticVertexBuffer;

  std::shared_ptr<std::vector<D3D12_INPUT_ELEMENT_DESC>> vbDecl;

  using Collection = std::vector<std::unique_ptr<ModelMeshPart>>;

  using DrawCallback =
    std::function<void(_In_ ID3D12GraphicsCommandList *commandList, _In_ const ModelMeshPart &part, void *pUserData)>;

  // Draw mesh part
  void __cdecl Draw(_In_ ID3D12GraphicsCommandList *commandList) const;

  void __cdecl DrawInstanced(_In_ ID3D12GraphicsCommandList *commandList, uint32_t instanceCount,
                             uint32_t startInstanceLocation = 0) const;
};

//----------------------------------------------------------------------------------
// A mesh consists of one or more model mesh parts
class ModelMesh {
public:
  ModelMesh() noexcept;

  virtual ~ModelMesh();

  DirectX::BoundingSphere boundingSphere;
  DirectX::BoundingBox boundingBox;
  ModelMeshPart::Collection opaqueMeshParts;
  ModelMeshPart::Collection alphaMeshParts;
  std::wstring name;

  using Collection = std::vector<std::shared_ptr<ModelMesh>>;

  // Draw the mesh
  void __cdecl DrawOpaque(_In_ ID3D12GraphicsCommandList *commandList) const;
  void __cdecl DrawAlpha(_In_ ID3D12GraphicsCommandList *commandList) const;
};

//----------------------------------------------------------------------------------
// A model consists of one or more meshes
class Model {
public:
  Model() noexcept;

  virtual ~Model();

  struct ModelMaterialInfo {
    std::wstring name;
    bool perVertexColor;
    bool enableSkinning;
    bool enableDualTexture;
    bool enableNormalMaps;
    bool biasedVertexNormals;
    float specularPower;
    float alphaValue;
    DirectX::XMFLOAT3 ambientColor;
    DirectX::XMFLOAT3 diffuseColor;
    DirectX::XMFLOAT3 specularColor;
    DirectX::XMFLOAT3 emissiveColor;
    int diffuseTextureIndex;
    int specularTextureIndex;
    int normalTextureIndex;
    int emissiveTextureIndex;
    int samplerIndex;
    int samplerIndex2;

    ModelMaterialInfo() noexcept
      : perVertexColor(false)
      , enableSkinning(false)
      , enableDualTexture(false)
      , enableNormalMaps(false)
      , biasedVertexNormals(false)
      , specularPower(0)
      , alphaValue(0)
      , ambientColor(0, 0, 0)
      , diffuseColor(0, 0, 0)
      , specularColor(0, 0, 0)
      , emissiveColor(0, 0, 0)
      , diffuseTextureIndex(-1)
      , specularTextureIndex(-1)
      , normalTextureIndex(-1)
      , emissiveTextureIndex(-1)
      , samplerIndex(-1)
      , samplerIndex2(-1) {}
  };

  using ModelMaterialInfoCollection = std::vector<ModelMaterialInfo>;
  using TextureCollection           = std::vector<std::wstring>;

  ModelMesh::Collection meshes;
  ModelMaterialInfoCollection materials;
  TextureCollection textureNames;
  std::wstring name;

  HRESULT CreateFromSDKMESH(_In_ ID3D12Device *pDevice, _In_ D3D12MAAllocator *pAllocator, _In_ const uint8_t *data,
                            _In_ size_t idataSize, _In_ ModelLoaderFlags flags);

  HRESULT CreateFromSDKMESH(_In_ ID3D12Device *pDevice, _In_ D3D12MAAllocator *pAllocator, _In_ LPCWSTR pszFilePath,
                            _In_ ModelLoaderFlags flags);
};