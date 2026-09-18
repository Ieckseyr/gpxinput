// text
#ifndef GP_TEXT_H
#define GP_TEXT_H

#include <windows.h>



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

#endif 
