#include "Render.h"

#include "d3d11/D3d11.h"

bool AlphaRing::Render::Initialize() {
    return AlphaRing::Render::D3d11::Initialize();
}

bool AlphaRing::Render::Shutdown() {
    return AlphaRing::Render::D3d11::Shutdown();
}

void AlphaRing::Render::SetStateWireframe() {
    if (Graphics())
        Graphics()->SetWireframe();
}
