#include "common.h"
#include "runtime/Runtime.h"

BOOL APIENTRY DllMain(HANDLE handle, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(static_cast<HMODULE>(handle));
        HANDLE thread = CreateThread(nullptr, 0, AlphaRing::Runtime::Bootstrap, handle, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        // DllMain runs under the loader lock. Process teardown owns OS resources;
        // explicit unload must request shutdown before calling FreeLibrary.
        if (reserved == nullptr)
            AlphaRing::Runtime::RequestStop();
    }

    return TRUE;
}
