project(NBodyGravity)

set(${PROJECT_NAME}_src_files
  NBodyGravity.cpp
)

file(GLOB ${PROJECT_NAME}_shader_files Shaders/*.hlsl)

add_executable(
  ${PROJECT_NAME}
  ${${PROJECT_NAME}_src_files}
)
target_link_libraries(
  ${PROJECT_NAME}
  Common
  DirectXTex
)

copy_assets(${PROJECT_NAME}_shader_files "Shaders" copied_${PROJECT_NAME}_shader_files)

add_custom_target(
  ${PROJECT_NAME}CopyShaderFiles ALL
  DEPENDS ${copied_${PROJECT_NAME}_shader_files}
)