project(MultithreadedRendering)

set(${PROJECT_NAME}_src_files
  MultithreadedRendering.cpp
  MultithreadedDXUTMesh.h
  MultithreadedDXUTMesh.cpp
)

file(GLOB ${PROJECT_NAME}_shader_files Shaders/*.hlsl)

add_executable(
  ${PROJECT_NAME}
  ${${PROJECT_NAME}_src_files}
)
target_link_options(${PROJECT_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")
target_link_libraries(
  ${PROJECT_NAME}
  Common
)

copy_assets(${PROJECT_NAME}_shader_files "Shaders" copied_${PROJECT_NAME}_shader_files)

add_custom_target(
  ${PROJECT_NAME}CopyShaderFiles ALL
  DEPENDS ${copied_${PROJECT_NAME}_shader_files}
)