#include "DepthStencilStates.h"

ID3D11DepthStencilState* DepthStencilStates::Create(ID3D11Device* device, bool depthEnable)
{
    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = depthEnable;
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    desc.DepthFunc = D3D11_COMPARISON_LESS;
    desc.StencilEnable = FALSE;
    desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

    ID3D11DepthStencilState* state = nullptr;
    HRESULT hr = device->CreateDepthStencilState(&desc, &state);
    if (FAILED(hr))
    {
        // handle error (log or assert)
        return nullptr;
    }
    return state;
}