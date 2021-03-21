#include "AccelerationStructureGenerator.h"
#include "MeshBuffer.h"

// Helper to compute buffer alignment size
#ifndef ROUND_UP
#define ROUND_UP(v, powerof2Alignment) \
  (((v) + (powerof2Alignment-1)) & ~(powerof2Alignment - 1) )
#endif /*ROUND_UP*/


///
/// class BottomLevelASGenerator implementation
///

//--------------------------------------------------------------------------------------------------
// Add a vertex buffer in GPU memory into the acceleration structure. The
// vertices are supposed to be represented by 3 float32 value
void BottomLevelASGenerator::AddVertexBuffer(
  ID3D12Resource *vertexBuffer, // Buffer containing the vertex coordinates,
                                // possibly interleaved with other vertex data
  UINT64
  vertexOffsetInBytes, // Offset of the first vertex in the vertex buffer
  UINT vertexCount,    // Number of vertices to consider in the buffer
  UINT vertexSizeInBytes,  // Size of a vertex including all its other data,
                           // used to stride in the buffer
  ID3D12Resource *transformBuffer, // Buffer containing a 4x4 transform matrix
                                   // in GPU memory, to be applied to the
                                   // vertices. This buffer cannot be nullptr
  UINT64 transformOffsetInBytes,   // Offset of the transform matrix in the
                                   // transform buffer
  BOOL isOpaque /* = TRUE */ // If TRUE, the geometry is considered opaque,
                             // optimizing the search for a closest hit
) {
  AddVertexBuffer(vertexBuffer, vertexOffsetInBytes, vertexCount,
    vertexSizeInBytes, nullptr, 0, 0, DXGI_FORMAT_UNKNOWN, transformBuffer,
    transformOffsetInBytes, isOpaque);
}

//--------------------------------------------------------------------------------------------------
// Add a vertex buffer along with its index buffer in GPU memory into the
// acceleration structure. The vertices are supposed to be represented by 3
// float32 value. This implementation limits the original flexibility of the
// API:
//   - triangles (no custom intersector support)
//   - 3xfloat32 format
//   - 32-bit indices
void BottomLevelASGenerator::AddVertexBuffer(
  ID3D12Resource *vertexBuffer, // Buffer containing the vertex coordinates,
                                // possibly interleaved with other vertex data
  UINT64
  vertexOffsetInBytes, // Offset of the first vertex in the vertex buffer
  UINT vertexCount,    // Number of vertices to consider in the buffer
  UINT vertexSizeInBytes,  // Size of a vertex including all its other data,
                           // used to stride in the buffer
  ID3D12Resource *indexBuffer, // Buffer containing the vertex indices
                               // describing the triangles
  UINT64 indexOffsetInBytes, // Offset of the first index in the index buffer
  UINT indexCount,       // Number of indices to consider in the buffer
  DXGI_FORMAT indexFormat,
  ID3D12Resource *transformBuffer, // Buffer containing a 4x4 transform matrix
                                   // in GPU memory, to be applied to the
                                   // vertices. This buffer cannot be nullptr
  UINT64 transformOffsetInBytes,   // Offset of the transform matrix in the
                                   // transform buffer
  BOOL isOpaque /* = TRUE */ // If TRUE, the geometry is considered opaque,
                             // optimizing the search for a closest hit
) {
  // Create the DX12 descriptor representing the input data, assumed to be
  // opaque triangles, with 3xf32 vertex coordinates and 32-bit indices
  D3D12_RAYTRACING_GEOMETRY_DESC descriptor = {};
  descriptor.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  descriptor.Triangles.VertexBuffer.StartAddress =
    vertexBuffer->GetGPUVirtualAddress() + vertexOffsetInBytes;
  descriptor.Triangles.VertexBuffer.StrideInBytes = vertexSizeInBytes;
  descriptor.Triangles.VertexCount = vertexCount;
  descriptor.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  descriptor.Triangles.IndexBuffer =
    indexBuffer ? (indexBuffer->GetGPUVirtualAddress() + indexOffsetInBytes)
    : 0;
  descriptor.Triangles.IndexFormat =
    indexBuffer ? indexFormat : DXGI_FORMAT_UNKNOWN;
  descriptor.Triangles.IndexCount = indexCount;
  descriptor.Triangles.Transform3x4 =
    transformBuffer
    ? (transformBuffer->GetGPUVirtualAddress() + transformOffsetInBytes)
    : 0;
  descriptor.Flags = isOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
    : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

  m_vertexBuffers.push_back(descriptor);
}

