// tool
#include "gp_config.h"
#include "gp_haptics.h"
#include "gp_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const DWORD kStepMs = 8;      

const char* OnOff(BOOL v) { return v ? "开" : "关"; }

void PrintHeader(const char* title) {
    GpConPrintf("\n==== %s ====\n", title);
    GpConPrintf("  t(ms)  输入LT 输入RT | 出LT 出RT  出L  出R\n");
}

void PrintRow(DWORD t, int ltIn, int rtIn, const GpHapticsOut& o) {
    GpConPrintf("  %5u   %5d  %5d | %4u %4u %4u %4u\n",
           (unsigned)t, ltIn, rtIn,
           (unsigned)o.leftTrigger, (unsigned)o.rightTrigger,
           (unsigned)o.leftMotor, (unsigned)o.rightMotor);
}



void RunLine(const char* title, int isLT, DWORD pressAt, BYTE depth, DWORD holdMs,
             DWORD totalMs, DWORD from, DWORD to, BYTE gameL = 0, BYTE gameR = 0) {
    GpResetHaptics();
    PrintHeader(title);

    DWORD lt = 0, rt = 0;
    unsigned peakLT = 0, peakRT = 0, peakL = 0, peakR = 0;
    DWORD firstNZ = 0, lastNZ = 0;

    for (DWORD t = 0; t <= totalMs; t += kStepMs) {
        BOOL down = (t >= pressAt && t < pressAt + holdMs);
        if (isLT) lt = down ? depth : 0;
        else      rt = down ? depth : 0;

        GpOnPadInput(0, 1000 + t, (BYTE)lt, (BYTE)rt);

        GpHapticsOut o = {0, 0, 0, 0};
        GpTickHaptics(0, 1000 + t, gameL || gameR, gameL, gameR, 0, 0, &o);

        if (o.leftTrigger  > peakLT) peakLT = o.leftTrigger;
        if (o.rightTrigger > peakRT) peakRT = o.rightTrigger;
        if (o.leftMotor    > peakL)  peakL  = o.leftMotor;
        if (o.rightMotor   > peakR)  peakR  = o.rightMotor;

        if (o.leftTrigger + o.rightTrigger + o.leftMotor + o.rightMotor) {
            if (!firstNZ) firstNZ = t;
            lastNZ = t;
        }
        if (t >= from && t <= to) PrintRow(t, (int)lt, (int)rt, o);
    }

    GpConPrintf("  ---- 峰值: 出LT=%u 出RT=%u 出L=%u 出R=%u\n",
           peakLT, peakRT, peakL, peakR);
    if (lastNZ > firstNZ) {
        GpConPrintf("  ---- 持续: %u ms ~ %u ms (共 %u ms, 输入只按了 %u ms)\n",
               (unsigned)firstNZ, (unsigned)lastNZ,
               (unsigned)(lastNZ - firstNZ), (unsigned)holdMs);
    }
    GpConPrintf("\n");
}





void RunRide(void) {
    GpResetHaptics();
    PrintHeader("骑乘辅助（马速 0 -> 12）");
    printf("  t(ms)  马速   出R  出RT   （体感主导 + 轻量同步到扳机）\n");

    GpRdr2State st;
    memset(&st, 0, sizeof(st));
    st.magic      = GPRDR2_MAGIC;
    st.version    = GPRDR2_VERSION;
    st.weaponHash = 0x5B78B8DD;      
    st.weaponGroup = 0x18D5FA97;
    st.armed      = 1;
    st.onMount    = 1;
    st.onFoot     = 0;

    int pulses = 0, lastBody = 0;
    DWORD prevPulse = 0;
    DWORD gaps[32];
    int   gapCount = 0;

    for (DWORD t = 0; t <= 4000; t += kStepMs) {
        float speed = (float)((double)t / 4000.0 * 12.0);
        st.horseSpeed = speed;
        st.tickMs     = 1000 + t;
        GpOnGameState(0, 1000 + t, TRUE, &st);

        GpHapticsOut o = {0, 0, 0, 0};
        GpTickHaptics(0, 1000 + t, FALSE, 0, 0, 0, 0, &o);

        int body = o.rightMotor;
        if (body > 40 && lastBody <= 40) {
            ++pulses;
            if (prevPulse && gapCount < 32) gaps[gapCount++] = t - prevPulse;
            prevPulse = t;
        }
        lastBody = body;

        if ((t % 500) == 0)
            printf("  %5u  %5.1f  %4u  %4u\n", t, speed, o.rightMotor,
                   o.rightTrigger);
    }
    printf("  ---- 4 秒内 %d 次踏地脉冲\n", pulses);
    if (gapCount) {
        printf("  ---- 相邻间隔(ms):");
        for (int i = 0; i < gapCount && i < 12; ++i) printf(" %u", gaps[i]);
        printf("\n");
    }
    printf("\n");
}

}  

int main(int argc, char** argv) {
    GpProxyConfig cfg;
    GpConfigDefaults(&cfg);
    GpConfigLoad(&cfg);

    GpConPrintf("配置文件: %s\n",
           cfg.iniFound ? "已找到(本目录 gpxinput.ini)" : "未找到(用默认值)");
    GpConPrintf("自合成=%s  状态判据=%s  扳机折算到体感=%.2f\n",
           OnOff(cfg.haptics.enable), OnOff(cfg.haptics.useGameState),
           cfg.haptics.trigToBody);
    char toolDir[320];
    GpWideToUtf8(cfg.debugToolDir[0] ? cfg.debugToolDir : L"(游戏目录)",
               toolDir, (int)sizeof(toolDir));
    GpConPrintf("调试模式=%s  窗口(监视=%s 状态=%s)  工具目录=%s\n",
           OnOff(cfg.debugMode), cfg.debugMonitor ? "拉" : "不拉",
           cfg.debugStateView ? "拉" : "不拉", toolDir);
    GpConPrintf("输出通道设置=%d（0=自动 1=XInput 2=HID 3=WinGamingInput）\n", cfg.output);

    if (cfg.haptics.weaponCount > 0) {
        GpConPrintf("武器档 %d 套:\n", cfg.haptics.weaponCount);
        for (int i = 0; i < cfg.haptics.weaponCount; ++i) {
            const GpWeaponProfile& w = cfg.haptics.weapon[i];
            GpConPrintf("  %-9s 组=0x%08X  强度=%.2f 时长=%dms 体感=%.2f\n",
                   w.name, w.hash, w.shotGain, w.shotEnvMs, w.shotBodyKick);
        }
    }

    GpApplyHapticsSettings(cfg.haptics);

    const char* which = (argc > 1) ? argv[1] : "all";

    

    if (which[0] == 'a' || which[0] == 's')
        RunLine("开枪 (RT 扣到 210 保持 60ms)", 0, 100, 210, 60, 700, 90, 480);

    
    if (which[0] == 'a' || which[0] == 'l')
        RunLine("LT 按下 (按到 200 保持 500ms)", 1, 100, 200, 500, 700, 90, 400);

    


    if (which[0] == 'a' || which[0] == 's')
        RunLine("开枪接管（游戏本体 L=R=120 同时在震）", 0, 100, 210, 60, 700, 90, 300, 120, 120);

    
    if (which[0] == 'a' || which[0] == 'r') RunRide();

    return 0;
}
