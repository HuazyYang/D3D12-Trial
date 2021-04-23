#include <D3D12RendererContext.hpp>
#include <UIController.hpp>
#include <Win32Application.hpp>
#include "Model.hpp"

using Microsoft::WRL::ComPtr;

HRESULT CreateRendererAndUIInstance(D3D12RendererContext **ppRenderer, IUIController **ppUIController);

int main() {

  HRESULT hr;
  int ret;
  D3D12RendererContext *pRenderer;
  IUIController *pUIController;

  V_RETURN(CreateRendererAndUIInstance(&pRenderer, &pUIController));
  ret = RunSample(pRenderer, pUIController, 800, 600, L"Load Model for D3D12");

  SAFE_DELETE(pRenderer);

  return ret;
}

class LoadModelSample: public D3D12RendererContext, public IUIController {
public:
  HRESULT OnInitPipelines() override;

  void OnFrameMoved(float fTime, float fElapsedTime) override;
  void OnRenderFrame(float fTime, float fElapedTime) override;
};

HRESULT CreateRendererAndUIInstance(D3D12RendererContext **ppRenderer, IUIController **ppUIController) {
  LoadModelSample *pInstance = new LoadModelSample();
  *ppRenderer                = pInstance;
  *ppUIController            = pInstance;
  return S_OK;
}


HRESULT LoadModelSample::OnInitPipelines() {

  HRESULT hr;

  Model model;

  V_RETURN(model.CreateFromSDKMESH(m_pd3dDevice, &m_MemAllocator, LR"(D:\repos\directx-sdk-samples\Media\ShadowColumns\testscene.sdkmesh)",
  ModelLoader_Default));

  return hr;
}

void LoadModelSample::OnFrameMoved(float fTime, float fElapsedTime) {

}

void LoadModelSample::OnRenderFrame(float fTime, float fElapsedTime) {

}
