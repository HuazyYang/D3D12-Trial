// get AVX intrinsics
#include <immintrin.h>
// get CPUID capability
#include <intrin.h>

#include <iostream>

// written for clarity, not conciseness
#define OSXSAVEFlag (1UL << 27)
#define AVXFlag ((1UL << 28) | OSXSAVEFlag)
#define VAESFlag ((1UL << 25) | AVXFlag | OSXSAVEFlag)
#define FMAFlag ((1UL << 12) | AVXFlag | OSXSAVEFlag)
#define CLMULFlag ((1UL << 1) | AVXFlag | OSXSAVEFlag)

bool DetectFeature(unsigned int feature) {
  int CPUInfo[4], InfoType = 1;
  unsigned int ECX = 1;
  __cpuidex(CPUInfo, 1, 1);       // read the desired CPUID format
  ECX = CPUInfo[2];  // the output of CPUID in the ECX register.
  if ((ECX & feature) != feature) // Missing feature
    return false;
  __int64 val = _xgetbv(0); // read XFEATURE_ENABLED_MASK register
  if ((val & 6) != 6)       // check OS has enabled both XMM and YMM support.
    return false;
  return true;
}

int main() {

  if(DetectFeature(AVXFlag))
    std::cout << "Intel AVX SIMD supported" << std::endl;
  if(DetectFeature(VAESFlag))
    std::cout << "VAES supported" << std::endl;
  if (DetectFeature(CLMULFlag))
    std::cout << "VPCLMULQDQ instruction supported" << std::endl;
  if(DetectFeature(FMAFlag))
    std::cout << "FMA instructions supported" << std::endl;

  return 0;
}