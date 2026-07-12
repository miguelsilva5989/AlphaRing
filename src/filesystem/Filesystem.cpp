#include "Filesystem.h"

#include <filesystem>
#include <fstream>
#include <windows.h>

namespace {
    std::filesystem::path root_path;

    std::filesystem::path ModuleDirectory() {
        wchar_t module_path[MAX_PATH] {};
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&AlphaRing::Filesystem::Init),
                &module) ||
            !GetModuleFileNameW(module, module_path, MAX_PATH))
            return std::filesystem::current_path();
        return std::filesystem::path(module_path).parent_path();
    }

    void MigrateLegacyFile(const std::filesystem::path& module_dir, const char* name) {
        const auto legacy = module_dir / name;
        const auto destination = root_path / name;
        std::error_code error;
        if (!std::filesystem::exists(destination, error) && std::filesystem::exists(legacy, error))
            std::filesystem::copy_file(legacy, destination, std::filesystem::copy_options::skip_existing, error);
    }
}

bool AlphaRing::Filesystem::Init() {
    const auto module_dir = ModuleDirectory();
    root_path = (module_dir / ".." / ".." / ".." / "alpha_ring").lexically_normal();

    std::error_code error;
    std::filesystem::create_directories(root_path, error);
    if (error)
        return false;

    MigrateLegacyFile(module_dir, "settings.json");
    MigrateLegacyFile(module_dir, "custom_mappings.json");
    MigrateLegacyFile(module_dir, "custom_profiles.json");

    return true;
}

bool AlphaRing::Filesystem::Shutdown() {
    return true;
}

const std::filesystem::path& AlphaRing::Filesystem::RootPath() {
    if (root_path.empty())
        root_path = (ModuleDirectory() / ".." / ".." / ".." / "alpha_ring").lexically_normal();
    return root_path;
}

std::filesystem::path AlphaRing::Filesystem::DataPath(const char* file_name) {
    return RootPath() / (file_name ? file_name : "");
}

void AlphaRing::Filesystem::GetDir(const wchar_t *path_in, wchar_t *path_out) {
    std::wcscpy(path_out, std::filesystem::absolute(path_in).c_str());
}

void AlphaRing::Filesystem::GetDir(const char *path_in, wchar_t *path_out) {
    std::wcscpy(path_out, std::filesystem::absolute(path_in).c_str());
}

void AlphaRing::Filesystem::GetResource(const char *path_in, wchar_t *path_out) {
    std::wcsncpy(path_out, DataPath(path_in).c_str(), MAX_PATH - 1);
    path_out[MAX_PATH - 1] = L'\0';
}

bool AlphaRing::Filesystem::Exist(const char *path) {
    return std::filesystem::exists(std::filesystem::path(path));
}

bool AlphaRing::Filesystem::Exist(const wchar_t *path) {
    return std::filesystem::exists(std::filesystem::path(path));
}

bool AlphaRing::Filesystem::Save(const char *file_name, const char *data, size_t size) {
    std::ofstream file(file_name, std::ios::binary);
    if (!file.is_open()) return false;

    file.write(data, size);
    file.close();

    return true;
}
