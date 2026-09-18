// haptics
#include "gp_haptics.h"
#include "gp_log.h"

#include <math.h>
#include <string.h>

namespace {

const uint32_t kMaxControllers = 4;





const float kBaselineTauMs = 300.0f;


const float kShotAttackMs = 6.0f;


const DWORD kPeakMinGapMs = 140;


const int kPeakHistory = 6;

struct CtrlState {
    
    DWORD lastTick;
    BYTE  prevL, prevR;

    
    float baseL, baseR;
    DWORD baseTick;

    
    DWORD shotStart;
    float shotAmp;
    BOOL  shotActive;
    DWORD lastShotTick;
    

    int   shotEnvMs;
    float shotBodyKick;

    
    BOOL     stateValid;
    BOOL     aiming;
    uint32_t stateGroup;    
    uint32_t lastWeapon;    
    int32_t  lastAmmo;
    BOOL     wasShooting;

    
    DWORD peakTick[kPeakHistory];
    float peakAmp[kPeakHistory];
    int   peakCount;
    DWORD lastPeakTick;
    float rideAmp;
    DWORD rideUntil;
    BOOL  rideLogged;     

    
    BYTE  prevLT, prevRT;
    BYTE  trigPeak;       
    BOOL  padValid;
    BOOL  trigArmed;      
    BYTE  padLT, padRT;   
};

CtrlState  g_c[kMaxControllers];
GpHapticsSettings g_s;
BOOL g_applied = FALSE;

inline BYTE ClampByte(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (BYTE)(v + 0.5f);
}

inline float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}



inline void UpdateBaseline(float* base, float value, float dtMs) {
    if (dtMs <= 0.0f) { *base = value; return; }
    float a = dtMs / (kBaselineTauMs + dtMs);
    *base += (value - *base) * a;
}



const GpWeaponProfile* FindProfile(uint32_t group) {
    if (group == 0) return nullptr;
    for (int i = 0; i < g_s.weaponCount && i < GP_WEAPON_SLOTS; ++i) {
        if (g_s.weapon[i].hash != 0 && g_s.weapon[i].hash == group) return &g_s.weapon[i];
    }
    return nullptr;
}



void FireShot(CtrlState* cs, DWORD now, float amp, const GpWeaponProfile* prof) {
    cs->shotActive = TRUE;
    cs->shotStart  = now;
    cs->shotAmp    = Clamp01(amp);
    cs->lastShotTick = now;

    if (prof) {
        cs->shotEnvMs    = prof->shotEnvMs > 0 ? prof->shotEnvMs : g_s.shotEnvMs;
        cs->shotBodyKick = prof->shotBodyKick;
    } else {
        cs->shotEnvMs    = g_s.shotEnvMs;
        cs->shotBodyKick = g_s.shotBodyKick;
    }
    if (cs->shotEnvMs < 10) cs->shotEnvMs = 10;
}

CtrlState* State(uint32_t controller) {
    if (controller >= kMaxControllers) return nullptr;
    return &g_c[controller];
}

void PushPeak(CtrlState* cs, DWORD tick, float amp) {
    if (cs->peakCount < kPeakHistory) {
        cs->peakTick[cs->peakCount] = tick;
        cs->peakAmp[cs->peakCount] = amp;
        cs->peakCount++;
        return;
    }
    
    for (int i = 1; i < kPeakHistory; ++i) {
        cs->peakTick[i - 1] = cs->peakTick[i];
        cs->peakAmp[i - 1] = cs->peakAmp[i];
    }
    cs->peakTick[kPeakHistory - 1] = tick;
    cs->peakAmp[kPeakHistory - 1] = amp;
}



