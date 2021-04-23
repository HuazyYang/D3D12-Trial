#include "d3dUtils.h"
#include "HpFileIo.h"

int main() {

  HRESULT hr;
  HpFileIo::IFileDataBlob *pResult = nullptr;

  V_RETURN(HpFileIo::ReadFileDirectly(
    LR"(D:\repos\OpenGLTutorial\asset\textures\hdr\newport_loft.hdr)",
    0, 0,  &pResult
  ));
  SAFE_RELEASE(pResult);

  V_RETURN(HpFileIo::ReadFileDirectly(
    LR"(D:\repos\OpenGLTutorial\asset\textures\hdr\newport_loft.hdr)",
    1324, 81985,  &pResult
  ));
  SAFE_RELEASE(pResult);

  V_RETURN(HpFileIo::ReadFileDirectly(
    LR"(D:\repos\OpenGLTutorial\asset\textures\hdr\newport_loft.hdr)",
    13245, 0,  &pResult
  ));
  SAFE_RELEASE(pResult);
}