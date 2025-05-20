#pragma once

#include <d3d11.h>
#include <DirectXMath.h>

#include "../../../Bameware Base Shared/Headers/Rendering/Renderer.h"     
#include "../../../Bameware Base External/Headers/MemoryManager.h"

namespace Chams
{
    void Initialize(ID3D11Device * device, ID3D11DeviceContext * context);
    HRESULT __stdcall HookedDrawIndexed(ID3D11DeviceContext * context, UINT IndexCount, UINT StartIndexLocation, INT  BaseVertexLocation);
    bool IsPlayerMesh(ID3D11DeviceContext* context, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);

    DirectX::XMFLOAT4 GetHiddenChamColor();
    DirectX::XMFLOAT4 GetVisibleChamColor();

    inline bool Enabled = false;
}
