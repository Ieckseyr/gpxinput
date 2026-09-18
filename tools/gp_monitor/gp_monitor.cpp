// monitor
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "gp_monitor.h"

namespace {

volatile LONG g_stop = 0;

HANDLE g_out = INVALID_HANDLE_VALUE;   
bool   g_console = false;
int    g_width = 78;
int    g_height = 25;
int    g_written = 0;   

BOOL WINAPI OnCtrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_BREAK_EVENT) {
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    }
    return FALSE;
}









void InitOut(void) {
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    if (g_out != INVALID_HANDLE_VALUE && g_out != nullptr && GetConsoleMode(g_out, &mode)) {
        g_console = true;   
    } else {
        DWORD ft = (g_out == INVALID_HANDLE_VALUE || g_out == nullptr)
                       ? FILE_TYPE_UNKNOWN
                       : GetFileType(g_out);
        if (ft == FILE_TYPE_PIPE || ft == FILE_TYPE_DISK) {
            


            g_console = false;
        } else {
            

            HANDLE h = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
                g_out = h;
                g_console = true;
            } else if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
                g_console = false;
            }
        }
    }

    if (!g_console) return;

    
    CONSOLE_CURSOR_INFO ci;
    if (GetConsoleCursorInfo(g_out, &ci)) {
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(g_out, &ci);
    }
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (GetConsoleScreenBufferInfo(g_out, &bi)) {
        g_width = bi.dwSize.X > 20 ? bi.dwSize.X : 78;
        g_height = bi.srWindow.Bottom - bi.srWindow.Top + 1;
        if (g_height < 5) g_height = 25;
    }
}

void WriteOut(const char* utf8) {
    if (!utf8 || !utf8[0]) return;
    if (!g_console) {
        fputs(utf8, stdout);
        fflush(stdout);
        return;
    }
    
    wchar_t wbuf[16384];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, 16384);
    if (n <= 1) return;
    DWORD written = 0;
    WriteConsoleW(g_out, wbuf, (DWORD)(n - 1), &written, nullptr);
}






void PutLine(const char* text) {
    if (g_console && g_written >= g_height) return;
    char buf[512];
    int len = (int)strlen(text);
    if (len > g_width) len = g_width;
    memcpy(buf, text, (size_t)len);
    int pad = g_width - len;
    if (pad > 0) memset(buf + len, ' ', (size_t)pad);
    buf[len + (pad > 0 ? pad : 0)] = 0;
    WriteOut(buf);
    
    WriteOut("\r\n");
    ++g_written;
}

void CursorHome(void) {
    g_written = 0;
    if (!g_console) return;
    COORD c = {0, 0};
    SetConsoleCursorPosition(g_out, c);
}



void Bar(char* out, int width, unsigned value) {
    int filled = (int)((value * (unsigned)width + 127) / 255);
    if (filled > width) filled = width;
    int i = 0;
    out[i++] = '[';
    for (int k = 0; k < width; ++k) out[i++] = (k < filled) ? '#' : '.';
    out[i++] = ']';
    out[i] = 0;
}




bool ReadController(const GpMonBlock* blk, unsigned idx, GpMonController* out) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const GpMonController* src = &blk->ctl[idx];
        unsigned s0 = src->seq;
        if (s0 & 1u) { Sleep(0); continue; }   
        *out = *src;
        MemoryBarrier();
        if (src->seq == s0) return true;
    }
    return false;
}

bool ReadBlock(const GpMonBlock* view, GpMonBlock* out) {
    if (!view) return false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        memcpy(out, view, sizeof(GpMonBlock));
        if (view->magic == GPMON_MAGIC) return true;
        Sleep(1);
    }
    return false;
}

