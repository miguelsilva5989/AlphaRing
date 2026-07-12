#include <cstring>
#include "CModule.h"

CModule::CModule(EntrySet *entrySet, std::initializer_list<CPatch> patches)
: m_entries(entrySet), m_patches(patches) {};

bool CModule::load_module(const module_info_t *p_info) {
    if (!p_info || !p_info->hModule || p_info->errorCode != 0)
        return false;

    unload_module();

    m_info = *p_info;
    m_patches.update(m_info.hModule);

    if (m_entries && !m_entries->update(m_info.hModule)) {
        m_patches.detach();
        m_info = {};
        return false;
    }
    if (!m_patches.apply()) {
        if (m_entries)
            m_entries->detach();
        m_info = {};
        return false;
    }
    return true;
}

void CModule::unload_module() {
    if (m_entries)
        m_entries->detach();
    m_patches.detach();
    m_info = {};
}

#include "mcc/module/entry/halo1/halo1.h"
#include "offset_halo2.h"
#include "mcc/module/entry/halo3/halo3.h"
#include "mcc/module/entry/halo3odst/halo3odst.h"
#include "mcc/module/entry/haloreach/haloreach.h"
#include "mcc/module/entry/halo4/halo4.h"
#include "mcc/module/entry/groundhog/groundhog.h"

struct ModuleCollection {
    CModule halo1;
    CModule halo2;
    CModule halo3;
    CModule halo4;
    CModule groundhog;
    CModule halo3odst;
    CModule haloreach;
};

static ModuleCollection& Modules() {
    // The registry intentionally lives for the process lifetime. Constructing it
    // on the bootstrap thread avoids allocation-heavy global constructors in DllMain.
    static ModuleCollection* modules = new ModuleCollection {
    {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALO1_PF_4PLAYERS, "\xEB\x18", true},
        {"splitscreen_patch2", "", OFFSET_HALO1_PF_PAUSE, "\xEB", true},
        {"splitscreen_patch3", "", OFFSET_HALO1_PF_IDK, "\x90\x90\x90\x90\x90\x90", true}, // fix [issue](https://github.com/WinterSquire/AlphaRing/issues/19)
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALO2_PF_PLAYER_VALID, "\x31\xC0\xB0\x01\xC3\x90", true},
        {"splitscreen_patch2", "", OFFSET_HALO2_PF_PLAYER_COUNT1, "\x04", true},
        {"splitscreen_patch3", "", OFFSET_HALO2_PF_PLAYER_COUNT2, "\x04", true},
        {"splitscreen_patch4", "force making splitscreen works with more than 2 players", 0x5153E, "\x83\xF8\x01\x74\x04", true},
}}, {Halo3EntrySet(), {
        {"splitscreen_patch1", "", OFFSET_HALO3_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"Remove Black Bar1", "remove black bar", 0x8AE150/*0x8AD160*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", true},
        {"Remove Black Bar2", "remove black bar", 0x8AE164/*0x8AD174*/, "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01", true},
        {"Remove Black Bar3", "remove black bar", 0x8AE1A0/*0x8AD1B0*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", true},
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALO4_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"splitscreen_patch2", "", OFFSET_HALO4_PF_COOP_REJOIN, "\xEB", true},
        {"splitscreen_patch3", "", OFFSET_HALO4_PF_COOP_PLAYER_LIMIT, "\x90\x90\x90\x90\x90\x90", true},
        {"Remove Black Bar1", "remove black bar", 0xE84E50/*0xE84E40*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", false},
        {"Remove Black Bar2", "remove black bar", 0xE84E64/*0xE84E54*/, "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01", false},
        {"Remove Black Bar3", "remove black bar", 0xE84EA0/*0xE84E90*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", false},
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_GROUNDHOG_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"splitscreen_patch2", "", OFFSET_GROUNDHOG_PF_REJOIN, "\xEB", true},
        {"Remove Black Bar1", "remove black bar", OFFSET_GROUNDHOG_BLACKBAR_1 , "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", false},
        {"Remove Black Bar2", "remove black bar", OFFSET_GROUNDHOG_BLACKBAR_2 , "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01", false},
        {"Remove Black Bar3", "remove black bar", OFFSET_GROUNDHOG_BLACKBAR_3 , "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", false},
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALO3ODST_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"Remove Black Bar1", "remove black bar", 0x8F1FB0/*0x8F1FC0*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", true},
        {"Remove Black Bar2", "remove black bar", 0x8F1FC4/*0x8F1FD4*/, "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01", true},
        {"Remove Black Bar3", "remove black bar", 0x8F2000/*0x8F2010*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", true},
}}, {nullptr, {
        {"splitscreen_patch1", "", OFFSET_HALOREACH_PF_COOP_JOIN, "\x31\xC0\xC3\x90", true},
        {"splitscreen_patch2", "", OFFSET_HALOREACH_PF_COOP_REJOIN, "\xEB", true},
        {"Remove Black Bar1", "remove black bar", 0xB43CE0/*0xB43D10*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", true},
        {"Remove Black Bar2", "remove black bar", 0xB43CF4/*0xB43D24*/, "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01", true},
        {"Remove Black Bar3", "remove black bar", 0xB43D30/*0xB43D60*/, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01", true},
}}};
    return *modules;
}

namespace MCC::Module {
    CModule *GetSubModule(int module) {
        auto& modules = Modules();
        switch (module) {
            case MODULE_HALO1: return &modules.halo1;
            case MODULE_HALO2: return &modules.halo2;
            case MODULE_HALO3: return &modules.halo3;
            case MODULE_HALO4: return &modules.halo4;
            case MODULE_GROUNDHOG: return &modules.groundhog;
            case MODULE_HALO3ODST: return &modules.halo3odst;
            case MODULE_HALOREACH: return &modules.haloreach;
            default: return nullptr;
        }
    }

    CModule *GetSubModule(const char *module_name) {
        if (!module_name)
            return nullptr;
        for (int module = MODULE_HALO1; module < MODULE_MCC; ++module) {
            std::string expected = cModuleName[module];
            expected += ".dll";
            if (_stricmp(expected.c_str(), module_name) == 0)
                return GetSubModule(module);
        }
        return nullptr;
    }
}
