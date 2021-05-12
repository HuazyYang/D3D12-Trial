# Introduction
This repos is my experimental(trial) to high performance real time rendering engine. Rendering techniques and architectures are shipped with the samples.
## Rendering techniques
 - D3D12 rendering
 - Cascaded Shadowing Mapping(CSM) and Variant Shadowing Mappings(VSM) using D3D12
 - Particle system using D3D12 compute shader architecture

## Rendering architectures
 - Multithreaded Rendering using D3D12

# Build
## Prerequires
 * VC++ compiler support C++17 or higher, install Visual studio 2017 and above with Visual C++ workload is recommanded;
 * cmake with version 3.19.* or above;
 * Third party repos must be placed in the parent directory of this repos's local copy with subdirectory name "ThirdParty". Third party reposes used:
   * DirectXTex
   * Dear ImGui
   * D3D12MemoryAllocator
   * DirectXShaderCompiler
 * Clone [Media](https://github.com/TaylorSevens/Media) into any level of parent folder of this repos' local copy;
 * Clone [directx-sdk-sample](https://github.com/walbourn/directx-sdk-samples) into any level of parent folder of this repos' local copy. We just need some models and textures in theirs folder, nothing else.
## Build steps
 To build debug version, just kick cmake default build procedure;
 To build release version, select cmake variant to Release, the eidtor CmakeCache.txt with the option:`CMAKE_BUILD_TYPE=Release`, then kick off cmake build procedure.