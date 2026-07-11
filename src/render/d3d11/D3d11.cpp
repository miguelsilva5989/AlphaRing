#include "D3d11.h"

#include <d3d11.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

#include "common.h"
#include "global/Global.h"

#include "imgui.h"

#include "../imgui/ImGui.h"
#include "../Window/Window.h"

namespace AlphaRing::Render::D3d11 {
    bool CreateHook();
    void InitMainRender(IDXGISwapChain *swapChain);

    void *functions[205];

    void *GetFunction(int index) { return functions[index]; }

    HMODULE hD3d11;

    long (__stdcall *p_fD3D11CreateDeviceAndSwapChain)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                       const D3D_FEATURE_LEVEL *, UINT, UINT,
                                                       const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
                                                       D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

    DefDetourFunction(HRESULT, __stdcall, Present, IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (!Graphics()->pSwapChain) {
            InitMainRender(pSwapChain);
        } else if (Graphics()->pSwapChain != pSwapChain) {
            return ppOriginal_Present(pSwapChain, SyncInterval, Flags);
        }

        RenderMirrorSplitWindows();
        ImGui::Render();

        return ppOriginal_Present(pSwapChain, SyncInterval, Flags);
    }

    static MonitorSplitConfig monitor_split {
            true,
            true,
            true,
            false,
            false,
            {0, 0, 6000, 1440},
            {
                    {0, 0, 3440, 1440},
                    {3440, 0, 2560, 1440},
            }
    };

    struct MirrorWindow {
        HWND hwnd = nullptr;
        HWND backdrop_hwnd = nullptr;
        IDXGISwapChain* swap_chain = nullptr;
        UINT buffer_width = 0;
        UINT buffer_height = 0;
        DXGI_FORMAT buffer_format = DXGI_FORMAT_UNKNOWN;
        RectI content_rect {};
    };

    static MirrorWindow mirror_windows[2];
    static ATOM mirror_window_class = 0;
    static const char* mirror_window_class_name = "AlphaRingMirrorWindow";
    static std::atomic<bool> mirror_windows_visible {false};
    static std::atomic<bool> mirror_game_menu_suppressed {false};
    static bool split_transition_logged = false;
    static HANDLE mirror_window_thread = nullptr;
    static HANDLE mirror_window_ready_event = nullptr;
    static DWORD mirror_window_thread_id = 0;

    static constexpr UINT WM_MIRROR_SET_VISIBLE = WM_APP + 0x411;
    static constexpr UINT WM_MIRROR_APPLY_RECT = WM_APP + 0x412;

    struct MirrorWindowBinding {
        int player;
        bool backdrop;
    };

    static MirrorWindowBinding mirror_window_bindings[2][2] {
            {{0, true}, {0, false}},
            {{1, true}, {1, false}},
    };

    static bool PrepareMirrorSwapChainsForStart();

    MonitorSplitConfig& MonitorSplit() {
        return monitor_split;
    }

    struct DetectedMonitor {
        RectI rect;
        bool primary;
    };

    struct MonitorEnumState {
        DetectedMonitor monitors[16];
        int count = 0;
    };

    static void ApplyMirrorWindowRect(HWND hwnd, const MirrorWindowBinding& binding, bool show = false) {
        const auto& rect = binding.backdrop
                ? MonitorSplit().player[binding.player]
                : mirror_windows[binding.player].content_rect;
        SetWindowPos(
                hwnd,
                HWND_TOPMOST,
                rect.x,
                rect.y,
                rect.w,
                rect.h,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | (show ? SWP_SHOWWINDOW : 0)
        );
    }

