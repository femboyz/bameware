#include "chams.h"
#include "DepthStencilStates.h"

#include <d3d11.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <cstring>

#include "../../../Bameware Base Shared/Headers/Rendering/ShaderManager.h" 
#include "../../../Bameware Base External/Headers/MemoryManager.h"
#include "../../../Bameware Base Internal/Headers/Hooks/VMTHookManager.h"
#include "../MinHook/include/MinHook.h"

using namespace DirectX;

namespace Chams {

    using tDrawIndexed = HRESULT(__stdcall*)(ID3D11DeviceContext*, UINT, UINT, INT);

    static tDrawIndexed oDrawIndexed = nullptr;
    static ID3D11PixelShader* g_PixelShader = nullptr;
    static ID3D11Buffer* g_PSBuffer = nullptr;
    static ID3D11DepthStencilState* g_DS_Off = nullptr; // through‐wall
    static ID3D11DepthStencilState* g_DS_On = nullptr; // normal depth

    struct PSConstants { XMFLOAT4 Color; };

    extern bool Enabled = false;

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        printf("Chams initialized with console ok\n");
        printf("oDrawIndexed = %p\n", (void*)oDrawIndexed);

        auto vtbl = *reinterpret_cast<uintptr_t**>(context);
        printf("Context vtbl @ %p\n", vtbl);
        for (int i = 0; i < 20; i++) {
            printf(" vtbl[%2d] = %p\n", i, (void*)vtbl[i]);
        }

        const char* psSource = R"(
        cbuffer PSConstants : register(b0) {
            float4 Color;
        };
        float4 main() : SV_Target {
            return Color;
        })";

        // 1) Compile a one-line pixel shader that just outputs a constant color
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        HRESULT hr = D3DCompile(
            psSource,                        // your R"(…)" string
            strlen(psSource),                // length in bytes
            nullptr, nullptr, nullptr,       // optional filename, macros, includes
            "main",                          // entrypoint
            "ps_5_0",                        // target profile
            D3D10_SHADER_ENABLE_STRICTNESS,  // compile flags
            0,                               // effect flags
            &psBlob, &errorBlob
        );
        if (FAILED(hr)) {
            if (errorBlob) {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            return;
        }

        // 2) Create the pixel shader from that bytecode
        hr = device->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            &g_PixelShader
        );
        psBlob->Release();
        if (FAILED(hr)) {
            // Optional: log or break here
            return;
        }

        // 2) Create a constant buffer for PSConstants
        D3D11_BUFFER_DESC cbd{};
        cbd.Usage = D3D11_USAGE_DEFAULT;
        cbd.ByteWidth = sizeof(PSConstants);
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        device->CreateBuffer(&cbd, nullptr, &g_PSBuffer);

        // 3) Depth-stencil states: off = draw through walls, on = normal
        g_DS_Off = DepthStencilStates::Create(device, /*depthEnable=*/false);
        g_DS_On = DepthStencilStates::Create(device, /*depthEnable=*/true);

        // 4) Hook ID3D11DeviceContext::DrawIndexed (VT index 12 is common)
        if (MH_Initialize() != MH_OK) {
            printf("[Chams] MinHook initialize failed\n");
            return;    
        }
        
        // Grab the vtable from the DeviceContext
        auto vtbl = *reinterpret_cast<uintptr_t***>(context);
        const int index = 12; // usually DrawIndexed
        void* target = reinterpret_cast<void*>(vtbl[index]);
        
                    // Create and enable the hook
        if (MH_CreateHook(target, &HookedDrawIndexed, reinterpret_cast<void**>(&oDrawIndexed)) != MH_OK) {
            printf("[Chams] MH_CreateHook failed\n");
            return;
            }
        if (MH_EnableHook(target) != MH_OK) {
            printf("[Chams] MH_EnableHook failed\n");
            return;    
        }
         printf("[Chams] oDrawIndexed = %p (VT[%d]=%p)\n", (void*)oDrawIndexed, index, target);
    }


    // ——————————————————————————————————————————
    // Our hooked DrawIndexed: two-pass chams for player meshes
    // ——————————————————————————————————————————

    HRESULT __stdcall HookedDrawIndexed(
        ID3D11DeviceContext* context,
        UINT                 IndexCount,
        UINT                 StartIndexLocation,
        INT                  BaseVertexLocation
    )
    {
        if (!Chams::Enabled) {
            if (IsPlayerMesh(context, IndexCount, StartIndexLocation, BaseVertexLocation))
            {
                // --- backup old state ---
                ID3D11DepthStencilState* oldDS = nullptr;
                UINT                    oldRef = 0;
                context->OMGetDepthStencilState(&oldDS, &oldRef);

                ID3D11PixelShader* oldPS = nullptr;
                context->PSGetShader(&oldPS, nullptr, nullptr);

                // --- pass #1: hidden (through walls) ---
                context->OMSetDepthStencilState(g_DS_Off, 0);
                context->PSSetShader(g_PixelShader, nullptr, 0);
                {
                    PSConstants cb{ GetHiddenChamColor() };
                    context->UpdateSubresource(g_PSBuffer, 0, nullptr, &cb, 0, 0);
                    context->PSSetConstantBuffers(0, 1, &g_PSBuffer);
                }
                oDrawIndexed(context, IndexCount, StartIndexLocation, BaseVertexLocation);

                // --- pass #2: visible (normal depth) ---
                context->OMSetDepthStencilState(g_DS_On, 0);
                {
                    PSConstants cb{ GetVisibleChamColor() };
                    context->UpdateSubresource(g_PSBuffer, 0, nullptr, &cb, 0, 0);
                    context->PSSetConstantBuffers(0, 1, &g_PSBuffer);
                }
                oDrawIndexed(context, IndexCount, StartIndexLocation, BaseVertexLocation);

                // --- restore old state ---
                context->PSSetShader(oldPS, nullptr, 0);
                context->OMSetDepthStencilState(oldDS, oldRef);
                if (oldPS) oldPS->Release();
                if (oldDS) oldDS->Release();

                // skip the original call — we already drew it twice
                return S_OK;
            }
        }

        if (!Enabled)
        return oDrawIndexed(context, IndexCount, StartIndexLocation, BaseVertexLocation);
    }

    // ——————————————————————————————————————————
    // TODO: plug in your own “is this a player model?” logic here
    // ——————————————————————————————————————————
    bool IsPlayerMesh(
        ID3D11DeviceContext* /*context*/,
        UINT                 /*IndexCount*/,
        UINT                 /*StartIndexLocation*/,
        INT                  /*BaseVertexLocation*/
    )
    {
        // e.g. use MemoryManager to enumerate entities, compare mesh/material name,
        // filter out teammates, dormant, dead, etc. 
        return true;
    }

    // ——————————————————————————————————————————
    // TODO: return your chosen RGBA for occluded players
    // ——————————————————————————————————————————
    XMFLOAT4 GetHiddenChamColor()
    {
        return XMFLOAT4(1.0f, 0.0f, 0.0f, 0.5f); // semi-transparent red
    }

    // ——————————————————————————————————————————
    // TODO: return your chosen RGBA for visible players
    // ——————————————————————————————————————————
    XMFLOAT4 GetVisibleChamColor()
    {
        return XMFLOAT4(0.0f, 1.0f, 0.0f, 0.5f); // semi-transparent green
    }
}