DWORD DetectGait(CtrlState* cs, DWORD now) {
    

    if (cs->peakCount < 5) return 0;

    DWORD iv[kPeakHistory];
    int n = 0;
    for (int i = 1; i < cs->peakCount; ++i) {
        DWORD d = cs->peakTick[i] - cs->peakTick[i - 1];
        if (d < (DWORD)g_s.rideMinPeriodMs || d > (DWORD)g_s.rideMaxPeriodMs) return 0;
        iv[n++] = d;
    }
    if (n < 3) return 0;

    
    DWORD sorted[kPeakHistory];
    memcpy(sorted, iv, sizeof(DWORD) * (size_t)n);
    for (int i = 1; i < n; ++i) {
        DWORD v = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; --j; }
        sorted[j + 1] = v;
    }
    DWORD median = sorted[n / 2];

    

    for (int i = 0; i < n; ++i) {
        float dev = (float)((long)iv[i] - (long)median);
        if (dev < 0) dev = -dev;
        if (dev > (float)median * g_s.ridePeriodTol) return 0;
    }

    
    float sum = 0.0f;
    int cnt = 0;
    for (int i = 0; i < cs->peakCount; ++i) { sum += cs->peakAmp[i]; ++cnt; }
    float amp = cnt ? sum / (float)cnt : 0.0f;

    cs->rideAmp = amp;
    cs->rideUntil = now + (DWORD)g_s.rideHoldMs;
    cs->lastPeakTick = cs->peakTick[cs->peakCount - 1];

    if (!cs->rideLogged) {
        cs->rideLogged = TRUE;
        GP_LOG_INFO("haptics: 检出步态 周期=%ums 幅度=%.2f —— 开始输出骑乘辅助",
                    (unsigned)median, amp);
    }
    return median;
}


DWORD g_ridePeriod[kMaxControllers];

}  

void GpDefaultHapticsSettings(GpHapticsSettings* s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->enable           = FALSE;   

    s->shotFromTrigger  = TRUE;
    s->triggerPressThresh = 0.55f;
    s->triggerReleaseHyst = 0.20f;
    s->triggerRefractoryMs = 150;
    s->shotFromRumble   = FALSE;   

    s->shotRiseThresh   = 0.20f;
    s->shotGain         = 1.0f;
    s->shotEnvMs        = 90;
    s->shotRefractoryMs = 55;
    s->shotSide         = 0;
    s->shotBodyKick     = 0.15f;
    


    s->rideShotBoost    = 2.5f;

    s->rideEnable       = TRUE;
    s->ridePeakThresh   = 0.10f;
    s->rideGain         = 0.9f;
    s->rideTrigGain     = 0.35f;
    s->rideMinPeriodMs  = 200;
    s->rideMaxPeriodMs  = 900;
    s->ridePeriodTol    = 0.35f;
    s->rideHoldMs       = 1200;

    s->trigToBody       = 0.45f;

    
    s->useGameState = TRUE;
    s->aimBreathHz  = 0.4f;   
    s->aimTriggerLevel = 0.24f;  

    


    struct Def { uint32_t hash; const char* name; float sg; int env; float kick;
                 float at; float ab; float wob; };
    static const Def kDef[GP_WEAPON_SLOTS] = {
        { GPRDR2_GRP_PISTOL,   "Pistol",   0.80f,  70, 0.12f, 0.06f, 0.04f, 0.50f },
        { GPRDR2_GRP_REVOLVER, "Revolver", 0.95f,  80, 0.15f, 0.07f, 0.05f, 0.60f },
        { GPRDR2_GRP_REPEATER, "Repeater", 1.00f,  85, 0.15f, 0.07f, 0.05f, 0.50f },
        { GPRDR2_GRP_RIFLE,    "Rifle",    1.20f,  95, 0.18f, 0.09f, 0.06f, 0.45f },
        { GPRDR2_GRP_SHOTGUN,  "Shotgun",  1.60f, 120, 0.25f, 0.10f, 0.07f, 0.70f },
        { GPRDR2_GRP_SNIPER,   "Sniper",   1.35f, 110, 0.20f, 0.12f, 0.08f, 0.35f },
        { GPRDR2_GRP_BOW,      "Bow",      0.70f, 140, 0.10f, 0.05f, 0.03f, 0.80f },
        { GPRDR2_GRP_MELEE,    "Melee",    0.60f,  60, 0.10f, 0.00f, 0.00f, 0.00f },
    };
    for (int i = 0; i < GP_WEAPON_SLOTS; ++i) {
        s->weapon[i].hash         = kDef[i].hash;
        s->weapon[i].shotGain     = kDef[i].sg;
        s->weapon[i].shotEnvMs    = kDef[i].env;
        s->weapon[i].shotBodyKick = kDef[i].kick;
        s->weapon[i].aimTrig      = kDef[i].at;
        s->weapon[i].aimBody      = kDef[i].ab;
        s->weapon[i].aimWobble    = kDef[i].wob;
        strncpy_s(s->weapon[i].name, sizeof(s->weapon[i].name), kDef[i].name, _TRUNCATE);
    }
    s->weaponCount = GP_WEAPON_SLOTS;
}

