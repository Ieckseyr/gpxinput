













#include "sh_rdr2.h"

#include <stdio.h>
#include "gp_text.h"

namespace {

int Check(HMODULE h, const char* prefix, const char* plain, const char* what) {
    const char* real = sh::FindExportByPrefix(h, prefix);
    void* p = sh::Take(h, prefix, plain);
    GpConPrintf("  %-22s %-30s -> %s\n", what, real ? real : "(按前缀没找到)", p ? "取到" : "失败");
    return p ? 0 : 1;
}

}  

int main(int argc, char** argv) {
    if (argc < 2) {
        GpConPrintf("用法: shresolve_test.exe <ScriptHookRDR2.dll>\n");
        return 2;
    }

    wchar_t path[MAX_PATH] = {0};
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);

    
    HMODULE h = LoadLibraryExW(path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!h) {
        GpConPrintf("[失败] 打不开 %s (err=%lu)\n", argv[1], GetLastError());
        return 2;
    }
    GpConPrintf("已映射 %s  base=%p\n\n", argv[1], (void*)h);

    int bad = 0;
    bad += Check(h, "?scriptRegister@@",   "scriptRegister",   "scriptRegister");
    bad += Check(h, "?scriptUnregister@@YAXPEAUHINSTANCE__@@", "scriptUnregister", "scriptUnregister");
    bad += Check(h, "?scriptWait@@",       "scriptWait",       "scriptWait");
    bad += Check(h, "?nativeInit@@",       "nativeInit",       "nativeInit");
    bad += Check(h, "?nativePush64@@",     "nativePush64",     "nativePush64");
    bad += Check(h, "?nativeCall@@",       "nativeCall",       "nativeCall");
    bad += Check(h, "?getGlobalPtr@@",     "getGlobalPtr",     "getGlobalPtr");

    GpConPrintf("\n%s（%d 项失败）\n", bad ? "[失败] 有入口取不到" : "[通过] 全部入口都能取到", bad);
    FreeLibrary(h);
    return bad ? 1 : 0;
}