void Render(const GpMonBlock* blk, DWORD now, bool interactive) {
    char line[512];
    char b1[24], b2[24];

    bool alive = blk->writerHeartbeat != 0 &&
                 (DWORD)(now - blk->writerHeartbeat) < GPMON_HEARTBEAT_TIMEOUT_MS;
    DWORD age = blk->writerHeartbeat ? (DWORD)(now - blk->writerHeartbeat) : 0;

    static const char* kMode[] = {"透传", "改写", "阻断替换"};
    const char* modeName = (blk->mode < 3) ? kMode[blk->mode] : "?";

    

    bool compact = (g_height < 16);

    if (interactive) CursorHome();

    PutLine("============ gpxinput 震动监视 ============");
    

    const char* chName;
    switch (blk->outChannel) {
    case 1:  chName = "WinGamingInput(四电机)"; break;
    case 2:  chName = "HID(四电机)";            break;
    default: chName = "XInput(仅两马达)";       break;
    }
    snprintf(line, sizeof(line), " 模式=%s  通道=%s  HID=%u个  代理=%s  pid=%u",
             modeName, chName, blk->hidCount, alive ? "在线" : "不在线", blk->writerPid);
    PutLine(line);
    snprintf(line, sizeof(line), " 数据年龄=%ums  重绘=%s  窗口=%dx%d",
             age, g_console ? "就地位" : "追加(非控制台)", g_width, g_height);
    PutLine(line);
    if (blk->outChannel == 0) {
        PutLine(" 无四电机通道: 扳机内容折算到体感马达 (TrigToBody)");
    } else {
        PutLine(" 扳机可发: 合成出来的 LT/RT 会直接送到扳机电机");
    }
    PutLine("------------------------------------------");

    unsigned shown = 0;
    for (unsigned c = 0; c < GPMON_MAX_CONTROLLERS; ++c) {
        GpMonController mc;
        if (!ReadController(blk, c, &mc)) continue;

        


        BOOL anyValue = mc.hasGame || mc.outLM || mc.outRM || mc.outLT || mc.outRT ||
                        mc.padLT || mc.padRT || mc.outCount;
        if (!anyValue) continue;

        
        char synth[160] = {0};
        if (mc.shotActive) {
            char t[48];
            snprintf(t, sizeof(t), "开枪[剩%ums] ", mc.shotRemainMs);
            strncat(synth, t, sizeof(synth) - strlen(synth) - 1);
        }
        if (mc.rideActive) {
            char t[64];
            snprintf(t, sizeof(t), "骑乘[周期%ums 幅度%u] ", mc.ridePeriodMs, mc.rideAmp);
            strncat(synth, t, sizeof(synth) - strlen(synth) - 1);
        }
        if (!mc.shotActive && !mc.rideActive) {
            strncat(synth, "无(输出全部来自游戏)", sizeof(synth) - strlen(synth) - 1);
        }

        BOOL outNonZero = mc.outLM || mc.outRM || mc.outLT || mc.outRT;
        BOOL gameFresh = mc.tickMs && (DWORD)(now - mc.tickMs) < 300;
        BOOL stuckWarn = outNonZero && !mc.shotActive && !mc.rideActive && !gameFresh;

        if (compact) {
            snprintf(line, sizeof(line),
                     " #%u 游戏 L%3u/R%3u 输出 L%3u/R%3u 扳机 %u/%u 输入 %u/%u 组=%08X 瞄=%u | %s%s",
                     c, mc.gameLM, mc.gameRM, mc.outLM, mc.outRM,
                     mc.outLT, mc.outRT, mc.padLT, mc.padRT, mc.weaponGroup,
                     (unsigned)mc.aiming, synth,
                     stuckWarn ? " [!]卡震" : "");
            PutLine(line);
            continue;
        }

        

        snprintf(line, sizeof(line), " 手柄 %u   武器组=0x%08X  瞄准=%s  持械=%s  菜单=%s  状态=%s",
                 c, mc.weaponGroup,
                 mc.aiming ? "是" : "否",
                 mc.armed ? "是" : "否",
                 mc.menuActive ? "是" : "否",
                 mc.stateValid ? "在线" : "离线");
        PutLine(line);

        Bar(b1, 12, mc.gameLM);
        Bar(b2, 12, mc.gameRM);
        snprintf(line, sizeof(line), "   游戏原值  L%s%3u   R%s%3u", b1, mc.gameLM, b2, mc.gameRM);
        PutLine(line);

        Bar(b1, 12, mc.outLM);
        Bar(b2, 12, mc.outRM);
        snprintf(line, sizeof(line), "   最终输出  L%s%3u   R%s%3u", b1, mc.outLM, b2, mc.outRM);
        PutLine(line);

        Bar(b1, 12, mc.outLT);
        Bar(b2, 12, mc.outRT);
        snprintf(line, sizeof(line), "   输出扳机  LT%s%3u  RT%s%3u", b1, mc.outLT, b2, mc.outRT);
        PutLine(line);

        Bar(b1, 12, mc.padLT);
        Bar(b2, 12, mc.padRT);
        snprintf(line, sizeof(line), "   手柄输入  LT%s%3u  RT%s%3u", b1, mc.padLT, b2, mc.padRT);
        PutLine(line);

        snprintf(line, sizeof(line), "   合成状态  %s", synth);
        PutLine(line);
        if (stuckWarn) PutLine("   [!] 输出非零但没有依据 —— 疑似卡震");

        ++shown;
    }

    if (shown == 0) PutLine(" （还没有数据：进游戏动一下手柄，或开一枪）");

    PutLine("------------------------------------------");
    snprintf(line, sizeof(line), " 开枪判据: 右扳机扣下 (阈值见 gpxinput.ini)%s",
             interactive ? "   Ctrl+C 退出" : "");
    PutLine(line);
}

}  

