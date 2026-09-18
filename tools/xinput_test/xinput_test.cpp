// tool
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vector>
#include "gp_text.h"

typedef DWORD (WINAPI *FnGetState)(DWORD, void*);
typedef DWORD (WINAPI *FnSetState)(DWORD, void*);
typedef DWORD (WINAPI *FnGetCapabilities)(DWORD, DWORD, void*);

typedef struct { WORD wButtons; BYTE bLT, bRT; SHORT lx, ly, rx, ry; } GamepadRaw;
typedef struct { DWORD packet; GamepadRaw pad; } StateRaw;
typedef struct { WORD wLeftMotorSpeed, wRightMotorSpeed; } VibRaw;

static const DWORD kErrorDeviceNotConnected = 1167;




static unsigned int g_rng = 0x12345678u;


HMODULE LoadXInputLikeAGame(const wchar_t* name, wchar_t* resolved, DWORD resolvedChars) {
    HMODULE h = LoadLibraryW(name);
    if (h && resolved) {
        GetModuleFileNameW(h, resolved, resolvedChars);
    }
    return h;
}




static const char* W(const wchar_t* s) {
    const int kSlots = 4;
    const int kSize = 1024;
    static thread_local char bufs[kSlots][kSize];
    static thread_local int next = 0;
    char* b = bufs[next];
    next = (next + 1) % kSlots;
    GpWideToUtf8(s, b, kSize);
    return b;
}





struct ReplayRow {
    DWORD tick;
    BYTE  l, r;
};