void GpApplyHapticsSettings(const GpHapticsSettings& s) {
    g_s = s;
    g_applied = TRUE;

    
    if (g_s.shotRiseThresh < 0.01f) g_s.shotRiseThresh = 0.01f;
    if (g_s.shotRiseThresh > 1.0f)  g_s.shotRiseThresh = 1.0f;
    if (g_s.shotEnvMs < 10)  g_s.shotEnvMs = 10;
    if (g_s.shotEnvMs > 400) g_s.shotEnvMs = 400;
    if (g_s.shotRefractoryMs < 0)   g_s.shotRefractoryMs = 0;
    if (g_s.shotRefractoryMs > 500) g_s.shotRefractoryMs = 500;
    if (g_s.shotSide < 0 || g_s.shotSide > 2) g_s.shotSide = 0;
    if (g_s.triggerPressThresh < 0.1f) g_s.triggerPressThresh = 0.1f;
    if (g_s.triggerPressThresh > 0.95f) g_s.triggerPressThresh = 0.95f;
    if (g_s.triggerReleaseHyst < 0.0f) g_s.triggerReleaseHyst = 0.0f;
    if (g_s.triggerReleaseHyst > 0.5f) g_s.triggerReleaseHyst = 0.5f;
    if (g_s.triggerRefractoryMs < 0) g_s.triggerRefractoryMs = 0;
    if (g_s.triggerRefractoryMs > 2000) g_s.triggerRefractoryMs = 2000;
    if (g_s.rideShotBoost < 1.0f) g_s.rideShotBoost = 1.0f;
    if (g_s.rideShotBoost > 6.0f) g_s.rideShotBoost = 6.0f;
    if (g_s.rideMinPeriodMs < 60)  g_s.rideMinPeriodMs = 60;
    if (g_s.rideMaxPeriodMs > 3000) g_s.rideMaxPeriodMs = 3000;
    if (g_s.rideMaxPeriodMs <= g_s.rideMinPeriodMs) g_s.rideMaxPeriodMs = g_s.rideMinPeriodMs + 100;
    if (g_s.rideHoldMs < 0) g_s.rideHoldMs = 0;
    if (g_s.rideHoldMs > 10000) g_s.rideHoldMs = 10000;

    if (g_s.aimTriggerLevel < 0.05f) g_s.aimTriggerLevel = 0.05f;
    if (g_s.aimTriggerLevel > 0.9f) g_s.aimTriggerLevel = 0.9f;
    if (g_s.aimBreathHz < 0.0f) g_s.aimBreathHz = 0.0f;
    if (g_s.aimBreathHz > 5.0f) g_s.aimBreathHz = 5.0f;
    if (g_s.weaponCount < 0) g_s.weaponCount = 0;
    if (g_s.weaponCount > GP_WEAPON_SLOTS) g_s.weaponCount = GP_WEAPON_SLOTS;
    for (int i = 0; i < g_s.weaponCount; ++i) {
        if (g_s.weapon[i].shotEnvMs < 10) g_s.weapon[i].shotEnvMs = 10;
        if (g_s.weapon[i].shotEnvMs > 400) g_s.weapon[i].shotEnvMs = 400;
        if (g_s.weapon[i].shotGain < 0.0f) g_s.weapon[i].shotGain = 0.0f;
        if (g_s.weapon[i].shotGain > 4.0f) g_s.weapon[i].shotGain = 4.0f;
        g_s.weapon[i].name[sizeof(g_s.weapon[i].name) - 1] = 0;
    }

    GP_LOG_INFO("haptics: 自合成%s 开枪(扳机判据=%s 扣下阈值=%.2f / 波形判据=%s 阈值=%.2f 增益=%.2f 时长=%dms 侧=%d 体感=%.2f) "
                "骑乘(%s 增益=%.2f 扳机=%.2f 周期=%d~%dms) 无HID折算=%.2f",
                g_s.enable ? "开启" : "关闭",
                g_s.shotFromTrigger ? "开" : "关", g_s.triggerPressThresh,
                g_s.shotFromRumble ? "开" : "关",
                g_s.shotRiseThresh, g_s.shotGain, g_s.shotEnvMs, g_s.shotSide, g_s.shotBodyKick,
                g_s.rideEnable ? "开" : "关", g_s.rideGain, g_s.rideTrigGain,
                g_s.rideMinPeriodMs, g_s.rideMaxPeriodMs, g_s.trigToBody);

    GP_LOG_INFO("haptics: 游戏状态=%s 武器档=%d 套 瞄准起伏=%.2fHz",
                g_s.useGameState ? "采信" : "忽略", g_s.weaponCount, g_s.aimBreathHz);
    for (int i = 0; i < g_s.weaponCount; ++i) {
        const GpWeaponProfile& p = g_s.weapon[i];
        GP_LOG_INFO("haptics:   档%d %-9s 组=0x%08X 开枪(强度=%.2f 时长=%dms 体感=%.2f) "
                    "瞄准(扳机=%.2f 体感=%.2f 起伏=%.2f)",
                    i, p.name, p.hash, p.shotGain, p.shotEnvMs, p.shotBodyKick,
                    p.aimTrig, p.aimBody, p.aimWobble);
    }
}

