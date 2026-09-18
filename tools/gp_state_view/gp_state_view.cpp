














#include "gp_rdr2_state.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

namespace {



struct GroupName { uint32_t hash; const char* name; };
const GroupName kGroups[] = {
    { GPRDR2_GRP_PISTOL,   "Pistol"    },
    { GPRDR2_GRP_REVOLVER, "Revolver"  },
    { GPRDR2_GRP_REPEATER, "Repeater"  },
    { GPRDR2_GRP_RIFLE,    "Rifle"     },
    { GPRDR2_GRP_SHOTGUN,  "Shotgun"   },
    { GPRDR2_GRP_SNIPER,   "Sniper"    },
    { GPRDR2_GRP_BOW,      "Bow"       },
    { GPRDR2_GRP_MELEE,    "Melee"     },
    { GPRDR2_GRP_THROWN,   "Thrown"    },
    { GPRDR2_GRP_LASSO,    "Lasso"     },
};

const char* GroupName(uint32_t h) {
    if (h == 0) return "空手/无";
    for (int i = 0; i < (int)(sizeof(kGroups) / sizeof(kGroups[0])); ++i)
        if (kGroups[i].hash == h) return kGroups[i].name;
    return "未知组";
}

const char* YesNo(uint8_t v) { return v ? "是" : "否"; }

void Row(const char* label, const char* fmt, ...) {
    printf("  %-8s ", label);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

}  

int main(int argc, char** argv) {
    BOOL once = (argc > 1 && strcmp(argv[1], "--once") == 0);

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, GPRDR2_NAME);
    if (!mapping) {
        printf("找不到状态段 %ls\n", GPRDR2_NAME);
        printf("说明游戏内组件（gpxinput_rdr2.asi）没在跑：\n");
        printf("  · 文件是不是还在游戏根目录（别改成 .off）\n");
        printf("  · ScriptHookRDR2.log 里有没有 'Registering script gpxinput_rdr2.asi'\n");
        printf("  · 游戏内的 gpxinput_rdr2.log 里写了什么\n");
        return 2;
    }

    const GpRdr2State* st =
        (const GpRdr2State*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!st) {
        printf("映射失败 (err=%lu)\n", GetLastError());
        return 2;
    }

    printf("状态段已连接: %ls\n", GPRDR2_NAME);
    printf("magic=0x%08X version=%u (期望 0x%08X / %u)\n",
           st->magic, st->version, GPRDR2_MAGIC, GPRDR2_VERSION);
    if (st->magic != GPRDR2_MAGIC || st->version != GPRDR2_VERSION) {
        printf("段格式对不上 —— .asi 和这个工具不是同一版，重新编译一次\n");
        return 2;
    }

    uint32_t lastFrame = 0;
    for (;;) {
        

        GpRdr2State s;
        for (int i = 0; i < 8; ++i) {
            uint32_t s0 = st->seq;
            if (s0 & 1u) { Sleep(0); continue; }
            s = *st;
            MemoryBarrier();
            if (st->seq == s0) break;
        }

        if (!once) printf("\x1b[2J\x1b[H");   

        DWORD age = (DWORD)(GetTickCount() - s.tickMs);
        printf("========== gpxinput 游戏状态 ==========\n");
        if (s.frame == 0 || s.tickMs == 0) {
            

            printf("  心跳     组件已建段，但还没有发布任何一帧\n");
            printf("           多半是游戏还在主菜单/加载中（玩家的 ped 还是 0）；\n");
            printf("           进到世界里会自动开始。若一直如此，就看游戏目录的\n");
            printf("           gpxinput_rdr2.log 里有没有「脚本线程已启动」那一行。\n");
            printf("  写入进程 pid=%u   玩家 ped=%u\n", s.writerPid, s.playerPed);
            if (!once) { Sleep(200); continue; }
            break;
        }
        printf("  心跳     %u 帧   最后更新 %ums 前   %s\n",
               s.frame, age,
               (age <= GPRDR2_TIMEOUT_MS) ? "在线" : "已失效(代理会忽略)");
        printf("  写入进程 pid=%u   玩家 ped=%u\n", s.writerPid, s.playerPed);
        printf("--------------------------------------\n");
        printf("--------------------------------------\n");
        printf("  武器组   0x%08X  %s\n", s.weaponGroup, GroupName(s.weaponGroup));
        printf("  武器     0x%08X\n", s.weaponHash);
        printf("  弹匣     %u 发    距上次开枪 %ums\n", s.ammoInClip, s.timeSinceShot);
        printf("--------------------------------------\n");
        printf("  动作     开枪=%s  瞄准=%s  装弹=%s  持械=%s\n",
               YesNo(s.shooting), YesNo(s.aiming), YesNo(s.reloading), YesNo(s.armed));
        printf("  位置     步行=%s  骑马=%s  车内=%s  菜单=%s\n",
               YesNo(s.onFoot), YesNo(s.onMount), YesNo(s.inVehicle), YesNo(s.menuActive));
        printf("  速度     马=%0.2f  玩家=%0.2f\n", s.horseSpeed, s.playerSpeed);
        printf("======================================\n");
        (void)Row; (void)lastFrame;

        if (once) break;
        Sleep(200);
    }

    UnmapViewOfFile(st);
    CloseHandle(mapping);
    return 0;
}
