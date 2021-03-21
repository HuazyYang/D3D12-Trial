#pragma once
#include "d3dUtils.h"
#include <vector>

class RenderableMaterialList;
class RenderableItem;
class Material;

extern HRESULT CreateRenderableMaterialList(RenderableMaterialList **ppRenderableList);

class RenderableMaterialList: Unknown12
{
public:


protected:
  RenderableMaterialList();
  ~RenderableMaterialList();

  /// Diffuse texture.
  Material *m_pDiffuseMap;
  /// Shader binding table.
  std::vector<d3dUtils::SHADER_BINDING_ENTRY> m_aSBT;
  /// Renderable item.
  std::vector<RenderableItem *> m_aRenderableList;
};

