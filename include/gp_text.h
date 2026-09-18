// text
#ifndef GP_TEXT_H
#define GP_TEXT_H

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>



static __inline void GpWideToUtf8(const wchar_t* src, char* dst, int dstChars) {
    if (!dst || dstChars <= 0) return;
    dst[0] = 0;
    if (!src || !src[0]) return;

    int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstChars, nullptr, nullptr);
    if (n > 0) {
        dst[dstChars - 1] = 0;   
        return;
    }

    

    n = WideCharToMultiByte(CP_ACP, 0, src, -1, dst, dstChars, nullptr, nullptr);
    if (n > 0) {
        dst[dstChars - 1] = 0;
        return;
    }

    dst[0] = 0;
}














static __inline HANDLE GpConsoleHandle(void) {
    





    static HANDLE cached = INVALID_HANDLE_VALUE;
    static BOOL   tried  = FALSE;
    if (tried) return cached;
    tried = TRUE;

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode)) { cached = h; return cached; }
        cached = nullptr;              
        return cached;
    }

    h = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_EXISTING, 0, nullptr);
    cached = (h == INVALID_HANDLE_VALUE) ? nullptr : h;
    return cached;
}

static __inline void GpConPrintf(const char* fmt, ...) {
    char utf8[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(utf8, sizeof(utf8), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(utf8) - 1) n = (int)sizeof(utf8) - 1;

    HANDLE out = GpConsoleHandle();
    if (out) {
        wchar_t wide[4096];
        int w = MultiByteToWideChar(CP_UTF8, 0, utf8, n, wide, 4096);
        if (w > 0) {
            DWORD written = 0;
            WriteConsoleW(out, wide, (DWORD)w, &written, nullptr);
            return;
        }
    }

    
    fwrite(utf8, 1, (size_t)n, stdout);
    fflush(stdout);
}

#endif 
