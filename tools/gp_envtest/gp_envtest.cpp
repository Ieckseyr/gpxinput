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
    GpConPrintf("  每半秒一段马速，下面给出该段的峰值（体感主导 + 轻量同步到扳机）\n");

    GpRdr2State st;
    memset(&st, 0, sizeof(st));
    st.magic      = GPRDR2_MAGIC;
    st.version    = GPRDR2_VERSION;
    st.weaponHash = 0x5B78B8DD;      
    st.weaponGroup = 0x18D5FA97;
    st.armed      = 1;
    st.onMount    = 1;
    st.onFoot     = 0;

    int pulses = 0, lastBody = 0, winPeak = 0, winTrigPeak = 0;
    float prevSpeed = 0.0f;
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
        if (body > 12 && lastBody <= 12) {
            ++pulses;
            if (prevPulse && gapCount < 32) gaps[gapCount++] = t - prevPulse;
            prevPulse = t;
        }
        lastBody = body;
        prevSpeed = speed;

        if (body > winPeak) winPeak = body;
        if (o.rightTrigger > winTrigPeak) winTrigPeak = o.rightTrigger;
        

        if ((t % 500) == 0 && t) {
            GpConPrintf("  马速 %5.1f  ->  体感峰值 %3d   扳机峰值 %3d\n",
                        prevSpeed, winPeak, winTrigPeak);
            winPeak = 0;
            winTrigPeak = 0;
        }
    }
    GpConPrintf("  ---- 4 秒内 %d 次踏地脉冲\n", pulses);
    if (gapCount) {
        GpConPrintf("  ---- 相邻间隔(ms):");
        for (int i = 0; i < gapCount && i < 12; ++i) GpConPrintf(" %u", gaps[i]);
        GpConPrintf("\n");
    }
    GpConPrintf("\n");
}





void RunAim(void) {
    GpResetHaptics();
    GpConPrintf("\n==== 瞄准（手枪，LT 按住 3 秒）====\n");
    GpConPrintf("  t(ms)   出LT  出RT   出L   出R   （期望：扳机稳定 ~15，握把低频起伏）\n");

    GpRdr2State st;
    memset(&st, 0, sizeof(st));
    st.magic       = GPRDR2_MAGIC;
    st.version     = GPRDR2_VERSION;
    st.weaponHash  = 0x5B78B8DD;
    st.weaponGroup = 0x18D5FA97;     
    st.armed       = 1;
    st.aiming      = 1;              
    st.onFoot      = 1;

    int ltMin = 999, ltMax = 0, bodyMin = 999, bodyMax = 0;
    for (DWORD t = 0; t <= 3000; t += kStepMs) {
        st.tickMs = 1000 + t;
        GpOnPadInput(0, 1000 + t, 200, 0);          
        GpOnGameState(0, 1000 + t, TRUE, &st);

        GpHapticsOut o = {0, 0, 0, 0};
        GpTickHaptics(0, 1000 + t, FALSE, 0, 0, 0, 0, &o);

        if ((int)o.leftTrigger < ltMin) ltMin = o.leftTrigger;
        if ((int)o.leftTrigger > ltMax) ltMax = o.leftTrigger;
        if ((int)o.leftMotor   < bodyMin) bodyMin = o.leftMotor;
        if ((int)o.leftMotor   > bodyMax) bodyMax = o.leftMotor;

        if ((t % 250) == 0)
            GpConPrintf("  %5u   %4u  %4u  %4u  %4u\n", t, o.leftTrigger,
                        o.rightTrigger, o.leftMotor, o.rightMotor);
    }
    GpConPrintf("  ---- 扳机范围 %d~%d（越窄越「稳」），握把范围 %d~%d（应当有起伏）\n\n",
                ltMin, ltMax, bodyMin, bodyMax);
}


void RunBow(void) {
    GpResetHaptics();
    GpConPrintf("\n==== 拉弓（弓，RT 按住 2 秒）====\n");
    GpConPrintf("  t(ms)   出LT  出RT   出L   出R   （瞄准标志=false，仍应两侧爬升）\n");

    GpRdr2State st;
    memset(&st, 0, sizeof(st));
    st.magic       = GPRDR2_MAGIC;
    st.version     = GPRDR2_VERSION;
    st.weaponHash  = 0x88A8505C;              
    st.weaponGroup = GPRDR2_GRP_BOW;
    st.armed       = 1;
    st.aiming      = 0;   
    st.onFoot      = 1;

    int peakLT = 0, peakRT = 0;
    for (DWORD t = 0; t <= 2000; t += kStepMs) {
        st.tickMs = 1000 + t;
        GpOnPadInput(0, 1000 + t, 200, 255);   
        GpOnGameState(0, 1000 + t, TRUE, &st);

        GpHapticsOut o = {0, 0, 0, 0};
        GpTickHaptics(0, 1000 + t, FALSE, 0, 0, 0, 0, &o);

        if ((int)o.leftTrigger  > peakLT) peakLT = o.leftTrigger;
        if ((int)o.rightTrigger > peakRT) peakRT = o.rightTrigger;
        if ((t % 250) == 0)
            GpConPrintf("  %5u   %4u  %4u  %4u  %4u\n", t, o.leftTrigger,
                        o.rightTrigger, o.leftMotor, o.rightMotor);
    }
    GpConPrintf("  ---- 两侧峰值 LT=%d RT=%d（应当接近相等且随时间爬升）\n\n",
                peakLT, peakRT);
}



