/*-----------------------------------------------------------------------
Copyright (c) 2014-2018, NVIDIA. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Neither the name of its contributors may be used to endorse
or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/


/*
Contacts for feedback:
- pgautron@nvidia.com (Pascal Gautron)
- mlefrancois@nvidia.com (Martin-Karl Lefrancois)

The bottom-level hierarchy is used to store the triangle data in a way suitable
for fast ray-triangle intersection at runtime. To be built, this data structure
requires some scratch space which has to be allocated by the application.
Similarly, the resulting data structure is stored in an application-controlled
buffer.

To be used, the application must first add all the vertex buffers to be
contained in the final structure, using AddVertexBuffer. After all buffers have
been added, ComputeASBufferSizes will prepare the build, and provide the
required sizes for the scratch data and the final result. The Build call will
finally compute the acceleration structure and store it in the result buffer.

Note that the build is enqueued in the command list, meaning that the scratch
buffer needs to be kept until the command list execution is finished.


Example:

BottomLevelASGenerator bottomLevelAS;
bottomLevelAS.AddVertexBuffer(vertexBuffer, 0, vertexCount, sizeof(Vertex),
identityMat.Get(), 0); bottomLevelAS.AddVertexBuffer(vertexBuffer2, 0,
vertexCount2, sizeof(Vertex), identityMat.Get(), 0);
...
UINT64 scratchSizeInBytes = 0;
UINT64 resultSizeInBytes = 0;
bottomLevelAS.ComputeASBufferSizes(GetRTDevice(), false, &scratchSizeInBytes,
&resultSizeInBytes); AccelerationStructureBuffers buffers; buffers.pScratch =
nv_helpers_dx12::CreateBuffer(..., scratchSizeInBytes, ...); buffers.pResult =
nv_helpers_dx12::CreateBuffer(..., resultSizeInBytes, ...);

bottomLevelAS.Generate(m_commandList.Get(), rtCmdList, buffers.pScratch.Get(),
buffers.pResult.Get(), false, nullptr);

return buffers;

*/

#pragma once
#include "d3dUtils.h"
#include <DirectXMath.h>
#include <vector>

namespace DirectX {
  struct XMMATRIX;
};

/// Helper class to generate bottom-level acceleration structures for raytracing
class BottomLevelASGenerator: private NonCopyable
{
public:
  /// Add a vertex buffer in GPU memory into the acceleration structure. The
  /// vertices are supposed to be represented by 3 float32 value. Indices are
  /// implicit.
  void AddVertexBuffer(ID3D12Resource* vertexBuffer, /// Buffer containing the vertex coordinates,
                                                     /// possibly interleaved with other vertex data
    UINT64 vertexOffsetInBytes,   /// Offset of the first vertex in the vertex
                                  /// buffer
    uint32_t vertexCount,         /// Number of vertices to consider
                                  /// in the buffer
    UINT vertexSizeInBytes,       /// Size of a vertex including all
                                  /// its other data, used to stride
                                  /// in the buffer
    ID3D12Resource* transformBuffer, /// Buffer containing a 4x4 transform
                                     /// matrix in GPU memory, to be applied
                                     /// to the vertices. This buffer cannot
                                     /// be nullptr
    UINT64 transformOffsetInBytes,   /// Offset of the transform matrix in the
                                     /// transform buffer
    BOOL isOpaque = TRUE /// If TRUE, the geometry is considered opaque,
                         /// optimizing the search for a closest hit
  );

  /// Add a vertex buffer along with its index buffer in GPU memory into the acceleration structure.
  /// The vertices are supposed to be represented by 3 float32 value, and the indices are 32-bit
  /// unsigned ints
  void AddVertexBuffer(ID3D12Resource* vertexBuffer, /// Buffer containing the vertex coordinates,
                                                     /// possibly interleaved with other vertex data
    UINT64 vertexOffsetInBytes,   /// Offset of the first vertex in the vertex
                                  /// buffer
    UINT vertexCount,         /// Number of vertices to consider
                                  /// in the buffer
    UINT vertexSizeInBytes,       /// Size of a vertex including
                                  /// all its other data,
                                  /// used to stride in the buffer
    ID3D12Resource* indexBuffer,  /// Buffer containing the vertex indices
                                  /// describing the triangles
    UINT64 indexOffsetInBytes,    /// Offset of the first index in
                                  /// the index buffer
    UINT indexCount,          /// Number of indices to consider in the buffer
    DXGI_FORMAT indexFormat,      /// Index format.
    ID3D12Resource* transformBuffer, /// Buffer containing a 4x4 transform
                                     /// matrix in GPU memory, to be applied
                                     /// to the vertices. This buffer cannot
                                     /// be nullptr
    UINT64 transformOffsetInBytes,   /// Offset of the transform matrix in the
                                     /// transform buffer
    BOOL isOpaque = TRUE /// If TRUE, the geometry is considered opaque,
                         /// optimizing the search for a closest hit
  );

