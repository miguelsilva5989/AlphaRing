#include <tchar.h>
#include "Window.h"

#include "common.h"

#include "global/Global.h"

#include "imgui.h"

#include "../D3d11/D3d11.h"

LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace AlphaRing::Render::Window {
    WNDPROC oldWndProc = nullptr;

    static void ReleaseOverlayInput() {
        ImGui::GetIO().ClearInputKeys();
    }

    static bool IsFocusLossMessage(UINT uMsg, WPARAM wParam) {
        return uMsg == WM_KILLFOCUS ||
               (uMsg == WM_ACTIVATEAPP && !wParam) ||
               (uMsg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE);
    }

    static bool IsFocusMessage(UINT uMsg) {
        return uMsg == WM_KILLFOCUS ||
               uMsg == WM_SETFOCUS ||
               uMsg == WM_ACTIVATEAPP ||
               uMsg == WM_ACTIVATE;
    }

    //todo: WM_IME_COMPOSITION Support
    static LRESULT dWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (IsFocusMessage(uMsg)) {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

            if (IsFocusLossMessage(uMsg, wParam)) {
                AlphaRing::Global::Global()->show_imgui = false;
                ReleaseOverlayInput();
            }

            return CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
        }

        switch (uMsg) {
            case WM_KEYDOWN: {
                switch (wParam) {
                    case VK_F4:
                        AlphaRing::Global::Global()->show_imgui = !AlphaRing::Global::Global()->show_imgui;
                        return true;
                    case VK_ESCAPE:
                        AlphaRing::Render::D3d11::NotifyGameMenuKey();
                        break;
                    case 'W':
                    case 'A':
                    case 'S':
                    case 'D':
                    case VK_SPACE:
                        AlphaRing::Render::D3d11::NotifyGameplayInput();
                        break;
                }
                break;
            }
        }

        if (AlphaRing::Global::Global()->show_imgui) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
                return true;

            auto& io = ImGui::GetIO();

            if (io.WantCaptureMouse || io.WantCaptureKeyboard)
                return true;
        }

        return CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
    }

    bool Initialize() {
        oldWndProc = (WNDPROC)SetWindowLongPtr(Graphics()->hwnd, GWLP_WNDPROC, (LONG_PTR)dWndProc);

        return oldWndProc != nullptr;
    }
}
