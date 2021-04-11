project(Cube VERSION 0.1.0)

set(cube_src_files
InitD3D12App.cpp
RenderItem.cpp
RenderItem.h
)
set(cube_shader_files
  Shaders/box.hlsl
)
set_source_files_properties(${cube_shader_files} PROPERTIES HEADER_FILE_ONLY TRUE)

add_executable(
  Cube
  ${cube_src_files}
)
target_link_libraries(
  Cube
  Common
  DirectXTex
)

set(
  land_and_waves_src_files
  LandAndWavesApp.cpp
  RenderItem.cpp
  RenderItem.h
  Material.cpp
  Material.h
  LightUtils.cpp
  LightUtils.h
  Waves.cpp
  Waves.h
  GpuWaves.cpp
  GpuWaves.h
)
set(land_and_waves_shader_files
  Shaders/GpuWaves.hlsl
  Shaders/LandAndWaves.hlsl
  Shaders/LightUtils.hlsl
  Shaders/TreeBillboard.hlsl
)
set_source_files_properties(${land_and_waves_shader_files} PROPERTIES HEADER_FILE_ONLY TRUE)

add_executable(
  LandAndWavesApp
  ${land_and_waves_src_files}
)
target_link_libraries(
  LandAndWavesApp
  Common
  DirectXTex
)

set(
  shapes_src_files
  ShapesApp.cpp
  CubeMapRenderTarget.cpp
  CubeMapRenderTarget.h
  ShadowMap.cpp
  ShadowMap.h
  LightUtils.cpp
  LightUtils.h
  Material.cpp
  Material.h
  RenderItem.cpp
  RenderItem.h
)
set(
  shapes_shader_files
  Shaders/Shapes.hlsl
  Shaders/ShadowDepth.hlsl
  Shaders/LightUtils.hlsl
  Shaders/Sky.hlsl
)
set_source_files_properties(${shapes_shader_files} PROPERTIES HEADER_FILE_ONLY TRUE)

add_executable(
  ShapesApp
  ${shapes_src_files}
)
target_link_libraries(
  ShapesApp
  Common
  DirectXTex
)

copy_assets(cube_shader_files "Shaders" copied_cube_shader_files)
copy_assets(land_and_waves_shader_files "Shaders" copied_land_and_waves_shader_files)
copy_assets(shapes_shader_files "Shaders" copied_shapes_shader_files)

add_custom_target(
  ${PROJECT_NAME}CopyShaderFiles ALL
  DEPENDS ${copied_cube_shader_files} ${copied_land_and_waves_shader_files} ${copied_shapes_shader_files}
  # SOURCES ${cube_shader_files} ${land_and_waves_shader_files} ${shapes_shader_files}
)

source_group("Cube" FILES ${cube_src_files} ${cube_shader_files})
source_group("LandAndWavesApp" FILES ${land_and_waves_src_files} ${land_and_waves_shader_files})
source_group("ShapesApp" FILES ${shapes_src_files} ${shapes_shader_files})