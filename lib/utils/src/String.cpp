#include "String.h"

#include <windows.h>

void String::convert(char *dest, const wchar_t *src, size_t n) {
    if (!dest || n == 0)
        return;
    dest[0] = '\0';
    if (!src)
        return;

    const int capacity = n > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(n);
    const int result = WideCharToMultiByte(CP_UTF8, 0, src, -1, dest, capacity, nullptr, nullptr);
    if (result == 0)
        dest[0] = '\0';
    else
        dest[n - 1] = '\0';
}

void String::convert(wchar_t *dest, const char *src, size_t n) {
    if (!dest || n == 0)
        return;
    dest[0] = L'\0';
    if (!src)
        return;

    const int capacity = n > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(n);
    const int result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dest, capacity);
    if (result == 0)
        dest[0] = L'\0';
    else
        dest[n - 1] = L'\0';
}
