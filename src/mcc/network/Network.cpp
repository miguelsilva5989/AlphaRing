#include "Network.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <WinSock2.h>
#include <windows.h>
#include <winhttp.h>

#include "common.h"

namespace MCC::Network {
    std::mutex mutex;
    bool b_enable_capture;
    std::vector<RequestInfo*> request_list;
    std::unordered_map<HINTERNET, std::wstring> map_url;
    std::unordered_map<HINTERNET, RequestInfo*> map_request;

    void set_url(HINTERNET hConnect, LPCWSTR url) {
        if (!b_enable_capture) return;
        std::lock_guard<std::mutex> lock(mutex);
        map_url[hConnect] = url;
    }

    bool set_header(HINTERNET hRequest, LPCWSTR header) {
        char buffer[1024*1024];

        if (!b_enable_capture) return false;

        std::lock_guard<std::mutex> lock(mutex);
        auto it = map_request.find(hRequest);
        if (it == map_request.end()) return false;

        sprintf(buffer, "Header: %ls", header);
        it->second->headers += buffer;

        return true;
    }

    bool set_body(HINTERNET hRequest, LPCVOID lpBuffer, DWORD dwNumberOfBytes) {
        char buffer[1024*1024];

        if (!b_enable_capture) return false;

        std::lock_guard<std::mutex> lock(mutex);

        auto it = map_request.find(hRequest);
        if (it == map_request.end()) return false;

        sprintf(buffer, "%.*s", dwNumberOfBytes, (char*)lpBuffer);
        it->second->body += std::string(buffer);

        return true;
    }

    bool set_response(HINTERNET hRequest, LPCVOID lpBuffer, DWORD dwNumberOfBytes) {
        char buffer[1024*1024];

        if (!b_enable_capture) return false;

        std::lock_guard<std::mutex> lock(mutex);
        auto it = map_request.find(hRequest);
        if (it == map_request.end()) return false;

        sprintf(buffer, "%.*s", dwNumberOfBytes, (char*)lpBuffer);

        it->second->response += std::string(buffer);

        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);

        for (auto request: request_list) {
            delete request;
        }

        map_url.clear();
        map_request.clear();
        request_list.clear();
    }

    void add(HINTERNET hRequest, HINTERNET hConnect, LPCWSTR method, LPCWSTR path) {
        char buffer[1024*1024];

        if (!b_enable_capture) return;

        std::lock_guard<std::mutex> lock(mutex);

        sprintf(buffer, "%ls", method);

        auto str_method = std::string(buffer);

        sprintf(buffer, "%ls%ls", map_url[hConnect].c_str(), path);

        auto str_url = std::string(buffer);

        auto new_request = new RequestInfo {
                true,
                map_url[hConnect].length(),
                std::move(str_method),
                std::move(str_url),
                "",
                "",
                ""
        };

        map_request[hRequest] = new_request;
        request_list.emplace_back(new_request);
    }

    void close(HINTERNET hRequest) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = map_request.find(hRequest);
        if (it == map_request.end()) return;

        it->second->status = false;
    }
}

namespace MCC::Network {
    DefDetourFunction(HINTERNET, __stdcall, dWinHttpConnect, HINTERNET hSession, LPCWSTR pswzServerName, INTERNET_PORT nServerPort, DWORD dwReserved) {
        HINTERNET result = ppOriginal_dWinHttpConnect(hSession, pswzServerName, nServerPort, dwReserved);
        set_url(result, pswzServerName);
        return result;
    }

    DefDetourFunction(HINTERNET, __stdcall, dWinHttpOpenRequest, HINTERNET hConnect, LPCWSTR pwszVerb, LPCWSTR pwszObjectName, LPCWSTR pwszVersion, LPCWSTR pwszReferrer, LPCWSTR* ppwszAcceptTypes, DWORD dwFlags) {
        HINTERNET result = ppOriginal_dWinHttpOpenRequest(hConnect, pwszVerb, pwszObjectName, pwszVersion, pwszReferrer, ppwszAcceptTypes, dwFlags);
        add(result, hConnect, pwszVerb, pwszObjectName);
        return result;
    }

    DefDetourFunction(BOOL, __stdcall, dWinHttpAddRequestHeaders, HINTERNET hRequest, LPCWSTR pwszHeaders, DWORD dwHeadersLength, DWORD dwModifiers) {
        set_header(hRequest, pwszHeaders);
        return ppOriginal_dWinHttpAddRequestHeaders(hRequest, pwszHeaders, dwHeadersLength, dwModifiers);
    }

    DefDetourFunction(BOOL, __stdcall, dWinHttpWriteData, HINTERNET hRequest, LPCVOID lpBuffer, DWORD dwNumberOfBytesToWrite, LPDWORD lpdwNumberOfBytesWritten) {
        set_body(hRequest, lpBuffer, dwNumberOfBytesToWrite);
        return ppOriginal_dWinHttpWriteData(hRequest, lpBuffer, dwNumberOfBytesToWrite, lpdwNumberOfBytesWritten);
    }