  /// Compute the size of the scratch space required to build the acceleration structure, as well as
  /// the size of the resulting structure. The allocation of the buffers is then left to the
  /// application
  void ComputeASBufferSizes(
    ID3D12Device5* device, /// Device on which the build will be performed
    BOOL allowUpdate,           /// If TRUE, the resulting acceleration structure will
                                /// allow iterative updates
    UINT64* scratchSizeInBytes, /// Required scratch memory on the GPU to
                                /// build the acceleration structure
    UINT64* resultSizeInBytes   /// Required GPU memory to store the
                                /// acceleration structure
  );

  /// Enqueue the construction of the acceleration structure on a command list, using
  /// application-provided buffers and possibly a pointer to the previous acceleration structure in
  /// case of iterative updates. Note that the update can be done in place: the result and
  /// previousResult pointers can be the same.
  HRESULT Generate(
    ID3D12GraphicsCommandList4* commandList, /// Command list on which the build will be enqueued
    ID3D12Resource* scratchBuffer, /// Scratch buffer used by the builder to
                                   /// store temporary data
    ID3D12Resource* resultBuffer,  /// Result buffer storing the acceleration structure
    BOOL updateOnly = FALSE,       /// If TRUE, simply refit the existing acceleration structure
    ID3D12Resource* previousResult = nullptr /// Optional previous acceleration structure, used
                                             /// if an iterative update is requested
  );

private:
  /// Vertex buffer descriptors used to generate the AS
  std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> m_vertexBuffers = {};

  /// Amount of temporary memory required by the builder
  UINT64 m_scratchSizeInBytes = 0;

  /// Amount of memory required to store the AS
  UINT64 m_resultSizeInBytes = 0;

  /// Flags for the builder, specifying whether to allow iterative updates, or
  /// when to perform an update
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS m_flags;
};

/*
Contacts for feedback:
- pgautron@nvidia.com (Pascal Gautron)
- mlefrancois@nvidia.com (Martin-Karl Lefrancois)

The top-level hierarchy is used to store a set of instances represented by
bottom-level hierarchies in a way suitable for fast intersection at runtime. To
be built, this data structure requires some scratch space which has to be
allocated by the application. Similarly, the resulting data structure is stored
in an application-controlled buffer.

To be used, the application must first add all the instances to be contained in
the final structure, using AddInstance. After all instances have been added,
ComputeASBufferSizes will prepare the build, and provide the required sizes for
the scratch data and the final result. The Build call will finally compute the
acceleration structure and store it in the result buffer.

Note that the build is enqueued in the command list, meaning that the scratch
buffer needs to be kept until the command list execution is finished.



Example:

TopLevelASGenerator topLevelAS;
topLevelAS.AddInstance(instances1, matrix1, instanceId1, hitGroupIndex1);
topLevelAS.AddInstance(instances2, matrix2, instanceId2, hitGroupIndex2);
...
UINT64 scratchSize, resultSize, instanceDescsSize;
topLevelAS.ComputeASBufferSizes(GetRTDevice(), true, &scratchSize, &resultSize,
&instanceDescsSize); AccelerationStructureBuffers buffers; buffers.pScratch =
nv_helpers_dx12::CreateBuffer(..., scratchSizeInBytes, ...); buffers.pResult =
nv_helpers_dx12::CreateBuffer(..., resultSizeInBytes, ...);
buffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(..., resultSizeInBytes,
...); topLevelAS.Generate(m_commandList.Get(), rtCmdList,
m_topLevelAS.pScratch.Get(), m_topLevelAS.pResult.Get(),
m_topLevelAS.pInstanceDesc.Get(), updateOnly, updateOnly ?
m_topLevelAS.pResult.Get() : nullptr);

return buffers;

*/

