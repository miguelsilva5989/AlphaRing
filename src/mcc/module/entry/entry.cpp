#include "entry.h"

#include <vector>

Entry::Entry(EntrySet* set, __int64 offset, void *pDetour) {
    m_pOriginal = nullptr;
    m_pDetour = pDetour;
    m_offset = offset;
    m_target = 0;

    set->append(this);
}

bool Entry::update(__int64 hModule) {
    if (!hModule || !m_offset || !m_pDetour)
        return false;

    if (!detach())
        return false;

    const auto target = hModule + m_offset;
    if (!AlphaRing::Hook::Detour({
            {reinterpret_cast<void*>(target), m_pDetour, &m_pOriginal},
    }))
        return false;

    m_target = target;
    return true;
}

bool Entry::detach() {
    if (!m_target)
        return true;
    const bool removed = AlphaRing::Hook::Remove(reinterpret_cast<void*>(m_target));
    if (removed) {
        m_target = 0;
        m_pOriginal = nullptr;
    }
    return removed;
}

void EntrySet::append(Entry *entry) {
    if (!entry || entryCount >= MAX_ENTRY)
        return;
    entryArray[entryCount++] = entry;
}

bool EntrySet::update(__int64 hModule) {
    if (!hModule)
        return false;

    if (!detach())
        return false;
    std::vector<AlphaRing::Hook::Detour_t> hooks;
    hooks.reserve(entryCount);
    for (int i = 0; i < entryCount; ++i) {
        Entry* entry = entryArray[i];
        if (!entry || !entry->m_offset || !entry->m_pDetour)
            return false;
        hooks.emplace_back(
                reinterpret_cast<void*>(hModule + entry->m_offset),
                entry->m_pDetour,
                &entry->m_pOriginal
        );
    }

    if (!AlphaRing::Hook::Detour(hooks))
        return false;

    for (int i = 0; i < entryCount; ++i)
        entryArray[i]->m_target = hModule + entryArray[i]->m_offset;
    return true;
}

bool EntrySet::detach() {
    bool result = true;
    for (int i = 0; i < entryCount; ++i) {
        if (entryArray[i])
            result = entryArray[i]->detach() && result;
    }
    return result;
}