    DefDetourFunction(BOOL, __stdcall, dWinHttpReadData, HINTERNET hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead) {
        auto result = ppOriginal_dWinHttpReadData(hRequest, lpBuffer, dwNumberOfBytesToRead, lpdwNumberOfBytesRead);
        set_response(hRequest, lpBuffer, dwNumberOfBytesToRead);
        return result;
    }

    DefDetourFunction(BOOL, __stdcall, dWinHttpCloseHandle, HINTERNET hInternet) {
        close(hInternet);
        return ppOriginal_dWinHttpCloseHandle(hInternet);
    }

    bool Initialize() {
        bool result;

        result = AlphaRing::Hook::Detour("winhttp.dll", {
                { "WinHttpConnect", (void**)&ppOriginal_dWinHttpConnect, dWinHttpConnect },
                { "WinHttpOpenRequest", (void**)&ppOriginal_dWinHttpOpenRequest, dWinHttpOpenRequest },
                { "WinHttpAddRequestHeaders", (void**)&ppOriginal_dWinHttpAddRequestHeaders, dWinHttpAddRequestHeaders },
                { "WinHttpWriteData", (void**)&ppOriginal_dWinHttpWriteData, dWinHttpWriteData },
                { "WinHttpReadData", (void**)&ppOriginal_dWinHttpReadData, dWinHttpReadData },
                { "WinHttpCloseHandle", (void**)&ppOriginal_dWinHttpCloseHandle, dWinHttpCloseHandle }
        });

        assertm(result, "MCC:Network: failed to create hook");

        return true;
    }
}

namespace MCC::Network {
    DefDetourFunction(int, __stdcall, dWSASend, SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent, DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
        auto result = ppOriginal_dWSASend(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine);

        printf("WSASend: %.*s\n", lpBuffers->len, lpBuffers->buf);

        return result;
    }

    DefDetourFunction(int, __stdcall, dWSARecv, SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd, LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
        auto result = ppOriginal_dWSARecv(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped, lpCompletionRoutine);

        printf("WSARecv: %.*s\n", lpBuffers->len, lpBuffers->buf);

        return result;
    }
}

#include "imgui.h"

namespace MCC::Network {
    void RealContext();

    void ImGuiContext() {
        static bool show_network;

        if (ImGui::BeginMainMenuBar()) {
            ImGui::MenuItem("Network", nullptr, &show_network);
            ImGui::EndMainMenuBar();
        }

        if (show_network) {
            ImGui::SetNextWindowSize(ImVec2(940.0f, 620.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(680.0f, 420.0f), ImVec2(1600.0f, 1200.0f));
            if (ImGui::Begin("Network Inspector", &show_network, ImGuiWindowFlags_NoCollapse))
                RealContext();
            ImGui::End();
        }
    }


    void RealContext() {
        static int index = -1;

        ImGui::Checkbox("Capture traffic", &b_enable_capture);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            index = -1;
            clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu requests", request_list.size());
        ImGui::Separator();

        if (!request_list.empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                if (index - 1 < 0)
                    index = request_list.size() - 1;
                else
                    index -= 1;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                if (index + 1 >= request_list.size())
                    index = 0;
                else
                    index += 1;
        }

        const float list_width = std::max(250.0f, ImGui::GetContentRegionAvail().x * 0.34f);
        ImGui::BeginChild("Request List", {list_width, 0}, true);
        ImGui::TextDisabled("Requests");
        ImGui::Separator();
        for (int i = 0; i < request_list.size(); ++i) {
            auto info = request_list[i];
            ImGui::PushID(i);
            const size_t offset = std::min(static_cast<size_t>(info->url_length), info->url.size());
            const char* label = info->url.c_str() + offset;
            if (!label[0])
                label = info->url.c_str();
            if (ImGui::Selectable(label, index == i))
                index = i;
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("Requests Detail", ImVec2(0, 0), true);

        if (index >= 0 && index < request_list.size()) {
            auto current_info = request_list[index];

            ImGui::Text("%s", current_info->method.c_str());
            ImGui::SameLine();
            ImGui::TextColored(
                    current_info->status ? ImVec4(0.32f, 0.79f, 0.58f, 1.0f) : ImVec4(0.91f, 0.35f, 0.33f, 1.0f),
                    current_info->status ? "Active" : "Inactive"
            );
            ImGui::TextWrapped("%s", current_info->url.c_str());
            ImGui::Spacing();

            if (ImGui::BeginTabBar("NetworkPayload")) {
                const auto draw_payload = [](const char* tab, const char* child, const std::string& payload) {
                    if (ImGui::BeginTabItem(tab)) {
                        ImGui::BeginChild(child, ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
                        ImGui::TextUnformatted(payload.data(), payload.data() + payload.size());
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                };
                draw_payload("Headers", "HeadersPayload", current_info->headers);
                draw_payload("Request body", "RequestPayload", current_info->body);
                draw_payload("Response", "ResponsePayload", current_info->response);
                ImGui::EndTabBar();
            }
        } else {
            ImGui::TextDisabled("Select a request");
        }

        ImGui::EndChild();
    }
};