    static LRESULT CALLBACK MirrorWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCT*>(l_param);
            SetWindowLongPtr(
                    hwnd,
                    GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(create ? create->lpCreateParams : nullptr)
            );
        }

        auto* binding = reinterpret_cast<MirrorWindowBinding*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (binding && message == WM_MIRROR_APPLY_RECT) {
            ApplyMirrorWindowRect(hwnd, *binding);
            return 0;
        }
        if (message == WM_MIRROR_SET_VISIBLE) {
            if (w_param && binding)
                ApplyMirrorWindowRect(hwnd, *binding, true);
            else
                ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (message == WM_MOUSEACTIVATE)
            return MA_NOACTIVATEANDEAT;
        if (message == WM_NCHITTEST)
            return HTTRANSPARENT;
        if (binding && binding->backdrop && message == WM_ERASEBKGND) {
            RECT client {};
            GetClientRect(hwnd, &client);
            FillRect(reinterpret_cast<HDC>(w_param), &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return 1;
        }
        if (binding && binding->backdrop && message == WM_PAINT) {
            PAINTSTRUCT paint {};
            HDC dc = BeginPaint(hwnd, &paint);
            FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd, &paint);
            return 0;
        }

        return DefWindowProc(hwnd, message, w_param, l_param);
    }

    static BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
        auto* state = reinterpret_cast<MonitorEnumState*>(data);
        if (!state || state->count >= static_cast<int>(sizeof(state->monitors) / sizeof(state->monitors[0])))
            return TRUE;

        MONITORINFO info {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfo(monitor, &info))
            return TRUE;

        auto& entry = state->monitors[state->count++];
        entry.rect = {
                info.rcMonitor.left,
                info.rcMonitor.top,
                info.rcMonitor.right - info.rcMonitor.left,
                info.rcMonitor.bottom - info.rcMonitor.top,
        };
        entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        return TRUE;
    }

    bool DetectMonitorLayout() {
        MonitorEnumState state;
        if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&state)) || state.count < 2) {
            LOG_WARNING("Mirror Split: monitor detection found {} display(s); keeping configured rectangles", state.count);
            return false;
        }

        int primary = 0;
        for (int i = 0; i < state.count; ++i) {
            if (state.monitors[i].primary) {
                primary = i;
                break;
            }
        }

        int secondary = -1;
        long long closest_distance = std::numeric_limits<long long>::max();
        const auto& primary_rect = state.monitors[primary].rect;
        const long long primary_x = static_cast<long long>(primary_rect.x) * 2 + primary_rect.w;
        const long long primary_y = static_cast<long long>(primary_rect.y) * 2 + primary_rect.h;

        for (int i = 0; i < state.count; ++i) {
            const auto& rect = state.monitors[i].rect;
            LOG_INFO(
                    "Mirror Split: detected monitor {} at {},{} {}x{} primary={}",
                    i,
                    rect.x,
                    rect.y,
                    rect.w,
                    rect.h,
                    state.monitors[i].primary
            );

            if (i == primary)
                continue;

            const long long dx = static_cast<long long>(rect.x) * 2 + rect.w - primary_x;
            const long long dy = static_cast<long long>(rect.y) * 2 + rect.h - primary_y;
            const long long distance = dx * dx + dy * dy;
            if (distance < closest_distance) {
                closest_distance = distance;
                secondary = i;
            }
        }

        if (secondary < 0)
            return false;

        int left = state.monitors[0].rect.x;
        int top = state.monitors[0].rect.y;
        int right = left + state.monitors[0].rect.w;
        int bottom = top + state.monitors[0].rect.h;
        for (int i = 1; i < state.count; ++i) {
            const auto& rect = state.monitors[i].rect;
            left = std::min(left, rect.x);
            top = std::min(top, rect.y);
            right = std::max(right, rect.x + rect.w);
            bottom = std::max(bottom, rect.y + rect.h);
        }

        auto& config = MonitorSplit();
        config.desktop = {left, top, right - left, bottom - top};
        config.player[0] = state.monitors[primary].rect;
        config.player[1] = state.monitors[secondary].rect;
        return true;
    }

    static bool EnsureMirrorWindowClass() {
        if (mirror_window_class)
            return true;

        WNDCLASS wc {
                CS_HREDRAW | CS_VREDRAW,
                MirrorWindowProc,
                0,
                0,
                GetModuleHandle(nullptr),
                nullptr,
                nullptr,
                static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)),
                nullptr,
                mirror_window_class_name
        };

        mirror_window_class = RegisterClass(&wc);
        if (!mirror_window_class) {
            const auto error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS) {
                LOG_ERROR("Mirror Split: RegisterClass failed: {}", error);
                return false;
            }
        }

        return true;
    }

    static DWORD WINAPI MirrorWindowThreadProc(void*) {
        bool created = true;
        for (int i = 0; i < 2 && created; ++i) {
            auto& mirror = mirror_windows[i];
            const auto& monitor = MonitorSplit().player[i];
            const auto& content = mirror.content_rect;

            mirror.backdrop_hwnd = CreateWindowEx(
                    WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
                    mirror_window_class_name,
                    i == 0 ? "AlphaRing Player 1 Backdrop" : "AlphaRing Player 2 Backdrop",
                    WS_POPUP,
                    monitor.x,
                    monitor.y,
                    monitor.w,
                    monitor.h,
                    Graphics()->hwnd,
                    nullptr,
                    GetModuleHandle(nullptr),
                    &mirror_window_bindings[i][0]
            );
            mirror.hwnd = CreateWindowEx(
                    WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
                    mirror_window_class_name,
                    i == 0 ? "AlphaRing Player 1" : "AlphaRing Player 2",
                    WS_POPUP,
                    content.x,
                    content.y,
                    content.w,
                    content.h,
                    Graphics()->hwnd,
                    nullptr,
                    GetModuleHandle(nullptr),
                    &mirror_window_bindings[i][1]
            );
            created = mirror.backdrop_hwnd && mirror.hwnd;
        }

        SetEvent(mirror_window_ready_event);
        if (!created)
            return 1;

        MSG message {};
        while (GetMessage(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        return 0;
    }

    static bool EnsureMirrorWindowThread() {
        if (mirror_window_thread && WaitForSingleObject(mirror_window_thread, 0) == WAIT_TIMEOUT)
            return mirror_windows[0].hwnd && mirror_windows[1].hwnd;

        if (mirror_window_thread) {
            CloseHandle(mirror_window_thread);
            mirror_window_thread = nullptr;
        }

        mirror_window_ready_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!mirror_window_ready_event)
            return false;

        mirror_window_thread = CreateThread(
                nullptr,
                0,
                MirrorWindowThreadProc,
                nullptr,
                0,
                &mirror_window_thread_id
        );
        if (!mirror_window_thread) {
            CloseHandle(mirror_window_ready_event);
            mirror_window_ready_event = nullptr;
            return false;
        }

        const auto wait = WaitForSingleObject(mirror_window_ready_event, 5000);
        CloseHandle(mirror_window_ready_event);
        mirror_window_ready_event = nullptr;
        if (wait != WAIT_OBJECT_0 || !mirror_windows[0].hwnd || !mirror_windows[1].hwnd) {
            LOG_ERROR("Mirror Split: window thread failed to create all output windows");
            return false;
        }

        LOG_INFO("Mirror Split: window thread ready id={}", mirror_window_thread_id);
        return true;
    }

    static void ReleaseMirrorSwapChain(MirrorWindow& mirror) {
        if (mirror.swap_chain) {
            mirror.swap_chain->Release();
            mirror.swap_chain = nullptr;
        }
        mirror.buffer_width = 0;
        mirror.buffer_height = 0;
        mirror.buffer_format = DXGI_FORMAT_UNKNOWN;
    }

    static void DestroyMirrorWindow(MirrorWindow& mirror) {
        ReleaseMirrorSwapChain(mirror);
        if (mirror.hwnd) {
            DestroyWindow(mirror.hwnd);
            mirror.hwnd = nullptr;
        }
        if (mirror.backdrop_hwnd) {
            DestroyWindow(mirror.backdrop_hwnd);
            mirror.backdrop_hwnd = nullptr;
        }
        mirror.content_rect = {};
    }

    static bool CreateMirrorSwapChain(
            MirrorWindow& mirror,
            UINT buffer_width,
            UINT buffer_height,
            DXGI_FORMAT buffer_format
    ) {
        ReleaseMirrorSwapChain(mirror);

        if (!Graphics()->pIDXGIFactory || !Graphics()->pDevice || !mirror.hwnd ||
            buffer_width == 0 || buffer_height == 0 || buffer_format == DXGI_FORMAT_UNKNOWN)
            return false;

        DXGI_SWAP_CHAIN_DESC desc {};
        desc.BufferDesc.Width = buffer_width;
        desc.BufferDesc.Height = buffer_height;
        desc.BufferDesc.RefreshRate.Numerator = 60;
        desc.BufferDesc.RefreshRate.Denominator = 1;
        desc.BufferDesc.Format = buffer_format;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = mirror.hwnd;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        LOG_INFO(
                "Mirror Split: creating output buffer {}x{} format={} hwnd={:p}",
                buffer_width,
                buffer_height,
                static_cast<unsigned>(buffer_format),
                static_cast<void*>(mirror.hwnd)
        );
        auto result = Graphics()->pIDXGIFactory->CreateSwapChain(
                Graphics()->pDevice,
                &desc,
                &mirror.swap_chain
        );

        if (FAILED(result)) {
            LOG_ERROR("Mirror Split: CreateSwapChain failed: 0x{:x}", static_cast<unsigned>(result));
            mirror.swap_chain = nullptr;
            return false;
        }

        mirror.buffer_width = buffer_width;
        mirror.buffer_height = buffer_height;
        mirror.buffer_format = buffer_format;
        LOG_INFO(
                "Mirror Split: created output buffer {}x{} format={}",
                buffer_width,
                buffer_height,
                static_cast<unsigned>(buffer_format)
        );
        return true;
    }

    static bool EnsureMirrorSwapChain(
            MirrorWindow& mirror,
            UINT buffer_width,
            UINT buffer_height,
            DXGI_FORMAT buffer_format
    ) {
        if (mirror.swap_chain &&
            mirror.buffer_width == buffer_width &&
            mirror.buffer_height == buffer_height &&
            mirror.buffer_format == buffer_format)
            return true;

        return CreateMirrorSwapChain(mirror, buffer_width, buffer_height, buffer_format);
    }

    static RectI CalculateMirrorContentRect(int player) {
        auto& config = MonitorSplit();
        RectI content = config.player[player];
        if (!config.match_primary_aspect || player == 0 ||
            config.player[0].w <= 0 || config.player[0].h <= 0 ||
            content.w <= 0 || content.h <= 0)
            return content;

        const double primary_aspect = static_cast<double>(config.player[0].w) / config.player[0].h;
        const double output_aspect = static_cast<double>(content.w) / content.h;
        if (output_aspect < primary_aspect) {
            const int fitted_height = std::max(1, static_cast<int>(std::lround(content.w / primary_aspect)));
            content.y += (content.h - fitted_height) / 2;
            content.h = fitted_height;
        } else if (output_aspect > primary_aspect) {
            const int fitted_width = std::max(1, static_cast<int>(std::lround(content.h * primary_aspect)));
            content.x += (content.w - fitted_width) / 2;
            content.w = fitted_width;
        }

        return content;
    }

    static void UpdateMirrorContentRect(int player) {
        auto& mirror = mirror_windows[player];
        const auto content = CalculateMirrorContentRect(player);
        if (mirror.content_rect.x == content.x &&
            mirror.content_rect.y == content.y &&
            mirror.content_rect.w == content.w &&
            mirror.content_rect.h == content.h)
            return;

        mirror.content_rect = content;
        if (mirror.hwnd)
            PostMessage(mirror.hwnd, WM_MIRROR_APPLY_RECT, 0, 0);
    }

    static void SetMirrorWindowsVisible(bool visible) {
        if (mirror_windows_visible.load(std::memory_order_acquire) == visible)
            return;

        for (int i = 0; i < 2; ++i) {
            auto& mirror = mirror_windows[i];
            if (!mirror.hwnd)
                continue;

            if (visible) {
                if (mirror.backdrop_hwnd)
                    PostMessage(mirror.backdrop_hwnd, WM_MIRROR_SET_VISIBLE, TRUE, 0);
                PostMessage(mirror.hwnd, WM_MIRROR_SET_VISIBLE, TRUE, 0);
            } else {
                PostMessage(mirror.hwnd, WM_MIRROR_SET_VISIBLE, FALSE, 0);
                if (mirror.backdrop_hwnd)
                    PostMessage(mirror.backdrop_hwnd, WM_MIRROR_SET_VISIBLE, FALSE, 0);
            }
        }

        mirror_windows_visible.store(visible, std::memory_order_release);
        LOG_INFO("Mirror Split: output visibility queued={}", visible);
    }

    void NotifyGameMenuKey() {
        mirror_game_menu_suppressed.store(true, std::memory_order_release);
        SetMirrorWindowsVisible(false);
        LOG_INFO("Mirror Split: hidden for game menu key");
    }

    void NotifyGameplayInput() {
        if (!mirror_game_menu_suppressed.exchange(false, std::memory_order_acq_rel))
            return;

        LOG_INFO("Mirror Split: gameplay input detected; output may resume");
    }

    bool StartMirrorSplitWindows() {
        if (!EnsureMirrorWindowClass())
            return false;

        mirror_game_menu_suppressed.store(false, std::memory_order_release);
        auto& config = MonitorSplit();
        if (config.auto_detect_monitors)
            DetectMonitorLayout();

        for (const auto& rect : config.player) {
            if (rect.w <= 0 || rect.h <= 0) {
                LOG_ERROR("Mirror Split: invalid monitor rectangle {}x{}", rect.w, rect.h);
                return false;
            }
        }

        for (int i = 0; i < 2; ++i) {
            auto& mirror = mirror_windows[i];
            mirror.content_rect = CalculateMirrorContentRect(i);
        }

        if (!EnsureMirrorWindowThread()) {
            LOG_ERROR("Mirror Split: failed to initialize the output window thread");
            return false;
        }
        for (int i = 0; i < 2; ++i) {
            PostMessage(mirror_windows[i].backdrop_hwnd, WM_MIRROR_APPLY_RECT, 0, 0);
            PostMessage(mirror_windows[i].hwnd, WM_MIRROR_APPLY_RECT, 0, 0);
        }

        config.mirror_windows_enabled = true;
        if (!PrepareMirrorSwapChainsForStart()) {
            LOG_ERROR("Mirror Split: failed to prepare output swap chains");
            StopMirrorSplitWindows();
            return false;
        }
        SetMirrorWindowsVisible(!config.auto_hide_when_not_split);
        LOG_INFO(
                "Mirror Split: started p1={},{} {}x{} p2={},{} {}x{} waiting_for_split={}",
                config.player[0].x,
                config.player[0].y,
                config.player[0].w,
                config.player[0].h,
                config.player[1].x,
                config.player[1].y,
                config.player[1].w,
                config.player[1].h,
                config.auto_hide_when_not_split
        );
        return true;
    }

    bool StopMirrorSplitWindows() {
        SetMirrorWindowsVisible(false);
        mirror_game_menu_suppressed.store(false, std::memory_order_release);
        for (auto& mirror : mirror_windows)
            ReleaseMirrorSwapChain(mirror);

        mirror_windows_visible.store(false, std::memory_order_release);
        split_transition_logged = false;
        MonitorSplit().mirror_windows_enabled = false;
        LOG_INFO("Mirror Split: stopped");
        return true;
    }

    struct SourceCrop {
        UINT left;
        UINT top;
        UINT width;
        UINT height;
    };

    static DXGI_FORMAT MirrorSwapChainFormat(DXGI_FORMAT source_format) {
        if (source_format == DXGI_FORMAT_B8G8R8A8_TYPELESS || source_format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        if (source_format == DXGI_FORMAT_R8G8B8A8_TYPELESS || source_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        return source_format;
    }

    static SourceCrop CalculateSourceCrop(
            UINT source_width,
            UINT source_height,
            int player,
            const RectI& output
    ) {
        const UINT player_height = source_height / 2;
        SourceCrop crop {0, player == 0 ? 0u : player_height, source_width, player_height};

        if (source_width == 0 || player_height == 0 || output.w <= 0 || output.h <= 0)
            return crop;

        const double source_aspect = static_cast<double>(source_width) / player_height;
        const double output_aspect = static_cast<double>(output.w) / output.h;
        if (std::abs(source_aspect - output_aspect) <= source_aspect * 0.001)
            return crop;

        if (source_aspect > output_aspect) {
            crop.width = std::clamp(
                    static_cast<UINT>(std::lround(player_height * output_aspect)),
                    1u,
                    source_width
            );
            crop.left = (source_width - crop.width) / 2;
        } else if (source_aspect < output_aspect) {
            crop.height = std::clamp(
                    static_cast<UINT>(std::lround(source_width / output_aspect)),
                    1u,
                    player_height
            );
            crop.top += (player_height - crop.height) / 2;
        }

        return crop;
    }

    static bool PrepareMirrorSwapChainsForStart() {
        if (!Graphics()->pSwapChain)
            return false;

        ID3D11Texture2D* source = nullptr;
        const auto result = Graphics()->pSwapChain->GetBuffer(
                0,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&source)
        );
        if (FAILED(result) || !source)
            return false;

        D3D11_TEXTURE2D_DESC source_desc {};
        source->GetDesc(&source_desc);
        source->Release();

        if (MonitorSplit().native_ce_render && source_desc.Height <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION / 2) {
            source_desc.Height *= 2;
            source_desc.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        }

        LOG_INFO(
                "Mirror Split: preparing for source {}x{} native_requested={}",
                source_desc.Width,
                source_desc.Height,
                MonitorSplit().native_ce_render
        );

        for (int i = 0; i < 2; ++i) {
            const auto crop = CalculateSourceCrop(
                    source_desc.Width,
                    source_desc.Height,
                    i,
                    mirror_windows[i].content_rect
            );
            if (!EnsureMirrorSwapChain(
                    mirror_windows[i],
                    crop.width,
                    crop.height,
                    MirrorSwapChainFormat(source_desc.Format)
            ))
                return false;
        }
        return true;
    }

    void RenderMirrorSplitWindows() {
        const unsigned detected_viewports = ConsumeDetectedSplitViewportMask();
        auto& config = MonitorSplit();
        if (!config.mirror_windows_enabled || !Graphics()->pSwapChain || !Graphics()->pContext)
            return;

        static unsigned split_visibility_grace = 0;
        if ((detected_viewports & 3u) == 3u) {
            split_visibility_grace = 10;
            if (!split_transition_logged) {
                LOG_INFO("Mirror Split: both player viewports detected");
                split_transition_logged = true;
            }
        } else if (split_visibility_grace > 0) {
            --split_visibility_grace;
        } else {
            split_transition_logged = false;
        }

        UpdateMirrorContentRect(0);
        UpdateMirrorContentRect(1);
        const bool should_show =
                (!config.auto_hide_when_not_split || split_visibility_grace > 0) &&
                !AlphaRing::Global::Global()->show_imgui &&
                !mirror_game_menu_suppressed.load(std::memory_order_acquire);
        if (!should_show) {
            SetMirrorWindowsVisible(false);
            return;
        }

        static bool source_acquire_logged = false;
        if (!source_acquire_logged) {
            LOG_INFO("Mirror Split: acquiring first gameplay source");
            source_acquire_logged = true;
        }
        ID3D11Texture2D* source = AcquireNativeMirrorSource();
        HRESULT result = S_OK;
        if (!source) {
            result = Graphics()->pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&source);
            if (FAILED(result) || !source)
                return;
        }

        D3D11_TEXTURE2D_DESC source_desc {};
        source->GetDesc(&source_desc);
        static UINT logged_source_width = 0;
        static UINT logged_source_height = 0;
        if (source_desc.Width != logged_source_width || source_desc.Height != logged_source_height) {
            LOG_INFO(
                    "Mirror Split: presenting source {}x{} format={}",
                    source_desc.Width,
                    source_desc.Height,
                    static_cast<unsigned>(source_desc.Format)
            );
            logged_source_width = source_desc.Width;
            logged_source_height = source_desc.Height;
        }
        if (source_desc.Width == 0 || source_desc.Height < 2 || source_desc.SampleDesc.Count != 1) {
            LOG_ERROR(
                    "Mirror Split: unsupported source buffer {}x{} samples={}",
                    source_desc.Width,
                    source_desc.Height,
                    source_desc.SampleDesc.Count
            );
            source->Release();
            return;
        }

        bool outputs_presented = true;
        for (int i = 0; i < 2; ++i) {
            auto& mirror = mirror_windows[i];
            if (!mirror.hwnd) {
                outputs_presented = false;
                continue;
            }

            const auto crop = CalculateSourceCrop(
                    source_desc.Width,
                    source_desc.Height,
                    i,
                    mirror.content_rect
            );
            const auto output_format = MirrorSwapChainFormat(source_desc.Format);
            if (!EnsureMirrorSwapChain(mirror, crop.width, crop.height, output_format)) {
                outputs_presented = false;
                continue;
            }

            ID3D11Texture2D* destination = nullptr;
            result = mirror.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&destination);
            if (FAILED(result) || !destination) {
                outputs_presented = false;
                continue;
            }

            const D3D11_BOX box {
                    crop.left,
                    crop.top,
                    0,
                    crop.left + crop.width,
                    crop.top + crop.height,
                    1
            };
            Graphics()->pContext->CopySubresourceRegion(destination, 0, 0, 0, 0, source, 0, &box);
            destination->Release();

            result = mirror.swap_chain->Present(0, 0);
            if (FAILED(result)) {
                outputs_presented = false;
                LOG_ERROR("Mirror Split: Present({}) failed: 0x{:x}", i, static_cast<unsigned>(result));
            }
        }

        source->Release();
        if (outputs_presented && !mirror_windows_visible.load(std::memory_order_acquire)) {
            LOG_INFO("Mirror Split: first frames presented; showing output windows");
            SetMirrorWindowsVisible(true);
        }
    }

    DefDetourFunction(HRESULT, __stdcall, ResizeBuffers, IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        if (!Graphics()->pSwapChain) {
            InitMainRender(pSwapChain);
        } else if (Graphics()->pSwapChain != pSwapChain) {
            return ppOriginal_ResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        }

        LOG_INFO(
                "D3D11 ResizeBuffers: count={} size={}x{} format={} flags={}",
                BufferCount,
                Width,
                Height,
                static_cast<unsigned>(NewFormat),
                SwapChainFlags
        );

        if (Graphics()->pView) {
            Graphics()->pView->Release();
            Graphics()->pView = nullptr;
        }

        auto result = ppOriginal_ResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        Graphics()->RecreateRenderTargetView();

        return result;
    }
}