/// Helper class to generate top-level acceleration structures for raytracing
class TopLevelASGenerator
{
public:
  TopLevelASGenerator();

  /// Reserver buffer for instances.
  VOID Reserve(size_t instanceCount);

  /// Add an instance to the top-level acceleration structure. The instance is
  /// represented by a bottom-level AS, a transform, an instance ID and the
  /// index of the hit group indicating which shaders are executed upon hitting
  /// any geometry within the instance
  void
    AddInstance(ID3D12Resource* bottomLevelAS, /// Bottom-level acceleration structure containing the
                                               /// actual geometric data of the instance
      _In_ const DirectX::XMMATRIX& transform, /// Transform matrix to apply to the instance,
                                          /// allowing the same bottom-level AS to be used
                                          /// at several world-space positions
      _In_ UINT instanceID,   /// Instance ID, which can be used in the shaders to
                         /// identify this specific instance
      _In_ UINT hitGroupIndex /// Hit group index, corresponding the the index of the
                         /// hit group in the Shader Binding Table that will be
                         /// invocated upon hitting the geometry
    );

  /// Compute the size of the scratch space required to build the acceleration
  /// structure, as well as the size of the resulting structure. The allocation
  /// of the buffers is then left to the application
  void ComputeASBufferSizes(
    _In_ ID3D12Device5* device, /// Device on which the build will be performed
    _In_ BOOL allowUpdate,              /// If TRUE, the resulting acceleration structure will
                                   /// allow iterative updates
    _Out_ UINT64* scratchSizeInBytes,    /// Required scratch memory on the GPU to
                                   /// build the acceleration structure
    _Out_ UINT64* resultSizeInBytes,     /// Required GPU memory to store the
                                   /// acceleration structure
    _Out_ UINT64* descriptorsSizeInBytes /// Required GPU memory to store instance
                                   /// descriptors, containing the matrices,
                                   /// indices etc.
  );

  /// Enqueue the construction of the acceleration structure on a command list,
  /// using application-provided buffers and possibly a pointer to the previous
  /// acceleration structure in case of iterative updates. Note that the update
  /// can be done in place: the result and previousResult pointers can be the
  /// same.
  HRESULT Generate(
    _In_ ID3D12GraphicsCommandList4* commandList, /// Command list on which the build will be enqueued
    _In_ ID3D12Resource* scratchBuffer,     /// Scratch buffer used by the builder to
                                       /// store temporary data
    _In_ ID3D12Resource* resultBuffer,      /// Result buffer storing the acceleration structure
    _In_ ID3D12Resource* descriptorsBuffer, /// Auxiliary result buffer containing the instance
                                       /// descriptors, has to be in upload heap
    _In_opt_ BOOL updateOnly = FALSE, /// If TRUE, simply refit the existing acceleration structure
    _In_opt_ ID3D12Resource* previousResult = nullptr /// Optional previous acceleration structure, used
                                             /// if an iterative update is requested
  );

private:
  /// Helper struct storing the instance data
  struct Instance
  {
    Instance(ID3D12Resource* blAS, const DirectX::XMMATRIX& tr, UINT iID, UINT hgId);
    Instance(const Instance &rhs);
    ~Instance();
    /// Bottom-level AS
    ID3D12Resource* bottomLevelAS;
    /// Transform matrix
    DirectX::XMMATRIX transform;
    /// Instance ID visible in the shader
    UINT instanceID;
    /// Hit group index used to fetch the shaders from the SBT
    UINT hitGroupIndex;
  };

  /// Construction flags, indicating whether the AS supports iterative updates
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS m_flags;
  /// Instances contained in the top-level AS
  std::vector<Instance> m_instances;

  /// Size of the temporary memory used by the TLAS builder
  UINT64 m_scratchSizeInBytes;
  UINT64 m_updateScratchSizeInBytes;
  /// Size of the buffer containing the instance descriptors
  UINT64 m_instanceDescsSizeInBytes;
  /// Size of the buffer containing the TLAS
  UINT64 m_resultSizeInBytes;
};