static int RunReplay(const wchar_t* path, int loops, FnSetState setState) {
    FILE* f = _wfopen(path, L"rb");
    if (!f) {
        printf("[失败] 打不开录制文件。\n");
        return 1;
    }

    std::vector<ReplayRow> rows;
    char line[1024];
    bool headerSkipped = false;

    while (fgets(line, sizeof(line), f)) {
        

        if (!headerSkipped) {
            headerSkipped = true;
            if (strstr(line, "seq,") || strstr(line, "tick_ms")) continue;
        }

        unsigned long long seq;
        unsigned long tick;
        unsigned c, sr, fl, cn, il, ir, ilt, irt, ol, orr, olt, ort;
        int got = sscanf(line, "%llu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                         &seq, &tick, &c, &sr, &fl, &cn,
                         &il, &ir, &ilt, &irt, &ol, &orr, &olt, &ort);
        if (got != 14) continue;   

        ReplayRow r;
        r.tick = (DWORD)tick;
        r.l = (BYTE)il;
        r.r = (BYTE)ir;
        rows.push_back(r);
    }
    fclose(f);

    if (rows.empty()) {
        printf("[失败] 文件里没有可回放的帧。确认它是 gp_processor --record 生成的 CSV。\n");
        return 1;
    }

    DWORD span = rows.back().tick - rows.front().tick;
    printf("读到 %d 帧，原始时长 %.1f 秒\n", (int)rows.size(), span / 1000.0);

    

    {
        BYTE maxL = 0, maxR = 0;
        long sumL = 0, sumR = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].l > maxL) maxL = rows[i].l;
            if (rows[i].r > maxR) maxR = rows[i].r;
            sumL += rows[i].l;
            sumR += rows[i].r;
        }
        printf("  数据概览: 左马达 峰值=%u 均值=%ld   右马达 峰值=%u 均值=%ld\n",
               maxL, sumL / (long)rows.size(), maxR, sumR / (long)rows.size());
        if (maxL == 0 && maxR == 0) {
            printf("  注意: 录到的震动全是 0。多半是录制时游戏里没触发震动，\n");
            printf("        或者当时代理是透传模式、根本没在发数据。\n");
        }
        printf("\n回放中... 按 Ctrl+C 中断\n\n");
    }

    int loop = 0;
    for (;;) {
        DWORD base = GetTickCount();
        DWORD rec0 = rows.front().tick;

        for (size_t i = 0; i < rows.size(); ++i) {
            


            DWORD target = rows[i].tick - rec0;
            DWORD elapsed = GetTickCount() - base;
            if (target > elapsed) {
                DWORD wait = target - elapsed;
                Sleep(wait > 100 ? 100 : wait);
            }

            VibRaw v;
            v.wLeftMotorSpeed  = (WORD)(rows[i].l * 257);
            v.wRightMotorSpeed = (WORD)(rows[i].r * 257);
            setState(0, &v);
        }

        ++loop;
        if (loops > 0 && loop >= loops) break;
        printf("  第 %d 轮回放完成\n", loop);
    }

    
    {
        VibRaw z;
        z.wLeftMotorSpeed = 0;
        z.wRightMotorSpeed = 0;
        setState(0, &z);
    }
    printf("\n[完成] 回放 %d 轮\n", loop);
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);

    
    const wchar_t* replayPath = nullptr;
    int replayLoops = 0;   

    for (int i = 1; i < argc; ++i) {
        if (!_wcsicmp(argv[i], L"--replay") && i + 1 < argc) {
            replayPath = argv[++i];
        } else if (!_wcsicmp(argv[i], L"--loops") && i + 1 < argc) {
            replayLoops = _wtoi(argv[++i]);
        } else if (!_wcsicmp(argv[i], L"--help") || !_wcsicmp(argv[i], L"-h")) {
            printf("xinput_test —— 模拟游戏进程\n\n");
            printf("用法:\n");
            printf("  xinput_test.exe [秒数] [模式]        发合成波形\n");
            printf("     模式: ramp(默认) / square / full / driving\n");
            printf("       driving = 模拟赛车游戏的四层复合信号，调手感用这个\n");
            printf("  xinput_test.exe --replay <录制.csv>   回放真实游戏录制的数据\n");
            printf("  xinput_test.exe --replay <文件> --loops 3    只回放 3 轮\n\n");
            printf("回放用的 CSV 由 gp_processor --record <文件> 生成。\n");
            return 0;
        }
    }

    int durationSec = 5;
    const wchar_t* mode = L"ramp";
    if (!replayPath) {
        durationSec = (argc > 1 && argv[1][0] != L'-') ? _wtoi(argv[1]) : 5;
        if (durationSec <= 0) durationSec = 5;
        if (argc > 2 && argv[2][0] != L'-') mode = argv[2];
    }

    printf("=== xinput_test —— 手柄震动反代端到端自测 ===\n\n");
    printf("宿主进程: pid=%lu  %s\n", GetCurrentProcessId(),
           sizeof(void*) == 8 ? "x64" : "x86");
    if (replayPath) {
        printf("回放文件: %s\n\n", W(replayPath));
    } else {
        printf("震动模式: %s   持续: %d 秒\n\n", W(mode), durationSec);
    }

    
    wchar_t resolved[MAX_PATH] = {0};
    HMODULE hx = LoadXInputLikeAGame(L"xinput1_4.dll", resolved, MAX_PATH);
    const wchar_t* dllName = L"xinput1_4.dll";
    if (!hx) {
        hx = LoadXInputLikeAGame(L"xinput1_3.dll", resolved, MAX_PATH);
        dllName = L"xinput1_3.dll";
    }
    if (!hx) {
        hx = LoadXInputLikeAGame(L"xinput9_1_0.dll", resolved, MAX_PATH);
        dllName = L"xinput9_1_0.dll";
    }
    if (!hx) {
        printf("[失败] 三个 xinput DLL 都加载不了，err=%lu\n", GetLastError());
        return 1;
    }

    printf("[1] 实际加载到: %s\n", W(resolved));
    printf("    模块名: %s\n", W(dllName));

    
    {
        wchar_t selfDir[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, selfDir, MAX_PATH);
        wchar_t* slash = wcsrchr(selfDir, L'\\');
        if (slash) *(slash + 1) = 0;
        if (_wcsnicmp(resolved, selfDir, wcslen(selfDir)) == 0) {
            printf("    => 加载的是同目录下的 DLL，代理劫持生效\n");
        } else {
            printf("    => 加载的是系统目录的 DLL。代理没生效（DLL 不在本目录？）\n");
        }
    }

    
    {
        struct { const char* name; WORD ord; } probes[] = {
            {"XInputGetState", 0}, {"XInputSetState", 0},
            {"XInputGetCapabilities", 0}, {"XInputEnable", 0},
            {"XInputGetBatteryInformation", 0}, {"XInputGetKeystroke", 0},
            {"XInputGetAudioDeviceIds", 0},
            {"XInputGetStateEx(ord 100)", 100},
        };
        int missing = 0;
        printf("\n[2] 导出解析:\n");
        for (int i = 0; i < (int)(sizeof(probes) / sizeof(probes[0])); ++i) {
            void* p = probes[i].ord
                    ? (void*)GetProcAddress(hx, MAKEINTRESOURCEA(probes[i].ord))
                    : (void*)GetProcAddress(hx, probes[i].name);
            printf("    %-32s %s\n", probes[i].name, p ? "OK" : "缺失");
            if (!p) ++missing;
        }
        if (missing) {
            printf("    警告: 有 %d 个导出解析失败，真实游戏可能会因此启动失败\n", missing);
        }
    }

    FnGetState getState = (FnGetState)GetProcAddress(hx, "XInputGetState");
    FnSetState setState = (FnSetState)GetProcAddress(hx, "XInputSetState");
    if (!getState || !setState) {
        printf("\n[失败] 缺 XInputGetState / XInputSetState，无法继续\n");
        return 1;
    }

    
    {
        printf("\n[3] 转发验证:\n");
        int connected = 0;
        for (DWORD c = 0; c < 4; ++c) {
            StateRaw st;
            memset(&st, 0, sizeof(st));
            DWORD rc = getState(c, &st);
            if (rc == ERROR_SUCCESS) {
                ++connected;
                printf("    控制%d 已连接: 包号=%lu 左扳机=%u 右扳机=%u "
                       "摇杆=(%d,%d,%d,%d)\n",
                       c, st.packet, st.pad.bLT, st.pad.bRT,
                       st.pad.lx, st.pad.ly, st.pad.rx, st.pad.ry);
            } else if (rc == kErrorDeviceNotConnected) {
                printf("    控制%d 未连接 (1167)\n", c);
            } else {
                printf("    控制%d 返回了意外的错误码 %lu —— 转发可能有问题\n", c, rc);
            }
        }
        if (connected == 0) {
            printf("    注意: 当前没有手柄连接。震动调用仍会执行，加工端也仍应看到帧，\n");
            printf("          只是最后一步「真的让手柄动起来」无法验证。\n");
        }
    }

    
    if (replayPath) {
        return RunReplay(replayPath, replayLoops, setState);
    }

    
    printf("\n[4] 开始发震动 %d 秒...\n", durationSec);

    int ticks = 0;
    DWORD start = GetTickCount();
    DWORD lastPrint = start;

    while ((DWORD)(GetTickCount() - start) < (DWORD)(durationSec * 1000)) {
        DWORD elapsed = GetTickCount() - start;

        BYTE l = 0, r = 0;
        if (!_wcsicmp(mode, L"full")) {
            l = 255; r = 255;
        } else if (!_wcsicmp(mode, L"driving")) {
            













            DWORD t = elapsed % 20000;

            
            int idle = 24 + (int)(18.0 * ((t % 100) < 50 ? 1.0 : 0.35));

            
            g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
            int road = 40 + (int)((g_rng & 0x3F));

            
            int shift = 0;
            DWORD phase3 = t % 3000;
            if (phase3 < 150) shift = 150 - (int)(phase3);          
            if (phase3 >= 150 && phase3 < 300) shift = 0;

            
            int brake = 0;
            DWORD phase5 = t % 5000;
            if (phase5 < 800) {
                
                brake = ((phase5 / 40) % 2 == 0) ? 170 : 0;
            }

            int lv = idle + shift;
            int rv = road + brake;
            if (lv > 255) lv = 255;
            if (rv > 255) rv = 255;
            l = (BYTE)lv;
            r = (BYTE)rv;
        } else if (!_wcsicmp(mode, L"square")) {
            
            bool on = ((elapsed / 1000) % 2) == 0;
            l = on ? 200 : 0;
            r = on ? 0 : 200;
        } else {
            
            DWORD phase = elapsed % 4000;
            int v = (phase < 2000) ? (int)(phase * 255 / 2000)
                                   : (int)((4000 - phase) * 255 / 2000);
            l = (BYTE)v;
            r = (BYTE)(255 - v);
        }

        VibRaw vib;
        vib.wLeftMotorSpeed  = (WORD)(l * 257);
        vib.wRightMotorSpeed = (WORD)(r * 257);
        setState(0, &vib);
        ++ticks;

        if ((DWORD)(GetTickCount() - lastPrint) >= 1000) {
            lastPrint = GetTickCount();
            printf("    t=%4lums  发出 左=%3u 右=%3u   累计 %d 次调用\n",
                   elapsed, l, r, ticks);
        }

        Sleep(16);   
    }

    printf("\n[完成] 共发出 %d 次 XInputSetState，耗时 %lu ms\n",
           ticks, GetTickCount() - start);
    printf("\n如果加工端日志里的「已处理」不为 0，说明整条链路是通的：\n");
    printf("  游戏 -> 代理捕获 -> 共享内存 -> 加工端 -> 回写 -> 代理输出\n");

    return 0;
}