namespace AlphaRing::Render::D3d11 {
    void InitMainRender(IDXGISwapChain *swapChain) {
        DXGI_SWAP_CHAIN_DESC sd;

        if (Graphics()->pSwapChain) {
            if (Graphics()->pSwapChain != swapChain)
                LOG_WARNING("D3D11: ignored additional swap chain {:p}", static_cast<void*>(swapChain));
            return;
        }

        // Get Device and Context
        Graphics()->pSwapChain = swapChain;
        Graphics()->pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&Graphics()->pDevice);
        Graphics()->pDevice->GetImmediateContext(&Graphics()->pContext);

        memcpy(functions + 18, *(void **) Graphics()->pDevice, 43 * sizeof(void *));
        memcpy(functions + 18 + 43, *(void **) Graphics()->pContext, 144 * sizeof(void *));

        bool result = CreateHook();

        assertm(result, "Failed to create hook");

        // Get Factory
        IDXGIDevice * pDXGIDevice = nullptr;
        Graphics()->pDevice->QueryInterface(__uuidof(IDXGIDevice), (void **)&pDXGIDevice);

        IDXGIAdapter * pDXGIAdapter = nullptr;
        pDXGIDevice->GetAdapter( &pDXGIAdapter );

        pDXGIAdapter->GetParent(__uuidof(IDXGIFactory), (void **)&Graphics()->pIDXGIFactory);

