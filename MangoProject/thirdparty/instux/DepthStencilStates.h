#pragma once
#include <d3d11.h>

namespace DepthStencilStates
{
    // Create a depth-stencil state with depth test enabled or disabled.
    // depthEnable == true  → only draw pixels nearer than existing ones
    // depthEnable == false → draw regardless of depth (through walls)
    ID3D11DepthStencilState* Create(ID3D11Device* device, bool depthEnable);
}