void RunGunShot(void) {
    GpResetHaptics();
    GpConPrintf("\n==== 开枪（状态驱动：IS_PED_SHOOTING 上升沿，M1899 手枪档）====\n");
    GpConPrintf("  t(ms)   出LT  出RT   出L   出R   （期望：RT 主导，握把跟随）\n");

    GpRdr2State st;
    memset(&st, 0, sizeof(st));
    st.magic       = GPRDR2_MAGIC;
    st.version     = GPRDR2_VERSION;
    st.weaponHash  = 0x5B78B8DD;      
    st.weaponGroup = GPRDR2_GRP_PISTOL;
    st.armed       = 1;
    st.onFoot      = 1;
    st.ammoInClip  = 7;

    int pLT=0,pRT=0,pL=0,pR=0;
    for (DWORD t = 0; t <= 400; t += kStepMs) {
        if (t == 200) st.shooting = 1;        
        if (t == 224) st.shooting = 0;        
        st.tickMs = 1000 + t;
        GpOnPadInput(0, 1000 + t, 0, 255);    
        GpOnGameState(0, 1000 + t, TRUE, &st);

        GpHapticsOut o = {0, 0, 0, 0};
        GpTickHaptics(0, 1000 + t, FALSE, 0, 0, 0, 0, &o);

        if ((int)o.leftTrigger  > pLT) pLT = o.leftTrigger;
        if ((int)o.rightTrigger > pRT) pRT = o.rightTrigger;
        if ((int)o.leftMotor    > pL)  pL  = o.leftMotor;
        if ((int)o.rightMotor   > pR)  pR  = o.rightMotor;

        if (t >= 190 && t <= 300)
            GpConPrintf("  %5u   %4u  %4u  %4u  %4u\n", t, o.leftTrigger,
                        o.rightTrigger, o.leftMotor, o.rightMotor);
    }
    GpConPrintf("  ---- 峰值 LT=%d RT=%d L=%d R=%d\n\n", pLT, pRT, pL, pR);
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

    if (cfg.haptics.gunCount > 0) {
        GpConPrintf("具体枪械档 %d 套（优先于武器组）：\n", cfg.haptics.gunCount);
        for (int i = 0; i < cfg.haptics.gunCount && i < 6; ++i) {
            const GpWeaponProfile& g = cfg.haptics.weapon[i];
            const GpWeaponProfile& gg = cfg.haptics.gun[i];
            GpConPrintf("  %-14s 0x%08X  强度=%.2f 时长=%dms 体感=%.2f\n",
                        gg.name, gg.hash, gg.shotGain, gg.shotEnvMs, gg.shotBodyKick);
            (void)g;
        }
    }

    GpApplyHapticsSettings(cfg.haptics);

    const char* which = (argc > 1) ? argv[1] : "all";

    

    if (which[0] == 'a' || which[0] == 's') {
        GpConPrintf("  （注：这条测的是「扳机判据」。UseGameState=true 且状态从未上线时，\n"
                    "    它被有意禁用 —— 想测扳机路径请把 ini 里 UseGameState 设为 false）\n");
        RunLine("开枪 (RT 扣到 210 保持 60ms)", 0, 100, 210, 60, 700, 90, 480);
    }

    
    if (which[0] == 'a' || which[0] == 'l')
        RunLine("LT 按下 (按到 200 保持 500ms)", 1, 100, 200, 500, 700, 90, 400);

    


    if (which[0] == 'a' || which[0] == 's')
        RunLine("开枪接管（游戏本体 L=R=120 同时在震）", 0, 100, 210, 60, 700, 90, 300, 120, 120);

    
    if (which[0] == 'a' || which[0] == 'm') RunAim();

    
    if (which[0] == 'a' || which[0] == 'g') RunGunShot();

    
    if (which[0] == 'a' || which[0] == 'w') RunBow();

    
    if (which[0] == 'a' || which[0] == 'r') RunRide();

    return 0;
}
