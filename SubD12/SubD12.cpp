#include <D3D12RendererContext.hpp>
#include <Win32Application.hpp>

HRESULT CreateSubD12Sample(D3D12RendererContext **ppRenderer, WindowInteractor **ppInteractor);

int main() {

  HRESULT hr;
  D3D12RendererContext *pRenderer;
  WindowInteractor *pInteractor;

  V_RETURN(CreateSubD12Sample(&pRenderer, &pInteractor));
  hr = RunSample(pRenderer, pInteractor, 800, 600, L"SubD12");
  SAFE_DELETE(pRenderer);

  return hr;
}

class SubD12Sample: public D3D12RendererContext, public WindowInteractor {

private:
  HRESULT OnInitPipelines() override;
  void OnResizeFrame(int cx, int cy) override;
  void OnFrameMoved(float fTime, float fElapsedTime) override;
  void OnRenderFrame(float fTime, float fElapsedTime) override;
};

HRESULT CreateSubD12Sample(D3D12RendererContext **ppRenderer, WindowInteractor **ppInteractor) {
  SubD12Sample *pSample = new SubD12Sample;
  *ppRenderer = pSample;
  *ppInteractor = pSample;
}