        // Release temporary COM objects to prevent memory leaks
        pDXGIAdapter->Release();
        pDXGIDevice->Release();

        // Create Render Target View
        Graphics()->RecreateRenderTargetView();

        // Get Window HWND
        Graphics()->pSwapChain->GetDesc(&sd);
        Graphics()->hwnd = sd.OutputWindow;
        LOG_INFO(
                "D3D11 SwapChain: buffer={}x{} windowed={} output_hwnd={:p}",
                sd.BufferDesc.Width,
                sd.BufferDesc.Height,
                sd.Windowed,
                static_cast<void*>(sd.OutputWindow)
        );

        ImGui::Initialize();
        Window::Initialize();
    }

    bool Initialize() {
        const WNDCLASS wc {
                CS_HREDRAW | CS_VREDRAW,
                DefWindowProc,
                0,
                0,
                GetModuleHandle(nullptr),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                "Default Window"
        };

        bool result;

        result = RegisterClass(&wc);

        assertm(result, "failed to register class");

        auto hwnd = CreateWindow(wc.lpszClassName, "Default Window", WS_OVERLAPPEDWINDOW, 0, 0,
                                     800, 600, nullptr, nullptr, wc.hInstance, nullptr);

        assertm(hwnd != nullptr, "failed to create window");

        hD3d11 = GetModuleHandle("d3d11.dll");

        assertm(hD3d11 != nullptr, "failed to find module \"d3d11.dll\"");

        p_fD3D11CreateDeviceAndSwapChain = (decltype(p_fD3D11CreateDeviceAndSwapChain)) GetProcAddress(
                hD3d11, "D3D11CreateDeviceAndSwapChain");

        assertm(p_fD3D11CreateDeviceAndSwapChain != nullptr, "failed to find function \"D3D11CreateDeviceAndSwapChain\"");

        IDXGISwapChain *swapChain;
        ID3D11Device *device;
        ID3D11DeviceContext *context;

        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_11_0};
        const DXGI_SWAP_CHAIN_DESC swapChainDesc {
                {
                        0,
                        0,
                        {
                                60,
                                1
                        },
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
                        DXGI_MODE_SCALING_UNSPECIFIED
                },
                {
                        1,
                        0
                },
                DXGI_USAGE_RENDER_TARGET_OUTPUT,
                1,
                hwnd,
                1,
                DXGI_SWAP_EFFECT_DISCARD,
                DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
        };

        result = SUCCEEDED(p_fD3D11CreateDeviceAndSwapChain(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, featureLevels, 2, D3D11_SDK_VERSION,
                                                            &swapChainDesc, &swapChain, &device, &featureLevel, &context));

        assertm(result, "failed to create device and swapchain");

        memcpy(functions, *(void **) swapChain, 18 * sizeof(void *));

        swapChain->Release();
        device->Release();
        context->Release();

        DestroyWindow(hwnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);

        result = AlphaRing::Hook::Detour({
            {functions[8],  Present,       (void **) &ppOriginal_Present},
            {functions[13], ResizeBuffers, (void **) &ppOriginal_ResizeBuffers},
        });

        assertm(result, "failed to create d3d11 hook");

        return result;
    }
}