void GpOnGameFrame(uint32_t controller, DWORD tick, BYTE bodyL, BYTE bodyR) {
    CtrlState* cs = State(controller);
    if (!cs) return;

    

    if (cs->lastTick == tick && cs->prevL == bodyL && cs->prevR == bodyR) return;

    DWORD now = tick ? tick : GetTickCount();
    if (cs->lastTick == 0) {
        cs->baseL = (float)bodyL;
        cs->baseR = (float)bodyR;
        cs->baseTick = now;
    }

    float dt = (float)(DWORD)(now - cs->baseTick);
    if (dt > 0.0f && dt < 1000.0f) {
        UpdateBaseline(&cs->baseL, (float)bodyL, dt);
        UpdateBaseline(&cs->baseR, (float)bodyR, dt);
        cs->baseTick = now;
    }

    
    float riseL = ((float)bodyL - cs->baseL) / 255.0f;
    float riseR = ((float)bodyR - cs->baseR) / 255.0f;

    




    BOOL risingL = bodyL > cs->prevL;
    BOOL risingR = bodyR > cs->prevR;

    float rise = 0.0f;
    int   side = 0;
    if (risingR && riseR >= riseL)      { rise = riseR; side = 0; }
    else if (risingL && riseL > riseR)  { rise = riseL; side = 1; }
    else if (risingR)                   { rise = riseR; side = 0; }
    else if (risingL)                   { rise = riseL; side = 1; }

    

    float prevRiseL = ((float)cs->prevL - cs->baseL) / 255.0f;
    float prevRiseR = ((float)cs->prevR - cs->baseR) / 255.0f;
    float prevRise  = prevRiseL > prevRiseR ? prevRiseL : prevRiseR;

    DWORD sinceShot = now - cs->lastShotTick;

    
    float thresh = g_s.shotRiseThresh;
    if (g_s.rideEnable && now < cs->rideUntil) thresh *= g_s.rideShotBoost;

    if (g_s.shotFromRumble &&
        rise >= thresh &&
        (prevRise < thresh * 0.6f || sinceShot > (DWORD)g_s.shotRefractoryMs) &&
        sinceShot >= (DWORD)g_s.shotRefractoryMs) {
        cs->shotActive = TRUE;
        cs->shotStart = now;
        cs->shotAmp = Clamp01(rise * g_s.shotGain);
        cs->lastShotTick = now;
        GP_LOG_DEBUG("haptics: 检测到冲击 幅度=%.2f 侧=%s (基线 L=%.0f R=%.0f 现值 L=%u R=%u)",
                     rise, side == 0 ? "右" : "左", cs->baseL, cs->baseR,
                     (unsigned)bodyL, (unsigned)bodyR);
    }

    





    BOOL triggerBusy = (cs->padRT > 40) ||
                       (cs->lastShotTick != 0 && (now - cs->lastShotTick) < 300);

    if (g_s.rideEnable && !triggerBusy) {
        float peak = ((float)bodyL + (float)bodyR) / 2.0f / 255.0f;
        float prevPeak = ((float)cs->prevL + (float)cs->prevR) / 2.0f / 255.0f;
        

        if (peak >= g_s.ridePeakThresh && prevPeak < g_s.ridePeakThresh &&
            (cs->lastPeakTick == 0 || (now - cs->lastPeakTick) >= kPeakMinGapMs)) {
            PushPeak(cs, now, peak);
            cs->lastPeakTick = now;
            DWORD period = DetectGait(cs, now);
            if (period) g_ridePeriod[controller] = period;
        }
    }

    cs->prevL = bodyL;
    cs->prevR = bodyR;
    cs->lastTick = tick;
}

