#pragma once

#include "common.h"

class EntrySet;

struct Entry {
public:
    Entry(EntrySet* set, __int64 offset, void* pDetour);

    template<typename Detour>
    Entry(EntrySet* set, __int64 offset, Detour pDetour)
        : Entry(set, offset, reinterpret_cast<void*>(pDetour)) {}

    bool update(__int64 hModule);
    bool detach();

    __int64 m_offset;
    void* m_pOriginal;
    __int64 m_target;
    void* m_pDetour;
};

class EntrySet {
public:
    void append(Entry* entry);
    bool update(__int64 hModule);
    bool detach();

private:
    inline static const int MAX_ENTRY = 20;
    int entryCount = 0;
    Entry* entryArray[MAX_ENTRY] {};

};
