#include "Hook.h"
#include "BuildManifest.h"

#include "common.h"

#include "utils.h"
#include "MinHook.h"

#include <cstdint>
#include <vector>

namespace AlphaRing::Hook {
    enum eDistro {
        Steam,
        WindowsStore,
        None
    };

    static eDistro distro = None;
    static std::uintptr_t hModule = 0;
    static FileVersion version;
    static bool minhook_initialized = false;

    namespace {
        bool CreateAndEnable(const std::vector<Detour_t>& hooks) {
            std::vector<void*> created;
            created.reserve(hooks.size());

            for (const auto& hook : hooks) {
                if (!hook.pTarget || !hook.detour || !hook.ppOriginal) {
                    LOG_ERROR("Refusing invalid hook target={:p} detour={:p}", hook.pTarget, hook.detour);
                    for (auto it = created.rbegin(); it != created.rend(); ++it)
                        MH_RemoveHook(*it);
                    return false;
                }

                const MH_STATUS status = MH_CreateHook(hook.pTarget, hook.detour, hook.ppOriginal);
                if (status != MH_OK) {
                    LOG_ERROR("MH_CreateHook({:p}) failed: {}", hook.pTarget, MH_StatusToString(status));
                    for (auto it = created.rbegin(); it != created.rend(); ++it)
                        MH_RemoveHook(*it);
                    return false;
                }
                created.push_back(hook.pTarget);
            }

            std::vector<void*> enabled;
            enabled.reserve(created.size());
            for (void* target : created) {
                const MH_STATUS status = MH_EnableHook(target);
                if (status != MH_OK) {
                    LOG_ERROR("MH_EnableHook({:p}) failed: {}", target, MH_StatusToString(status));
                    for (auto it = enabled.rbegin(); it != enabled.rend(); ++it)
                        MH_DisableHook(*it);
                    for (auto it = created.rbegin(); it != created.rend(); ++it)
                        MH_RemoveHook(*it);
                    return false;
                }
                enabled.push_back(target);
            }

            return true;
        }

        bool WritePatch(void* target, const char* bytes, size_t size) {
            if (!target || !bytes || size == 0)
                return false;

            DWORD old_protect = 0;
            if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
                LOG_ERROR("VirtualProtect({:p}, {}) failed: {}", target, size, GetLastError());
                return false;
            }