void GpOnPadInput(uint32_t controller, DWORD now, BYTE leftTrigger, BYTE rightTrigger) {
    CtrlState* cs = State(controller);
    if (!cs || !g_s.enable) return;

    cs->padLT = leftTrigger;
    cs->padRT = rightTrigger;

    if (!cs->padValid) {
        cs->prevLT = leftTrigger;
        cs->prevRT = rightTrigger;
        cs->trigPeak = rightTrigger;
        

        cs->trigArmed = (rightTrigger < 64);
        cs->padValid = TRUE;
        return;
    }

    







    
    float thresh = g_s.triggerPressThresh;
    if (g_s.rideEnable && now < cs->rideUntil) thresh = Clamp01(thresh + 0.25f);

    BYTE hi = (BYTE)(thresh * 255.0f + 0.5f);
    BYTE releasePx = (BYTE)(g_s.triggerReleaseHyst * 255.0f + 0.5f);
    if (releasePx < 8) releasePx = 8;

    if (rightTrigger > cs->trigPeak) cs->trigPeak = rightTrigger;
    if (cs->trigPeak > (BYTE)(releasePx + 16) &&
        rightTrigger <= (BYTE)(cs->trigPeak - releasePx)) {
        cs->trigArmed = TRUE;
        cs->trigPeak = rightTrigger;
    }

    DWORD sinceShot = now - cs->lastShotTick;

    

    BOOL stateShot = (g_s.useGameState && cs->stateValid);

    if (!stateShot && g_s.shotFromTrigger && cs->trigArmed && rightTrigger >= hi &&
        sinceShot >= (DWORD)g_s.triggerRefractoryMs) {
        const GpWeaponProfile* prof = FindProfile(cs->stateGroup);
        
        float amp = Clamp01((float)rightTrigger / 255.0f) * g_s.shotGain;
        FireShot(cs, now, amp, prof);
        cs->trigArmed = FALSE;
        cs->trigPeak = rightTrigger;
        GP_LOG_DEBUG("haptics: 扳机扣下 -> 开枪脉冲 (RT=%u 阈值=%u 强度=%.2f %s)",
                     (unsigned)rightTrigger, (unsigned)hi, amp,
                     prof ? prof->name : "通用");
    }

    cs->prevLT = leftTrigger;
    cs->prevRT = rightTrigger;
}

