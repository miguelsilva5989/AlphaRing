#pragma once

#include <windows.h>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

class ModuleDefinition {
public:
    ModuleDefinition(const char *moduleName, std::initializer_list<const char*> funcs);

    void* GetFunc(int index);
private:
    void Load();

    std::string m_moduleName;
    std::vector<std::string> m_functionNames;
    std::once_flag m_loadOnce;
    HMODULE m_hModule = nullptr;
    std::vector<void*> m_funcs;
};
