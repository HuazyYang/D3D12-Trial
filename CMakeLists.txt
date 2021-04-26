cmake_minimum_required(VERSION 3.0.0)
project(D3D12-Trial)

include(BuildUtilities.cmake)

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE OR NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Debug' as none was specified.")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()

if(CMAKE_BUILD_TYPE)
  set(TRIAL_OUTDIR_END_SUFFIX ${CMAKE_BUILD_TYPE})
  set(TRIAL_OUTDIR_SUFFIX ${TRIAL_OUTDIR_END_SUFFIX})
else()
  message(FATAL_ERROR "Build type unspecified!")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  add_compile_definitions("_UNICODE" "UNICODE" "WIN32_LEAN_AND_MEAN")
endif()
if(MSVC)
add_compile_options("/MP")
endif()

#  assimp
cmake_policy(SET CMP0012 NEW)
find_package(assimp REQUIRED)
if(NOT assimp_FOUND)
message(FATAL_ERROR "Could not find assimp")
else()
message(STATUS "assimp library: ${ASSIMP_INCLUDE_DIRS}")
endif()

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty")
set(COMMON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Common)
set(DIRECTXTEX_SOURCE_DIR ${THIRD_PARTY_DIR}/DirectXTex/DirectXTex)
set(D3D12MEMORYALLOCATOR_SOURCE_DIR ${THIRD_PARTY_DIR}/D3D12MemoryAllocator/src)
set(ASSIMP_INCLUDE_DIR ${THIRD_PARTY_DIR}/assimp-5.0.1/include)
set(ASSIMP_CONFIG_DIR ${THIRD_PARTY_DIR}/assimp-5.0.1/build/include)


set(HLSL_SHADER_MODEL_VERSION 6_0)
set(DXC_BINARY_DIR ${THIRD_PARTY_DIR}/dxc/bin/x64) # DXC binary path
set(DXC_COMPILER ${DXC_BINARY_DIR}/dxc.exe) # dxc.exe

include_directories(
  ${D3D12MEMORYALLOCATOR_SOURCE_DIR}
  ${THIRD_PARTY_DIR}
  ${DIRECTXTEX_SOURCE_DIR}
  ${COMMON_SOURCE_DIR}
  ${THIRD_PARTY_DIR}/dxc/inc # dxc headers
  ${ASSIMP_INCLUDE_DIRS}
)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
add_compile_definitions("_DEBUG" "DEBUG")
else()
add_compile_definitions("NDEBUG")
endif()

link_directories(
  ${THIRD_PARTY_DIR}/DirectXTex/build/bin/CMake/${TRIAL_OUTDIR_END_SUFFIX}
  ${THIRD_PARTY_DIR}/dxc/lib/x64 ${ASSIMP_LIBRARY_DIRS}
)

add_compile_definitions("UNICODE" "_UNICODE")

add_link_options("/NODEFAULTLIB:libcmt.lib")

add_subdirectory(Common)
# add_subdirectory(D3DTutorial)
# add_subdirectory(NBodyGravity)
# add_subdirectory(HDRToneMappingCS)
# add_subdirectory(HelloDXR)
add_subdirectory(LoadModel)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

