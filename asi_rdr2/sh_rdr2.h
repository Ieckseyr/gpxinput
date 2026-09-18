// sdk
#ifndef SH_RDR2_H
#define SH_RDR2_H

#include <windows.h>
#include <stdint.h>

namespace sh {

typedef void      (*FnScriptRegister)(HMODULE module, void (*fn)(void));
typedef void      (*FnScriptUnregister)(HMODULE module);
typedef void      (*FnScriptWait)(unsigned long waitTime);
typedef void      (*FnNativeInit)(uint64_t hash);
typedef void      (*FnNativePush64)(uint64_t value);
typedef uint64_t* (*FnNativeCall)(void);
typedef uint64_t* (*FnGetGlobalPtr)(int globalId);

inline FnScriptRegister  scriptRegister  = nullptr;
inline FnScriptUnregister scriptUnregister = nullptr;
inline FnScriptWait      scriptWait      = nullptr;
inline FnNativeInit      nativeInit      = nullptr;
inline FnNativePush64    nativePush64    = nullptr;
inline FnNativeCall      nativeCall      = nullptr;
inline FnGetGlobalPtr    getGlobalPtr    = nullptr;


inline bool Resolve(void) {
    HMODULE h = GetModuleHandleW(L"ScriptHookRDR2.dll");
    if (!h) return false;

    scriptRegister   = (FnScriptRegister)  GetProcAddress(h, "scriptRegister");
    scriptUnregister = (FnScriptUnregister)GetProcAddress(h, "scriptUnregister");
    scriptWait       = (FnScriptWait)      GetProcAddress(h, "scriptWait");
    nativeInit       = (FnNativeInit)      GetProcAddress(h, "nativeInit");
    nativePush64     = (FnNativePush64)    GetProcAddress(h, "nativePush64");
    nativeCall       = (FnNativeCall)      GetProcAddress(h, "nativeCall");
    getGlobalPtr     = (FnGetGlobalPtr)    GetProcAddress(h, "getGlobalPtr");

    return scriptWait && nativeInit && nativePush64 && nativeCall;
}

}  







inline uint64_t rdr2_call0(uint64_t hash) {
    sh::nativeInit(hash);
    uint64_t* r = sh::nativeCall();
    return r ? *r : 0;
}

inline uint64_t rdr2_call1(uint64_t hash, uint64_t a1) {
    sh::nativeInit(hash);
    sh::nativePush64(a1);
    uint64_t* r = sh::nativeCall();
    return r ? *r : 0;
}

inline uint64_t rdr2_call2(uint64_t hash, uint64_t a1, uint64_t a2) {
    sh::nativeInit(hash);
    sh::nativePush64(a1);
    sh::nativePush64(a2);
    uint64_t* r = sh::nativeCall();
    return r ? *r : 0;
}

inline uint64_t rdr2_call3(uint64_t hash, uint64_t a1, uint64_t a2, uint64_t a3) {
    sh::nativeInit(hash);
    sh::nativePush64(a1);
    sh::nativePush64(a2);
    sh::nativePush64(a3);
    uint64_t* r = sh::nativeCall();
    return r ? *r : 0;
}

#endif 