void GpOnGameState(uint32_t controller, DWORD now, BOOL valid, const GpRdr2State* st) {
    CtrlState* cs = State(controller);
    if (!cs || !g_s.enable) return;

    if (!valid || !st) {
        

        cs->stateValid  = FALSE;
        cs->aiming      = FALSE;
        cs->stateGroup  = 0;
        cs->lastWeapon  = 0;
        cs->lastAmmo    = 0;
        cs->wasShooting = FALSE;
        return;
    }

    

    if (st->weaponHash != cs->lastWeapon) {
        cs->lastWeapon  = st->weaponHash;
        cs->lastAmmo    = st->ammoInClip;
        cs->wasShooting = st->shooting != 0;
        cs->stateGroup  = st->weaponGroup;
        cs->stateValid  = TRUE;
        cs->aiming      = st->aiming != 0;
        return;
    }

    cs->stateValid = TRUE;
    cs->aiming     = st->aiming != 0;
    cs->stateGroup = st->weaponGroup;

    if (!g_s.useGameState) return;

    



    const GpWeaponProfile* prof = FindProfile(st->weaponGroup);
    DWORD refr = (DWORD)g_s.triggerRefractoryMs;

    if (st->ammoInClip >= 0 && st->ammoInClip < cs->lastAmmo) {
        int fired = cs->lastAmmo - st->ammoInClip;
        if (fired > 8) fired = 1;      
        for (int i = 0; i < fired; ++i) {
            if (i > 0 && now - cs->lastShotTick < refr) break;
            float amp = prof ? prof->shotGain : g_s.shotGain;
            FireShot(cs, now, amp, prof);
        }
        GP_LOG_DEBUG("haptics: 弹匣 %d -> %d，判定开枪 %d 发（组=0x%08X %s）",
                     cs->lastAmmo, st->ammoInClip, fired, st->weaponGroup,
                     prof ? prof->name : "通用");
    } else if (!prof && st->shooting && !cs->wasShooting &&
               now - cs->lastShotTick >= refr) {
        
        FireShot(cs, now, g_s.shotGain, nullptr);
        GP_LOG_DEBUG("haptics: shooting 上升沿 -> 开枪脉冲（组=0x%08X）", st->weaponGroup);
    }

    cs->lastAmmo    = st->ammoInClip;
    cs->wasShooting = st->shooting != 0;
}

void GpHapticsGetStatus(uint32_t controller, DWORD now, GpHapticsStatus* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));

    CtrlState* cs = State(controller);
    if (!cs) return;

    if (cs->shotActive) {
        DWORD t = now - cs->shotStart;
        if (t < (DWORD)g_s.shotEnvMs) {
            out->shotActive = 1;
            out->shotRemainMs = (uint16_t)((DWORD)g_s.shotEnvMs - t);
        }
    }
    out->stateValid  = cs->stateValid ? 1 : 0;
    out->weaponGroup = cs->stateGroup;
    out->aiming = (uint8_t)(cs->stateValid &&
                  (cs->aiming || cs->padLT >= (BYTE)(g_s.aimTriggerLevel * 255.0f + 0.5f))) ? 1 : 0;

    if (g_s.rideEnable && now < cs->rideUntil) {
        out->rideActive = 1;
        out->ridePeriodMs = (uint16_t)(g_ridePeriod[controller] > 0xFFFF
                                       ? 0xFFFF : g_ridePeriod[controller]);
        float a = cs->rideAmp;
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        out->rideAmp = (uint8_t)(a * 255.0f + 0.5f);
    }
}

BOOL GpHapticsActive(uint32_t controller) {
    CtrlState* cs = State(controller);
    if (!cs) return FALSE;
    DWORD now = GetTickCount();
    if (cs->shotActive && (DWORD)(now - cs->shotStart) < (DWORD)g_s.shotEnvMs) return TRUE;
    if (g_s.rideEnable && now < cs->rideUntil) return TRUE;
    return FALSE;
}

