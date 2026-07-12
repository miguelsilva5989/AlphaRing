#include "module_definition.h"

#include <filesystem>

// Use OutputDebugStringA instead of MessageBoxA for Wine/Proton compatibility
// MessageBoxA blocks and can freeze the game on Linux
static void LogError(const char* message) {
    OutputDebugStringA("[AlphaRing Error] ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

ModuleDefinition::ModuleDefinition(const char *moduleName, std::initializer_list<const char*> funcs)
    : m_moduleName(moduleName) {
    m_functionNames.reserve(funcs.size());
    for (const char* func : funcs)
        m_functionNames.emplace_back(func);
}

void ModuleDefinition::Load() {
    // Get System Directory
    wchar_t systemPath[MAX_PATH] {};
    if (!GetSystemDirectoryW(systemPath, MAX_PATH)) {
        LogError("Unable to load system directory");
        return;
    }

    // Load DLL
    std::filesystem::path path(systemPath);
    path.append(m_moduleName);
    if ((m_hModule = LoadLibraryW(path.c_str())) == nullptr) {
        LogError((std::string("Unable to load dll: ") + m_moduleName).c_str());
        return;
    }

    // Find Function
    m_funcs.reserve(m_functionNames.size());
    for (const auto& func : m_functionNames) {
        void* func_ptr = reinterpret_cast<void*>(GetProcAddress(m_hModule, func.c_str()));
        if (!func_ptr) {
            LogError((std::string("Unable to load function: ") + func).c_str());
            m_funcs.clear();
            FreeLibrary(m_hModule);
            m_hModule = nullptr;
            return;
        }

        m_funcs.push_back(func_ptr);
    }
}

void *ModuleDefinition::GetFunc(int index) {
    std::call_once(m_loadOnce, &ModuleDefinition::Load, this);
    if (index < 0 || index >= (int)m_funcs.size()) return nullptr;
    return m_funcs[index];
}
