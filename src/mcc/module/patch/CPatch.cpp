#include "CPatch.h"
#include "CPatchSet.h"
#include <windows.h>

bool CPatch::apply(void *dst, const void *src, size_t size, void *backup)  {
    if (dst == nullptr || src == nullptr || size == 0)
        return false;

    DWORD old_protect = 0;
    if (!VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &old_protect))
        return false;

    if (backup != nullptr)
        memcpy(backup, dst, size);
    memcpy(dst, src, size);
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), dst, size);

    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(dst, size, old_protect, &ignored);
    return flushed != FALSE && restored != FALSE;
}

bool CPatch::setState(bool state) {
    if (m_enabled == state)
        return true;

    if (!m_parent || !m_parent->loaded()) {
        m_enabled = state;
        if (!state)
            m_applied = false;
        return true;
    }

    if (state) {
        m_enabled = true;
        if (!apply()) {
            m_enabled = false;
            return false;
        }
        return true;
    }

    if (!restore())
        return false;
    m_enabled = false;
    return true;
}

bool CPatch::apply()  {
    if (!m_enabled || m_applied)
        return true;
    if (!m_parent || !m_parent->loaded() || m_offset < 0 || m_data.empty())
        return false;

    auto dst = reinterpret_cast<void*>(m_parent->moduleAddress() + m_offset);
    if (!apply(dst, m_data.data(), m_data.size(), m_backup.data()))
        return false;
    m_applied = true;
    return true;
}

bool CPatch::restore() {
    if (!m_applied)
        return true;
    if (!m_parent || !m_parent->loaded() || m_offset < 0 || m_backup.empty())
        return false;

    auto dst = reinterpret_cast<void*>(m_parent->moduleAddress() + m_offset);
    if (!apply(dst, m_backup.data(), m_backup.size()))
        return false;
    m_applied = false;
    return true;
}
