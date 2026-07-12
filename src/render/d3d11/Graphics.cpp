#include "D3d11.h"

static Graphics_t graphics;

Graphics_t* p_graphics = &graphics;

void Graphics_t::SetRenderTargetView() {
    if (pContext && pView)
        pContext->OMSetRenderTargets(1, &pView, nullptr);
}

void Graphics_t::ReleaseRenderTargetView() {
    if (pView) {
        pView->Release();
        pView = nullptr;
    }
}

bool Graphics_t::RecreateRenderTargetView() {
    if (!pSwapChain || !pDevice)
        return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    const HRESULT get_result = pSwapChain->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&pBackBuffer)
    );
    if (FAILED(get_result) || !pBackBuffer)
        return false;

    ID3D11RenderTargetView* new_view = nullptr;
    const HRESULT create_result = pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &new_view);
    pBackBuffer->Release();
    if (FAILED(create_result) || !new_view)
        return false;

    ReleaseRenderTargetView();
    pView = new_view;
    return true;
}

void Graphics_t::SetWireframe() {
    if (!pDevice || !pContext)
        return;

    D3D11_RASTERIZER_DESC r_desc;
    ID3D11RasterizerState* r_state = nullptr;
    ID3D11RasterizerState* r_state_new = nullptr;

    pContext->RSGetState(&r_state);
    if (r_state != nullptr) {
        r_state->GetDesc(&r_desc);
        r_state->Release();  // Release the original state

        r_desc.FillMode = D3D11_FILL_WIREFRAME;
        pDevice->CreateRasterizerState(&r_desc, &r_state_new);
        pContext->RSSetState(r_state_new);

        if (r_state_new != nullptr)
            r_state_new->Release();  // Release after setting (context holds its own ref)
    }
}
