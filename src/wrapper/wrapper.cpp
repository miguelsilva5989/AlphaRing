#include "module_definition.h"

#include "WtsApi32.h"

static ModuleDefinition& SystemWtsApi32() {
    static ModuleDefinition module {
        WRAPPER_DLL_NAME,
        {
            "WTSRegisterSessionNotification",
            "WTSUnRegisterSessionNotification",
            "WTSQueryUserToken",
            "WTSQuerySessionInformationW",
            "WTSFreeMemory"
        }
    };
    return module;
}

extern "C" {
#pragma comment(linker, "/EXPORT:WTSRegisterSessionNotification=WTSRegisterSessionNotificationWrapper")
BOOL __stdcall WTSRegisterSessionNotificationWrapper(HWND hWnd, DWORD dwFlags) {
    const auto function = reinterpret_cast<BOOL (__stdcall *)(HWND, DWORD)>(SystemWtsApi32().GetFunc(0));
    return function ? function(hWnd, dwFlags) : FALSE;
}

#pragma comment(linker, "/EXPORT:WTSUnRegisterSessionNotification=WTSUnRegisterSessionNotificationWrapper")
BOOL __stdcall WTSUnRegisterSessionNotificationWrapper(HWND hWnd) {
    const auto function = reinterpret_cast<BOOL (__stdcall *)(HWND)>(SystemWtsApi32().GetFunc(1));
    return function ? function(hWnd) : FALSE;
}

#pragma comment(linker, "/EXPORT:WTSQueryUserToken=WTSQueryUserTokenWrapper")
BOOL __stdcall WTSQueryUserTokenWrapper(ULONG SessionId, PHANDLE phToken) {
    const auto function = reinterpret_cast<BOOL (__stdcall *)(ULONG, PHANDLE)>(SystemWtsApi32().GetFunc(2));
    return function ? function(SessionId, phToken) : FALSE;
}

#pragma comment(linker, "/EXPORT:WTSQuerySessionInformationW=WTSQuerySessionInformationWWrapper")
BOOL __stdcall WTSQuerySessionInformationWWrapper(HANDLE hServer, DWORD SessionId, WTS_INFO_CLASS WTSInfoClass,
                                                  LPWSTR *ppBuffer, DWORD *pBytesReturned) {
    const auto function = reinterpret_cast<BOOL (__stdcall *)(HANDLE, DWORD, WTS_INFO_CLASS, LPWSTR*, DWORD*)>(
            SystemWtsApi32().GetFunc(3));
    return function ? function(hServer, SessionId, WTSInfoClass, ppBuffer, pBytesReturned) : FALSE;
}

#pragma comment(linker, "/EXPORT:WTSFreeMemory=WTSFreeMemoryWrapper")
void __stdcall WTSFreeMemoryWrapper(PVOID pMemory) {
    const auto function = reinterpret_cast<void (__stdcall *)(PVOID)>(SystemWtsApi32().GetFunc(4));
    if (function)
        function(pMemory);
}
}
