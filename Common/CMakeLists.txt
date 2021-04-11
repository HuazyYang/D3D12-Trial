project(Common VERSION 0.1.0)

set(src_files 
  Camera.cpp
  D3D12RendererContext.cpp
  D3D12RendererContext.hpp
  d3dUtils.cpp
  DescriptorHeapAllocator.cpp
  Common.cpp
  Common.h
  GameTimer.cpp
  MeshBuffer.cpp
  RenderableItem.cpp
  RenderableMaterialList.cpp
  RootSignatureGenerator.cpp
  Texture.cpp
  UploadBuffer.cpp
  Camera.h
  d3dUtils.h
  d3dx12.h
  DescriptorHeapAllocator.h
  GameTimer.hpp
  GeometryGenerator.h
  MeshBuffer.h
  RenderableItem.h
  RenderableMaterialList.h
  RootSignatureGenerator.h
  Texture.h
  UploadBuffer.h
  Win32Application.cpp
  Win32Application.hpp
  UIController.hpp
  ${DIRECTXTEX_SOURCE_DIR}/DDSTextureLoader/DDSTextureLoader12.cpp
  ${DIRECTXTEX_SOURCE_DIR}/DDSTextureLoader/DDSTextureLoader12.h
)

set(dxr_src_files
DXRHelpers/AccelerationStructureGenerator.cpp
DXRHelpers/AccelerationStructureGenerator.h
DXRHelpers/RaytracingPipelineGenerator.cpp
DXRHelpers/RaytracingPipelineGenerator.h
DXRHelpers/ShaderBindingTableGenerator.cpp
DXRHelpers/ShaderBindingTableGenerator.h
)

include_directories(${THIRD_PARTY_DIR}/imgui)
set(imgui_src_files
  ${THIRD_PARTY_DIR}/imgui/imgui.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_dx12.cpp
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_win32.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
  ${THIRD_PARTY_DIR}/imgui/imconfig.h
  ${THIRD_PARTY_DIR}/imgui/imgui.h
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_dx12.h
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_win32.h
  ${THIRD_PARTY_DIR}/imgui/imgui_internal.h
  ${THIRD_PARTY_DIR}/imgui/imstb_rectpack.h
  ${THIRD_PARTY_DIR}/imgui/imstb_textedit.h
  ${THIRD_PARTY_DIR}/imgui/imstb_truetype.h
)

add_library(
  ${PROJECT_NAME}
  ${src_files}
  ${dxr_src_files}
  ${imgui_src_files}
)
source_group(imgui ${imgui_src_files})
source_group(DXRHelpers ${dxr_src_files})

target_link_libraries(
  ${PROJECT_NAME}
  DirectXTex
  d3d12
  dxgi
  dxguid
  d3dcompiler
  dxcompiler
)

target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:d3d12.dll")