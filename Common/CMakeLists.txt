project(Common VERSION 0.1.0)

set(pch_files
  pch.hpp
)

set(src_files
  SDKmesh.cpp
  SDKmesh.h
  DXUTmisc.cpp
  DXUTmisc.h
  pch.cpp
  HpFileIo.cpp
  HpFileIo.h
  SyncFence.cpp
  SyncFence.hpp
  ResourceUploadBatch.cpp
  ResourceUploadBatch.hpp
  D3D12MemAllocator.cpp
  D3D12MemAllocator.hpp
  Camera.cpp
  D3D12RendererContext.cpp
  D3D12RendererContext.hpp
  d3dUtils.cpp
  DescriptorHeapAllocator.cpp
  Common.cpp
  Common.h
  MeshBuffer.cpp
  RootSignatureGenerator.cpp
  Texture.cpp
  UploadBuffer.cpp
  Camera.h
  d3dUtils.h
  d3dx12.h
  DescriptorHeapAllocator.h
  GeometryGenerator.h
  MeshBuffer.h
  RootSignatureGenerator.h
  Texture.h
  UploadBuffer.h
  Win32Application.cpp
  Win32Application.hpp
)

set(D3D12MemoryAllocatorSrc
  ${D3D12MEMORYALLOCATOR_SOURCE_DIR}/D3D12MemAlloc.h
  ${D3D12MEMORYALLOCATOR_SOURCE_DIR}/D3D12MemAlloc.cpp
)

set(dxr_src_files
DXRHelpers/AccelerationStructureGenerator.cpp
DXRHelpers/AccelerationStructureGenerator.h
DXRHelpers/RaytracingPipelineGenerator.cpp
DXRHelpers/RaytracingPipelineGenerator.h
DXRHelpers/ShaderBindingTableGenerator.cpp
DXRHelpers/ShaderBindingTableGenerator.h
)

set(imgui_src_files
  ${IMGUI_SOURCE_DIR}/imgui.cpp
  ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
  ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
  ${IMGUI_SOURCE_DIR}/imconfig.h
  ${IMGUI_SOURCE_DIR}/imgui.h
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx12.h
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.h
  ${IMGUI_SOURCE_DIR}/imgui_internal.h
  ${IMGUI_SOURCE_DIR}/imstb_rectpack.h
  ${IMGUI_SOURCE_DIR}/imstb_textedit.h
  ${IMGUI_SOURCE_DIR}/imstb_truetype.h
)

add_library(
  ${PROJECT_NAME}
  ${src_files}
  ${dxr_src_files}
  ${imgui_src_files}
  ${D3D12MemoryAllocatorSrc}
)
source_group(imgui ${imgui_src_files})
source_group(DXRHelpers ${dxr_src_files})
source_group(D3D12MemAlloc ${D3D12MemoryAllocatorSrc})

target_link_libraries(
  ${PROJECT_NAME}
  Microsoft::DirectXTex
  d3d12
  dxgi
  dxguid
  d3dcompiler
  dxcompiler
)
target_precompile_headers(${PROJECT_NAME}
  PRIVATE
  ${pch_files}
)
# add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}GenPrecompiledHeaders)

# target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:d3d12.dll")


# compile_shader_dxil("Shaders/GenerateMips.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Compiled" cs CSMain GenerateMips_hlslh)

# add_custom_target(
#   ${PROJECT_NAME}GenPrecompiledHeaders ALL
#   DEPENDS ${GenerateMips_hlslh}
#   SOURCES "Shaders/GenerateMips.hlsl"
# )