//--------------------------------------------------------------------------------------------------
// Compute the size of the scratch space required to build the acceleration
// structure, as well as the size of the resulting structure. The allocation of
// the buffers is then left to the application
void BottomLevelASGenerator::ComputeASBufferSizes(
  ID3D12Device5 *device, // Device on which the build will be performed
  BOOL allowUpdate,     // If TRUE, the resulting acceleration structure will
                        // allow iterative updates
  UINT64 *scratchSizeInBytes, // Required scratch memory on the GPU to build
                              // the acceleration structure
  UINT64 *resultSizeInBytes   // Required GPU memory to store the acceleration
                              // structure
) {
  // The generated AS can support iterative updates. This may change the final
  // size of the AS as well as the temporary memory requirements, and hence has
  // to be set before the actual build
  m_flags =
    allowUpdate
    ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
    : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

  // Describe the work being requested, in this case the construction of a
  // (possibly dynamic) bottom-level hierarchy, with the given vertex buffers

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildDesc;
  prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  prebuildDesc.NumDescs = static_cast<UINT>(m_vertexBuffers.size());
  prebuildDesc.pGeometryDescs = m_vertexBuffers.data();
  prebuildDesc.Flags = m_flags;

  // This structure is used to hold the sizes of the required scratch memory and
  // resulting AS
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};

  // Building the acceleration structure (AS) requires some scratch space, as
  // well as space to store the resulting structure This function computes a
  // conservative estimate of the memory requirements for both, based on the
  // geometry size.
  device->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);

  // Buffer sizes need to be 256-byte-aligned
  *scratchSizeInBytes =
    ROUND_UP(info.ScratchDataSizeInBytes,
      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
  *resultSizeInBytes = ROUND_UP(info.ResultDataMaxSizeInBytes,
    D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

  // Store the memory requirements for use during build
  m_scratchSizeInBytes = *scratchSizeInBytes;
  m_resultSizeInBytes = *resultSizeInBytes;
}

//--------------------------------------------------------------------------------------------------
// Enqueue the construction of the acceleration structure on a command list,
// using application-provided buffers and possibly a pointer to the previous
// acceleration structure in case of iterative updates. Note that the update can
// be done in place: the result and previousResult pointers can be the same.
HRESULT BottomLevelASGenerator::Generate(
  ID3D12GraphicsCommandList4
  *commandList, // Command list on which the build will be enqueued
  ID3D12Resource *scratchBuffer, // Scratch buffer used by the builder to
                                 // store temporary data
  ID3D12Resource
  *resultBuffer, // Result buffer storing the acceleration structure
  BOOL updateOnly,   // If TRUE, simply refit the existing
                     // acceleration structure
  ID3D12Resource *previousResult // Optional previous acceleration
                                 // structure, used if an iterative update
                                 // is requested
) {
  HRESULT hr;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = m_flags;
  // The stored flags represent whether the AS has been built for updates or
  // not. If yes and an update is requested, the builder is told to only update
  // the AS instead of fully rebuilding it
  if (flags ==
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE &&
    updateOnly) {
    flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
  }

  // Sanity checks
  if (m_flags !=
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE &&
    updateOnly) {
    V_RETURN2((HRESULT)(LONG_PTR)
      "Cannot update a bottom-level AS not originally built for updates", E_INVALIDARG);
  }
  if (updateOnly && previousResult == nullptr) {
    V_RETURN2((HRESULT)(LONG_PTR)
      "Bottom-level hierarchy update requires the previous hierarchy", E_INVALIDARG);
  }

  if (m_resultSizeInBytes == 0 || m_scratchSizeInBytes == 0) {
    V_RETURN2((HRESULT)(LONG_PTR)
      "Invalid scratch and result buffer sizes - ComputeASBufferSizes needs "
      "to be called before Build", E_INVALIDARG);
  }
  // Create a descriptor of the requested builder work, to generate a
  // bottom-level AS from the input parameters
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc;
  buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  buildDesc.Inputs.NumDescs = static_cast<UINT>(m_vertexBuffers.size());
  buildDesc.Inputs.pGeometryDescs = m_vertexBuffers.data();
  buildDesc.DestAccelerationStructureData = {
      resultBuffer->GetGPUVirtualAddress() };
  buildDesc.ScratchAccelerationStructureData = {
      scratchBuffer->GetGPUVirtualAddress() };
  buildDesc.SourceAccelerationStructureData =
    previousResult ? previousResult->GetGPUVirtualAddress() : 0;
  buildDesc.Inputs.Flags = flags;

  // Build the AS
  commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

  // Wait for the builder to complete by setting a barrier on the resulting
  // buffer. This is particularly important as the construction of the top-level
  // hierarchy may be called right afterwards, before executing the command
  // list.
  D3D12_RESOURCE_BARRIER uavBarrier;
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = resultBuffer;
  uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  commandList->ResourceBarrier(1, &uavBarrier);

  return S_OK;
}


///
/// Top Level AS Generator Implementation
///

TopLevelASGenerator::TopLevelASGenerator() {
  m_scratchSizeInBytes = 0;
  m_updateScratchSizeInBytes = 0;
  m_instanceDescsSizeInBytes = 0;
  m_resultSizeInBytes = 0;
}

VOID TopLevelASGenerator::Reserve(size_t instanceCount) {
  m_instances.reserve(instanceCount);
}

//--------------------------------------------------------------------------------------------------
//
// Add an instance to the top-level acceleration structure. The instance is
// represented by a bottom-level AS, a transform, an instance ID and the index
// of the hit group indicating which shaders are executed upon hitting any
// geometry within the instance
void TopLevelASGenerator::AddInstance(
  _In_ ID3D12Resource* bottomLevelAS,      // Bottom-level acceleration structure containing the
                                      // actual geometric data of the instance
  _In_ const DirectX::XMMATRIX& transform, // Transform matrix to apply to the instance, allowing the
                                      // same bottom-level AS to be used at several world-space
                                      // positions
  _In_ UINT instanceID,                    // Instance ID, which can be used in the shaders to
                                      // identify this specific instance
  _In_ UINT hitGroupIndex                  // Hit group index, corresponding the the index of the
                                      // hit group in the Shader Binding Table that will be
                                      // invocated upon hitting the geometry
)
{
  m_instances.emplace_back(Instance(bottomLevelAS, transform, instanceID, hitGroupIndex));
}

//--------------------------------------------------------------------------------------------------
//
// Compute the size of the scratch space required to build the acceleration
// structure, as well as the size of the resulting structure. The allocation of
// the buffers is then left to the application
void TopLevelASGenerator::ComputeASBufferSizes(
  _In_ ID3D12Device5* device, // Device on which the build will be performed
  _In_ BOOL allowUpdate,                        // If TRUE, the resulting acceleration structure will
                                           // allow iterative updates
  _In_ UINT64* scratchSizeInBytes,              // Required scratch memory on the GPU to build
                                           // the acceleration structure
  _In_ UINT64* resultSizeInBytes,               // Required GPU memory to store the acceleration
                                           // structure
  _In_ UINT64* descriptorsSizeInBytes           // Required GPU memory to store instance
                                           // descriptors, containing the matrices,
                                           // indices etc.
)
{
  // The generated AS can support iterative updates. This may change the final
  // size of the AS as well as the temporary memory requirements, and hence has
  // to be set before the actual build
  m_flags = allowUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
    : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

  // Describe the work being requested, in this case the construction of a
  // (possibly dynamic) top-level hierarchy, with the given instance descriptors
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
    prebuildDesc = {};
  prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  prebuildDesc.NumDescs = static_cast<UINT>(m_instances.size());
  prebuildDesc.Flags = m_flags;

  // This structure is used to hold the sizes of the required scratch memory and
  // resulting AS
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};

  // Building the acceleration structure (AS) requires some scratch space, as
  // well as space to store the resulting structure This function computes a
  // conservative estimate of the memory requirements for both, based on the
  // number of bottom-level instances.
  device->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);

  // Buffer sizes need to be 256-byte-aligned
  info.ResultDataMaxSizeInBytes =
    ROUND_UP(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
  info.ScratchDataSizeInBytes =
    ROUND_UP(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

  m_resultSizeInBytes = info.ResultDataMaxSizeInBytes;
  m_scratchSizeInBytes = info.ScratchDataSizeInBytes;
  // The instance descriptors are stored as-is in GPU memory, so we can deduce
  // the required size from the instance count
  m_instanceDescsSizeInBytes =
    ROUND_UP(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * static_cast<UINT64>(m_instances.size()),
      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

  *scratchSizeInBytes = m_scratchSizeInBytes;
  *resultSizeInBytes = m_resultSizeInBytes;
  *descriptorsSizeInBytes = m_instanceDescsSizeInBytes;
}

//--------------------------------------------------------------------------------------------------
//
// Enqueue the construction of the acceleration structure on a command list,
// using application-provided buffers and possibly a pointer to the previous
// acceleration structure in case of iterative updates. Note that the update can
// be done in place: the result and previousResult pointers can be the same.
HRESULT TopLevelASGenerator::Generate(
  _In_ ID3D12GraphicsCommandList4* commandList, // Command list on which the build will be enqueued
  _In_ ID3D12Resource* scratchBuffer,     // Scratch buffer used by the builder to
                                     // store temporary data
  _In_ ID3D12Resource* resultBuffer,      // Result buffer storing the acceleration structure
  _In_ ID3D12Resource* descriptorsBuffer, // Auxiliary result buffer containing the instance
                                     // descriptors, has to be in upload heap
  _In_opt_ BOOL updateOnly /*= FALSE*/,       // If TRUE, simply refit the existing
                                     // acceleration structure
  _In_opt_ ID3D12Resource* previousResult /*= nullptr*/ // Optional previous acceleration
                                               // structure, used if an iterative update
                                               // is requested
)
{
  HRESULT hr;
  // Copy the descriptors in the target descriptor buffer
  D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;
  descriptorsBuffer->Map(0, nullptr, reinterpret_cast<void**>(&instanceDescs));
  if (!instanceDescs) {
    V_RETURN2((HRESULT)(LONG_PTR)"Cannot map the instance descriptor buffer - is it "
      "in the upload heap?", E_INVALIDARG);
  }

  auto instanceCount = static_cast<UINT>(m_instances.size());

  // Initialize the memory to zero on the first time only
  if (!updateOnly) {
    ZeroMemory(instanceDescs, m_instanceDescsSizeInBytes);
  }

  // Create the description for each instance
  for (UINT i = 0; i < instanceCount; i++) {
    // Instance ID visible in the shader in InstanceID()
    instanceDescs[i].InstanceID = m_instances[i].instanceID;
    // Index of the hit group invoked upon intersection
    instanceDescs[i].InstanceContributionToHitGroupIndex = m_instances[i].hitGroupIndex;
    // Instance flags, including backface culling, winding, etc - TODO: should
    // be accessible from outside
    instanceDescs[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    // Instance transform matrix
    DirectX::XMMATRIX m = DirectX::XMMatrixTranspose(
      m_instances[i].transform); // the INSTANCE_DESC is row major
    memcpy(instanceDescs[i].Transform, &m, sizeof(instanceDescs[i].Transform));
    // Get access to the bottom level
    instanceDescs[i].AccelerationStructure = m_instances[i].bottomLevelAS->GetGPUVirtualAddress();
    // Visibility mask, always visible here - TODO: should be accessible from
    // outside
    instanceDescs[i].InstanceMask = 0xFF;
  }

  descriptorsBuffer->Unmap(0, nullptr);

  // If this in an update operation we need to provide the source buffer
  D3D12_GPU_VIRTUAL_ADDRESS pSourceAS = updateOnly ? previousResult->GetGPUVirtualAddress() : 0;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = m_flags;
  // The stored flags represent whether the AS has been built for updates or
  // not. If yes and an update is requested, the builder is told to only update
  // the AS instead of fully rebuilding it
  if (flags == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE && updateOnly) {
    flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
  }

  // Sanity checks
  if (m_flags != D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE && updateOnly) {
    V_RETURN2((HRESULT)(LONG_PTR)"Cannot update a top-level AS not originally built for updates", E_INVALIDARG);
  }
  if (updateOnly && previousResult == nullptr) {
    V_RETURN2((HRESULT)(LONG_PTR)"Top-level hierarchy update requires the previous hierarchy", E_INVALIDARG);
  }

  // Create a descriptor of the requested builder work, to generate a top-level
  // AS from the input parameters
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
  buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  buildDesc.Inputs.InstanceDescs = descriptorsBuffer->GetGPUVirtualAddress();
  buildDesc.Inputs.NumDescs = instanceCount;
  buildDesc.DestAccelerationStructureData = { resultBuffer->GetGPUVirtualAddress()
  };
  buildDesc.ScratchAccelerationStructureData = { scratchBuffer->GetGPUVirtualAddress()
  };
  buildDesc.SourceAccelerationStructureData = pSourceAS;
  buildDesc.Inputs.Flags = flags;

  // Build the top-level AS
  commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

  // Wait for the builder to complete by setting a barrier on the resulting
  // buffer. This can be important in case the rendering is triggered
  // immediately afterwards, without executing the command list
  D3D12_RESOURCE_BARRIER uavBarrier;
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = resultBuffer;
  uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  commandList->ResourceBarrier(1, &uavBarrier);

  return S_OK;
}

//--------------------------------------------------------------------------------------------------
//
//
TopLevelASGenerator::Instance::Instance(ID3D12Resource* blAS, const DirectX::XMMATRIX& tr, UINT iID,
  UINT hgId)
  : bottomLevelAS(blAS), transform(tr), instanceID(iID), hitGroupIndex(hgId)
{
  SAFE_ADDREF(bottomLevelAS);
}

TopLevelASGenerator::Instance::Instance(const Instance &rhs)
: Instance(rhs.bottomLevelAS, rhs.transform, rhs.instanceID, rhs.hitGroupIndex) {
}

TopLevelASGenerator::Instance::~Instance() {
  SAFE_RELEASE(bottomLevelAS);
}