int main(int argc, char** argv) {
    bool once = false;
    bool persist = false;
    bool wantHelp = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--once")) once = true;
        else if (!strcmp(argv[i], "--persist")) persist = true;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) wantHelp = true;
    }

    

    InitOut();

    if (wantHelp) {
        WriteOut("gp_monitor —— 四电机实时监视\r\n");
        WriteOut("  gp_monitor            持续刷新（Ctrl+C 退出）\r\n");
        WriteOut("  gp_monitor --persist  游戏退出后不自动关窗\r\n");
        WriteOut("  gp_monitor --once     只打印一次快照（脚本/排障用）\r\n");
        return 0;
    }

    SetConsoleCtrlHandler(OnCtrl, TRUE);

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, GPMON_NAME);
    if (!mapping) {
        WriteOut("没找到监视段。可能的原因：\r\n");
        WriteOut("  1) 游戏还没启动，或代理没加载 —— 代理启动时会创建这一段；\r\n");
        WriteOut("  2) gpxinput.ini 里 MonitorEnable=false；\r\n");
        WriteOut("  3) 代理与监视器版本不匹配（重新编译两者即可）。\r\n");
        return 2;
    }

    

    const GpMonBlock* view = (const GpMonBlock*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        char msg[128];
        snprintf(msg, sizeof(msg), "映射监视段失败 (err=%lu)。\r\n", GetLastError());
        WriteOut(msg);
        CloseHandle(mapping);
        return 4;
    }

    int rc = 0;
    DWORD firstAlive = 0;
    for (;;) {
        GpMonBlock blk;
        if (!ReadBlock(view, &blk)) {
            WriteOut("监视段存在但内容不可读（magic 不对）。代理可能刚启动，稍后重试。\r\n");
            rc = 3;
            if (once) break;
            Sleep(500);
            continue;
        }

        Render(&blk, GetTickCount(), !once);
        if (once) break;

        if (InterlockedCompareExchange(&g_stop, 1, 1) == 1) break;

        

        if (!persist) {
            DWORD now2 = GetTickCount();
            bool alive = blk.writerHeartbeat != 0 &&
                         (DWORD)(now2 - blk.writerHeartbeat) < GPMON_HEARTBEAT_TIMEOUT_MS;
            if (alive) {
                firstAlive = now2;
            } else if (firstAlive != 0 && (DWORD)(now2 - firstAlive) > 10000) {
                WriteOut("\r\n代理已退出（心跳停止超过 10 秒），监视窗口自动关闭。"
                         "要它一直留着请加 --persist。\r\n");
                break;
            }
        }

        Sleep(50);   
    }

    UnmapViewOfFile((LPCVOID)view);
    CloseHandle(mapping);
    if (g_console) {
        CONSOLE_CURSOR_INFO ci;
        if (GetConsoleCursorInfo(g_out, &ci)) {
            ci.bVisible = TRUE;
            SetConsoleCursorInfo(g_out, &ci);
        }
    }
    return rc;
}
