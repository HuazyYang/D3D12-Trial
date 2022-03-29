cmake_minimum_required(VERSION 3.0.0)
project(D3D12-Trial)

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_BUILD_TYPE $<CONFIG>)
endif()

include(./CMake/BuildUtilities.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  add_compile_definitions("_UNICODE" "UNICODE" "WIN32_LEAN_AND_MEAN")
endif()
if(MSVC)
add_compile_options("/MP")
endif()

find_package(DirectXTex CONFIG REQUIRED)

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty")
set(COMMON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Common)
set(D3D12MEMORYALLOCATOR_SOURCE_DIR ${THIRD_PARTY_DIR}/D3D12MemoryAllocator/src)
set(IMGUI_SOURCE_DIR ${THIRD_PARTY_DIR}/imgui) # For ImGui

set(HLSL_SHADER_MODEL_VERSION 6_0)
set(DXC_BINARY_DIR ${THIRD_PARTY_DIR}/dxc/bin/x64) # DXC binary path
set(DXC_COMPILER ${DXC_BINARY_DIR}/dxc.exe) # dxc.exe

include_directories(
  ${D3D12MEMORYALLOCATOR_SOURCE_DIR}
  ${THIRD_PARTY_DIR}
  ${DIRECTXTEX_SOURCE_DIR}
  ${COMMON_SOURCE_DIR}
  ${THIRD_PARTY_DIR}/dxc/inc # dxc headers
  ${IMGUI_SOURCE_DIR}
  ${IMGUI_SOURCE_DIR}/backends

  ${THIRD_PARTY_DIR}/glm
)

add_compile_options("/fp:fast")

link_directories(
  ${THIRD_PARTY_DIR}/dxc/lib/x64
)

add_compile_definitions("UNICODE" "_UNICODE" "_WIN32_WINNT=0xA00" "WINVER=0xA00")
add_compile_definitions("WIN32_LEAN_AND_MEAN")

add_link_options("/NODEFAULTLIB:libcmt.lib")

add_subdirectory(Common)
add_subdirectory(NBodyGravity)
add_subdirectory(HDRToneMappingCS)
add_subdirectory(HelloDXR)
add_subdirectory(LoadModel)
add_subdirectory(MultithreadedRendering)
add_subdirectory(PredicationQueries)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

