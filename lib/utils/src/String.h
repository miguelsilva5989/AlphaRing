#pragma once

#include <cstddef>

#define STR_CPY(return_type, name, ...) inline return_type name(__VA_ARGS__) { \
    if (dest == nullptr || n == 0) return 0; \
    if (src == nullptr) { dest[0] = 0; return 0; } \
    size_t i = 0; \
    for (; i + 1 < n && src[i] != 0; ++i) { \
        dest[i] = src[i]; \
    } \
    dest[i] = 0; \
    return i; \
} \

namespace String {
    STR_CPY(size_t, wstrcpy, wchar_t *dest, const wchar_t *src, size_t n);
    STR_CPY(size_t, wstrcpy, wchar_t *dest, const char *src, size_t n);
    STR_CPY(size_t, strcpy, char *dest, const char *src, size_t n);
    STR_CPY(size_t, strcpy, char *dest, const wchar_t *src, size_t n);

    template <size_t _Size> inline size_t wstrcpy(wchar_t (&dest)[_Size], const wchar_t *src) {return wstrcpy(dest, src, _Size);}
    template <size_t _Size> inline size_t wstrcpy(wchar_t (&dest)[_Size], const char *src) {return wstrcpy(dest, src, _Size);}
    template <size_t _Size> inline size_t strcpy(char (&dest)[_Size], const char *src) {return strcpy(dest, src, _Size);}
    template <size_t _Size> inline size_t strcpy(char (&dest)[_Size], const wchar_t *src) {return strcpy(dest, src, _Size);}

    void convert(char* dest, const wchar_t* src, size_t n);
    void convert(wchar_t* dest, const char* src, size_t n);
}