void GpTickHaptics(uint32_t controller, DWORD now, BOOL hasGame,
                   BYTE baseL, BYTE baseR, BYTE baseTrigL, BYTE baseTrigR,
                   GpHapticsOut* out) {
    if (!out) return;
    out->leftMotor   = baseL;
    out->rightMotor  = baseR;
    out->leftTrigger  = baseTrigL;
    out->rightTrigger = baseTrigR;

    if (!g_applied || !g_s.enable) return;

    CtrlState* cs = State(controller);
    if (!cs) return;

    float addBodyL = 0.0f, addBodyR = 0.0f, addTrigL = 0.0f, addTrigR = 0.0f;

    
    if (cs->shotActive) {
        

        DWORD envMs = (DWORD)(cs->shotEnvMs > 0 ? cs->shotEnvMs : g_s.shotEnvMs);
        DWORD t = now - cs->shotStart;
        if (t < envMs) {
            float tMs = (float)t;
            

            float attack = tMs < kShotAttackMs ? (tMs / kShotAttackMs) : 1.0f;
            float x = tMs / (float)envMs;
            float decay = powf(1.0f - x, 1.2f);
            float env = attack * decay * cs->shotAmp;

            



            float env255 = env * 255.0f;

            switch (g_s.shotSide) {
            case 1:  addTrigL += env255; break;
            case 2:  addTrigL += env255; addTrigR += env255; break;
            default: addTrigR += env255; break;
            }
            addBodyL += env255 * cs->shotBodyKick;
            addBodyR += env255 * cs->shotBodyKick;
        } else {
            cs->shotActive = FALSE;
        }
    }

    
    if (g_s.rideEnable && now < cs->rideUntil) {
        DWORD period = g_ridePeriod[controller];
        if (period >= 60) {
            DWORD phase = (now - cs->lastPeakTick) % period;
            float x = (float)phase / (float)period;      
            float env = powf(1.0f - x, 1.6f);            

            

            float amp = Clamp01(cs->rideAmp * g_s.rideGain) * env;
            addBodyL += amp * 255.0f * 0.5f;
            addBodyR += amp * 255.0f * 0.5f;
            addTrigL += amp * g_s.rideTrigGain * 255.0f;
            addTrigR += amp * g_s.rideTrigGain * 255.0f;
        }
    } else if (cs->rideLogged) {
        cs->rideLogged = FALSE;
        GP_LOG_INFO("haptics: 步态结束 —— 停止骑乘辅助");
    }

    







    BOOL aimingNow = cs->stateValid &&
                     (cs->aiming || cs->padLT >= (BYTE)(g_s.aimTriggerLevel * 255.0f + 0.5f));
    if (g_s.useGameState && aimingNow) {
        const GpWeaponProfile* prof = FindProfile(cs->stateGroup);
        if (prof && (prof->aimTrig > 0.0f || prof->aimBody > 0.0f)) {
            float w = 1.0f;
            if (g_s.aimBreathHz > 0.01f && prof->aimWobble > 0.0f) {
                

                float secs = (float)now / 1000.0f;
                w = 1.0f - prof->aimWobble * 0.5f *
                            (1.0f - cosf(6.2831853f * g_s.aimBreathHz * secs));
            }
            addTrigL += prof->aimTrig * 255.0f * w;
            addTrigR += prof->aimTrig * 255.0f * w;
            addBodyL += prof->aimBody * 255.0f * w;
            addBodyR += prof->aimBody * 255.0f * w;
        }
    }

    out->leftMotor    = ClampByte((float)out->leftMotor + addBodyL);
    out->rightMotor   = ClampByte((float)out->rightMotor + addBodyR);
    out->leftTrigger  = ClampByte((float)out->leftTrigger + addTrigL);
    out->rightTrigger = ClampByte((float)out->rightTrigger + addTrigR);

    (void)hasGame;   
}

void GpResetHaptics(void) {
    memset(g_c, 0, sizeof(g_c));
    memset(g_ridePeriod, 0, sizeof(g_ridePeriod));
}
