#include "CPatchSet.h"

CPatchSet::~CPatchSet() {
    detach();
    for (auto* patch : m_embed_patches)
        delete patch;
    for (auto* patch : m_patches)
        delete patch;
}

void CPatchSet::clear() {
    for (auto* patch : m_patches) {
        patch->restore();
        delete patch;
    }
    m_patches.clear();
}

bool CPatchSet::apply() {
    for (auto* patch : m_embed_patches) {
        if (patch->enabled() && !patch->apply()) {
            detach();
            return false;
        }
    }

    for (auto* patch : m_patches) {
        if (patch->enabled() && !patch->apply()) {
            detach();
            return false;
        }
    }
    return true;
}

bool CPatchSet::detach() {
    bool result = true;
    for (auto* patch : m_embed_patches)
        result = patch->restore() && result;
    for (auto* patch : m_patches)
        result = patch->restore() && result;
    hModule = 0;
    return result;
}

void CPatchSet::update(__int64 hModule) {
    if (this->hModule && this->hModule != hModule)
        detach();
    this->hModule = hModule;
}

void CPatchSet::add(const char *name, const char *desc, __int64 offset, const std::vector<__int8> &src, bool enabled) {
    auto patch = new CPatch(name, desc, offset, src, enabled);
    patch->setParent(this);
    m_patches.push_back(patch);
}
