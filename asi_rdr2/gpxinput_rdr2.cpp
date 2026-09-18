// asi
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "sh_rdr2.h"
#include "rdr2_natives.h"
#include "gp_rdr2_state.h"

namespace {

HANDLE      g_mapping = nullptr;
GpRdr2State* g_state  = nullptr;
uint32_t    g_frame   = 0;
bool        g_disabled = false;
char        g_logPath[MAX_PATH] = {0};


void Log(const char* fmt, ...) {
    if (!g_logPath[0]) {
        
        GetModuleFileNameA(nullptr, g_logPath, MAX_PATH);
        char* slash = strrchr(g_logPath, '\\');
        if (slash) slash[1] = 0;
        strncat_s(g_logPath, MAX_PATH, "gpxinput_rdr2.log", _TRUNCATE);
    }
    FILE* f = fopen(g_logPath, "a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

bool OpenState(void) {
    HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, GPRDR2_NAME);
    bool created = false;
    if (!h) {
        h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                               sizeof(GpRdr2State), GPRDR2_NAME);
        if (h && GetLastError() != ERROR_ALREADY_EXISTS) created = true;
    }
    if (!h) {
        Log("创建/打开状态段失败 (err=%lu)", GetLastError());
        return false;
    }
    void* p = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (!p) {
        Log("映射状态段失败 (err=%lu)", GetLastError());
        CloseHandle(h);
        return false;
    }
    g_mapping = h;
    g_state = (GpRdr2State*)p;

    if (created) {
        memset(g_state, 0, sizeof(GpRdr2State));
        g_state->writerPid = GetCurrentProcessId();
        g_state->seq = 0;
    } else {
        Log("状态段已存在，接手写入（pid=%u）", g_state->writerPid);
    }
    Log("状态段就绪（%s）", created ? "本进程创建" : "接手已有");
    return true;
}



uint32_t CurrentWeapon(int ped) {
    uint32_t hash = 0;
    sh::nativeInit(N_GET_CURRENT_PED_WEAPON);
    sh::nativePush64((uint64_t)(int64_t)ped);
    sh::nativePush64((uint64_t)(uintptr_t)&hash);
    sh::nativePush64((uint64_t)0);
    sh::nativeCall();
    return hash;
}

void Tick(void) {
    if (g_disabled) {
        sh::scriptWait(500);
        return;
    }

    int ped = (int)rdr2_call0(N_PLAYER_PED_ID);

    

    if (ped == 0) {
        sh::scriptWait(0);
        return;
    }

    uint32_t weapon    = CurrentWeapon(ped);
    uint32_t group     = (uint32_t)rdr2_call1(N_GET_WEAPONTYPE_GROUP, weapon);
    int      ammo      = (int)rdr2_call2(N_GET_AMMO_IN_CLIP, (uint64_t)(int64_t)ped, weapon);
    uint32_t sinceShot = (uint32_t)rdr2_call1(N_TIME_SINCE_PED_LAST_SHOT, (uint64_t)(int64_t)ped);
    int      mount     = (int)rdr2_call1(N_GET_MOUNT, (uint64_t)(int64_t)ped);

    float horseSpeed = 0.0f;
    if (mount != 0) {
        
        uint64_t bits = rdr2_call1(N_GET_ENTITY_SPEED, (uint64_t)(int64_t)mount);
        float v;
        memcpy(&v, &bits, sizeof(v));
        if (v == v && v < 1000.0f) horseSpeed = v;   
    }

    
    g_state->seq++;                 
    MemoryBarrier();

    g_state->tickMs        = GetTickCount();
    g_state->frame         = ++g_frame;
    g_state->writerPid     = GetCurrentProcessId();

    g_state->weaponHash    = weapon;
    g_state->weaponGroup   = group;
    g_state->ammoInClip    = ammo;
    g_state->timeSinceShot = sinceShot;

    g_state->shooting  = (uint8_t)(rdr2_call1(N_IS_PED_SHOOTING, (uint64_t)(int64_t)ped) != 0);
    

    g_state->aiming    = (uint8_t)(rdr2_call1(N_IS_PLAYER_FREE_AIMING, 0) != 0);
    g_state->reloading = (uint8_t)(rdr2_call1(N_IS_PED_RELOADING, (uint64_t)(int64_t)ped) != 0);
    g_state->onFoot    = (uint8_t)(rdr2_call1(N_IS_PED_ON_FOOT, (uint64_t)(int64_t)ped) != 0);
    g_state->onMount   = (uint8_t)(mount != 0);
    g_state->inVehicle = (uint8_t)(rdr2_call2(N_IS_PED_IN_ANY_VEHICLE, (uint64_t)(int64_t)ped, 0) != 0);

    g_state->horseSpeed  = horseSpeed;
    g_state->playerSpeed = 0.0f;
    g_state->mountHash   = 0;
    g_state->playerPed   = (uint32_t)ped;

    MemoryBarrier();
    g_state->seq++;                 

    sh::scriptWait(0);                  
}

}  


extern "C" __declspec(dllexport) void ScriptMain(void) {
    

    if (!sh::Resolve()) {
        Log("解析 ScriptHookRDR2 入口失败（脚本钩子不在？），脚本自禁用");
        return;
    }
    Log("ScriptHookRDR2 入口解析成功");

    if (!OpenState()) {
        g_disabled = true;
        Log("状态段不可用，脚本自禁用（代理会退回只看扳机）");
        return;
    }
    Log("gpxinput_rdr2 启动：每帧发布游戏状态");

    

    for (int i = 0; i < 600; ++i) {
        int ped = (int)rdr2_call0(N_PLAYER_PED_ID);
        if (ped != 0) {
            Log("自检通过：playerPed=%d", ped);
            break;
        }
        if (i == 599) {
            Log("自检失败：拿不到 playerPed，脚本自禁用");
            g_disabled = true;
            return;
        }
        sh::scriptWait(16);
    }

    for (;;) Tick();
}
