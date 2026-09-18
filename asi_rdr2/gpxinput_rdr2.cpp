// asi
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "sh_rdr2.h"
#include "rdr2_natives.h"
#include "gp_rdr2_state.h"

namespace {

HMODULE       g_self       = nullptr;
HANDLE        g_mapping    = nullptr;
GpRdr2State*  g_state      = nullptr;
uint32_t      g_frame      = 0;
volatile LONG g_registered = 0;      
volatile LONG g_disabled   = 0;
BOOL          g_checked    = FALSE;  
int           g_bootFrames = 0;
char          g_logPath[MAX_PATH] = {0};

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
    if (g_state) return true;

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
        g_state->magic   = GPRDR2_MAGIC;
        g_state->version = GPRDR2_VERSION;
        g_state->writerPid = GetCurrentProcessId();
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
    if (InterlockedCompareExchange(&g_disabled, 1, 1) == 1) {
        sh::scriptWait(500);
        return;
    }

    int ped = (int)rdr2_call0(N_PLAYER_PED_ID);

    

    if (!g_checked && ped != 0) {
        g_checked = TRUE;
        Log("自检通过：playerPed=%d，开始发布游戏状态", ped);
    }

    

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

    g_state->magic         = GPRDR2_MAGIC;
    g_state->version       = GPRDR2_VERSION;
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
    g_state->menuActive = (uint8_t)(rdr2_call0(N_IS_PAUSE_MENU_ACTIVE) != 0);
    g_state->armed      = (uint8_t)(rdr2_call2(N_IS_PED_ARMED, (uint64_t)(int64_t)ped, 1) != 0);

    g_state->horseSpeed  = horseSpeed;
    g_state->playerSpeed = 0.0f;
    g_state->mountHash   = 0;
    g_state->playerPed   = (uint32_t)ped;

    MemoryBarrier();
    g_state->seq++;                 

    ++g_bootFrames;
    if (g_bootFrames == 120) {
        
        Log("状态样本：武器=0x%08X 组=0x%08X 弹匣=%d 瞄准=%u 持械=%u 菜单=%u 骑马=%u",
            weapon, group, ammo, g_state->aiming, g_state->armed,
            g_state->menuActive, g_state->onMount);
    }

    sh::scriptWait(0);              
}





DWORD WINAPI BootThread(LPVOID) {
    


    for (int i = 0; i < 3000; ++i) {         
        if (sh::Resolve()) {
            if (InterlockedCompareExchange(&g_registered, 1, 0) != 0) return 0;
            OpenState();
            sh::scriptRegister(g_self, Tick);
            Log("已向 ScriptHookRDR2 注册脚本（等了 %d ms）", i * 100);
            return 0;
        }
        if (i > 0 && (i % 100) == 0)
            Log("等待 ScriptHookRDR2... 已等 %d 秒（%s）", i / 10, sh::Report());
        Sleep(100);
    }
    Log("等待 ScriptHookRDR2 超时（5 分钟）—— 脚本未注册，代理会退回只看扳机（%s）",
        sh::Report());
    return 0;
}

}  



extern "C" __declspec(dllexport) void ScriptMain(void) {
    if (!sh::Resolve()) {
        Log("ScriptMain 被调用，但解析 ScriptHookRDR2 入口失败");
        return;
    }
    if (InterlockedCompareExchange(&g_registered, 1, 0) != 0) return;
    OpenState();
    sh::scriptRegister(g_self, Tick);
    Log("ScriptMain 路径注册成功");
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        
        HANDLE t = CreateThread(nullptr, 0, BootThread, nullptr, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
