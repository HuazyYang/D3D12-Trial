cmake_minimum_required(VERSION 3.0.0)

# set common build facilities so sub module can find it
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

include(BuildUtilities.cmake)

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE OR NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Debug' as none was specified.")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
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

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty")
set(COMMON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Common)

add_subdirectory(${THIRD_PARTY_DIR}/glfw ${CMAKE_CURRENT_BINARY_DIR}/glfw)
# you still need DirectXTex package

include_directories(
  $ENV{VK_SDK_PATH}/include
  ${THIRD_PARTY_DIR}/DirectXTex/DirectXTex
  ${COMMON_SOURCE_DIR}
  ${THIRD_PARTY_DIR}/glfw/include
)

if(CMAKE_BUILD_TYPE)
  string(TOUPPER "${CMAKE_BUILD_TYPE}" MY_CXX_BUILD_TYPE_EXT)
  string(TOUPPER "_${CMAKE_BUILD_TYPE}" MY_C_BUILD_TYPE_EXT)
  add_compile_definitions(${MY_C_BUILD_TYPE_EXT} ${MY_CXX_BUILD_TYPE_EXT})

  set(TRIAL_OUTDIR_END_SUFFIX ${CMAKE_BUILD_TYPE})
  set(TRIAL_OUTDIR_SUFFIX ${TRIAL_OUTDIR_END_SUFFIX})

  link_directories(${CMAKE_CURRENT_BINARY_DIR}/glfw/src/${TRIAL_OUTDIR_END_SUFFIX})
  link_directories(${THIRD_PARTY_DIR}/DirectXTex/build/bin/CMake/${TRIAL_OUTDIR_END_SUFFIX})

else()
  message(FATAL_ERROR "Build type unspecified!")
endif()

add_compile_definitions("UNICODE" "_UNICODE")

if(MSVC)
add_link_options("/NODEFAULTLIB:libcmt.lib")
endif(MSVC)

add_subdirectory(D3DTutorial)
add_subdirectory(Common)
add_subdirectory(NBodyGravity)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

