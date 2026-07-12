#pragma once

#include <d3d11.h>

struct Graphics_t {
    ID3D11Device *pDevice = nullptr;
    ID3D11DeviceContext *pContext = nullptr;
    IDXGISwapChain *pSwapChain = nullptr;
    ID3D11RenderTargetView *pView = nullptr;
    IDXGIFactory *pIDXGIFactory = nullptr;
    HWND hwnd = nullptr;

    void SetRenderTargetView();
    void ReleaseRenderTargetView();
    bool RecreateRenderTargetView();
    void SetWireframe();
};

extern Graphics_t* p_graphics;
inline Graphics_t* Graphics() {return p_graphics;}
