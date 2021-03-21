project(Common VERSION 0.1.0)

set(src_files 
  Camera.cpp
  D3D12App.cpp
  d3dUtils.cpp
  DDSTextureLoader12.cpp
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
  D3D12App.h
  d3dUtils.h
  d3dx12.h
  DDSTextureLoader12.h
  DescriptorHeapAllocator.h
  DirectXTex.h
  GameTimer.h
  GeometryGenerator.h
  ImGuiForDX12.h
  MeshBuffer.h
  RenderableItem.h
  RenderableMaterialList.h
  RootSignatureGenerator.h
  Texture.h
  UploadBuffer.h
)

set(imgui_src_files
  imgui/imgui.cpp
  imgui/imgui_draw.cpp
  imgui/imgui_impl_dx12.cpp
  imgui/imgui_impl_win32.cpp
  imgui/imgui_widgets.cpp
  imgui/imconfig.h
  imgui/imgui.h
  imgui/imgui_impl_dx12.h
  imgui/imgui_impl_win32.h
  imgui/imgui_internal.h
  imgui/imstb_rectpack.h
  imgui/imstb_textedit.h
  imgui/imstb_truetype.h
)

add_library(
  ${PROJECT_NAME}
  ${src_files}
  ${imgui_src_files}
)

target_link_libraries(
  ${PROJECT_NAME}
  DirectXTex
  d3d12
  dxcompiler
  dxgi
  d3dcompiler
)

target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:d3d12.dll")