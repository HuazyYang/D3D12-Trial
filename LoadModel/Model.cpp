#include <d3dUtils.h>
#include "Model.hpp"

ModelMeshPart::ModelMeshPart(uint32_t ipartIndex) noexcept :
    partIndex(ipartIndex),
    materialIndex(0),
    indexCount(0),
    startIndex(0),
    vertexOffset(0),
    vertexStride(0),
    vertexCount(0),
    indexBufferSize(0),
    vertexBufferSize(0),
    primitiveType(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST),
    indexFormat(DXGI_FORMAT_R16_UINT)
{
}


ModelMeshPart::~ModelMeshPart()
{
}

ModelMesh::ModelMesh() noexcept
{
}


ModelMesh::~ModelMesh()
{
}

Model::Model() noexcept {

}

Model::~Model() {

}