            memcpy(target, bytes, size);
            const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), target, size);

            DWORD ignored = 0;
            const BOOL restored = VirtualProtect(target, size, old_protect, &ignored);
            if (!flushed || !restored) {
                LOG_ERROR("Failed to finalize patch at {:p}: flush={} restore={}", target, flushed != FALSE, restored != FALSE);
                return false;
            }
            return true;
        }
    }

    bool IsWS() {
        return distro == WindowsStore;
    }

    bool Initialize() {
        const auto& build = Compatibility::CurrentBuild();
        const MH_STATUS initialize_status = MH_Initialize();
        if (initialize_status != MH_OK) {
            LOG_ERROR("MH_Initialize failed: {}", MH_StatusToString(initialize_status));
            return false;
        }
        minhook_initialized = true;

		LOG_INFO("Initializing AlphaRing...");
        LOG_INFO("Created by WinterSquire, updated by xTrxplex\n");
		LOG_WARNING(" == This version only supports the steam version of the game ==");

        if ((hModule = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(build.executable))) != 0) {
            distro = Steam;
        } else if ((hModule = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("MCCWinStore-Win64-Shipping.exe"))) != 0) {
            distro = WindowsStore;
        } else {
            distro = None;
        }

        if (distro == None) {
            LOG_ERROR("Supported MCC executable was not found");
            Shutdown();
            return false;
        }

        if (distro == WindowsStore) {
            LOG_ERROR("The Windows Store build is not supported by this offset manifest");
            Shutdown();
            return false;
        }

        LOG_INFO("Game Version[{}]: {}", IsWS() ? "Windows Store" : "Steam", build.version);

        if ((version = FileVersion(static_cast<__int64>(hModule))) != FileVersion::fromString(build.version)) {
            LOG_ERROR("Version mismatch - Expected [{}], Got [{}]", build.version, version.toString());
            Shutdown();
            return false;
        }

        return true;
    }

    bool Shutdown() {
        if (!minhook_initialized)
            return true;

        const MH_STATUS disable_status = MH_DisableHook(MH_ALL_HOOKS);
        if (disable_status != MH_OK && disable_status != MH_ERROR_NOT_CREATED)
            LOG_WARNING("MH_DisableHook(all) failed: {}", MH_StatusToString(disable_status));

        const MH_STATUS uninitialize_status = MH_Uninitialize();
        minhook_initialized = false;
        hModule = 0;
        distro = None;

        return uninitialize_status == MH_OK;
    }

    bool Detour(const std::initializer_list<Detour_t> &hooks) {
        return CreateAndEnable(std::vector<Detour_t>(hooks));
    }

    bool Detour(const std::vector<Detour_t>& hooks) {
        return CreateAndEnable(hooks);
    }

    bool Detour(const std::initializer_list<DetourOffset>& hooks) {
        if (!hModule)
            return false;

        std::vector<Detour_t> resolved;
        resolved.reserve(hooks.size());
        for (const auto& hook : hooks) {
            const __int64 offset = IsWS() ? hook.offset_ws : hook.offset_steam;
            if (offset <= 0)
                return false;
            resolved.emplace_back(
                    reinterpret_cast<void*>(hModule + static_cast<std::uintptr_t>(offset)),
                    hook.detour,
                    hook.ppOriginal
            );
        }
        return CreateAndEnable(resolved);
    }

    bool Detour(const char *module_name, const std::initializer_list<DetourFunction>& hooks) {
        auto hModule = GetModuleHandleA(module_name);

        if (hModule == nullptr)
            return false;

        std::vector<Detour_t> resolved;
        resolved.reserve(hooks.size());
        for (const auto& hook : hooks) {
            auto pTarget = reinterpret_cast<void*>(GetProcAddress(hModule, hook.function_name));

            if (pTarget == nullptr)
                return false;
            resolved.emplace_back(pTarget, hook.detour, hook.ppOriginal);
        }
        return CreateAndEnable(resolved);
    }

    bool Offset(const std::initializer_list<FunctionOffset> &offsets) {
        if (!hModule)
            return false;

        for (const auto& offset : offsets) {
            const __int64 selected = IsWS() ? offset.offset_ws : offset.offset_steam;
            if (!offset.ppFunction || selected <= 0)
                return false;
            *offset.ppFunction = reinterpret_cast<void*>(hModule + static_cast<std::uintptr_t>(selected));
        }
        return true;
    }

    bool Patch(const char *module_name, const std::initializer_list<PatchFunction> &patches) {
        auto hModule = GetModuleHandleA(module_name);

        if (hModule == nullptr)
            return false;

        for (const auto& patch : patches) {
            auto pTarget = (LPVOID)GetProcAddress(hModule, patch.function_name);

            if (!pTarget || !WritePatch(pTarget, patch.patch, patch.size))
                return false;
        }

        return true;
    }

    bool Patch(const std::initializer_list<PatchMCC> &patches) {
        if (!hModule)
            return false;

        for (const auto& patch : patches) {
            const __int64 selected = IsWS() ? patch.offset_ws : patch.offset_steam;
            if (selected <= 0 || !WritePatch(
                    reinterpret_cast<void*>(hModule + static_cast<std::uintptr_t>(selected)),
                    patch.patch,
                    patch.size))
                return false;
        }

        return true;
    }

    bool Remove(void* target) {
        if (!target)
            return true;

        const MH_STATUS disable_status = MH_DisableHook(target);
        if (disable_status != MH_OK && disable_status != MH_ERROR_DISABLED && disable_status != MH_ERROR_NOT_CREATED)
            return false;

        const MH_STATUS remove_status = MH_RemoveHook(target);
        return remove_status == MH_OK || remove_status == MH_ERROR_NOT_CREATED;
    }
}
