#pragma once

#include "Graphics.h"
#include "MonitorLayout.h"

namespace AlphaRing::Render::D3d11 {
    struct MonitorSplitConfig {
        bool auto_detect_monitors;
        bool auto_hide_when_not_split;
        bool match_primary_aspect;
        bool native_ce_render;
        bool mirror_windows_enabled;
        RectI desktop;
        RectI player[2];
    };

    bool Initialize();
    bool Shutdown();

    void* GetFunction(int index);
    MonitorSplitConfig& MonitorSplit();
    bool DetectMonitorLayout();
    bool StartMirrorSplitWindows();
    bool StopMirrorSplitWindows();
    void RenderMirrorSplitWindows();
    void NotifyGameMenuKey();
    void NotifyGameplayInput();
    unsigned ConsumeDetectedSplitViewportMask();
    void NotifyMccModuleLoaded(int title);
    void NotifyMccModuleUnloaded(int title);
    void NotifyMainSwapChainResize();
    void SetNativeRenderEnabled(bool enabled);
    bool NativeRenderActive();
    const char* NativeRenderStatus();
    ID3D11Texture2D* AcquireNativeMirrorSource();
}
