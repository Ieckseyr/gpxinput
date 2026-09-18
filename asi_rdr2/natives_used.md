# 我们依赖的 native 清单（签名以官方 SDK 为准）

这份文件存在的唯一理由：**native 的参数个数和类型不能靠猜**。

`GET_CURRENT_PED_WEAPON` 少传两个参数、`GET_AMMO_IN_CLIP` 的第三个参数是指针而不是
布尔 —— 这两处都曾经让脚本在游戏里访问违例（0xC0000005）。而且它们**不是必崩**：
少传的参数拿到的是参数栈上的残留值，恰好无害时能跑过去，于是现象变成「有时能用、
多数时候整帧被跳过」，从日志上完全看不出原因。

## 权威来源

- **ScriptHookRDR2 SDK**（Alexander Blade，<https://www.dev-c.com/rdr2/scripthookrdr2>）
  里的 `inc/natives.h` —— 每个 native 的真实签名都在那里，是**唯一**该信的东西。
  该文件受版权保护，**不要**复制进本仓库；需要时从官网下载 SDK，解压后对照。
- 参考实现：[GlossMod/RDR2NativeTrainer](https://github.com/GlossMod/RDR2NativeTrainer)
  —— 真实 mod 的用法（注册约定、循环、native 调用方式）。

## 注册约定（已按官方示例核对）

```cpp
DllMain(DLL_PROCESS_ATTACH): scriptRegister(hInstance, ScriptMain);   // 自己注册自己
void ScriptMain() { while (true) { ...; WAIT(0); } }                  // 脚本自己循环
```

- 注册的函数**只被调用一次**，脚本必须自己循环，每帧 `WAIT(0)`（= `scriptWait(0)`）让出。
  写成「做一帧就 return」的现象是：脚本跑了一次就没了，状态只发布 1 帧。
- 脚本内部抛异常（含访问违例）会被 ScriptHookRDR2 判为「执行出错」并**停掉整个脚本**，
  所以每帧要自己兜住异常，别让它逃出去。

## 我们调用的 native（签名照抄 SDK）

| 用途 | native | 签名 | 备注 |
|---|---|---|---|
| 玩家 ped | `PLAYER_PED_ID` | `Ped PLAYER_PED_ID()` | 0 参数 |
| 当前武器 | `GET_CURRENT_PED_WEAPON` | `BOOL GET_CURRENT_PED_WEAPON(Ped ped, Hash* weaponHash, BOOL unused, Any p3, BOOL p4)` | **5 个参数**，武器哈希是写进指针的 |
| 武器类别 | `GET_WEAPONTYPE_GROUP` | `Hash GET_WEAPONTYPE_GROUP(Hash weaponHash)` | |
| 弹匣弹药 | `GET_AMMO_IN_CLIP` | `BOOL GET_AMMO_IN_CLIP(Ped ped, Hash weaponHash, int* ammo)` | **第 3 个参数是 `int*`**，不是返回值 |
| 距上次开枪 | `_0x285D36C5C72B0569` | `Any _0x285D36C5C72B0569(Any p0)` | SDK 里还没起名 |
| 坐骑 | `GET_MOUNT` | `Ped GET_MOUNT(Ped ped)` | |
| 实体速度 | `GET_ENTITY_SPEED` | `float GET_ENTITY_SPEED(Entity entity)` | 返回值是 float，按位取 |
| 是否在射击 | `IS_PED_SHOOTING` | `BOOL IS_PED_SHOOTING(Ped ped)` | |
| 是否自由瞄准 | `IS_PLAYER_FREE_AIMING` | `BOOL IS_PLAYER_FREE_AIMING(Player player)` | 参数是**玩家索引**，不是 ped |
| 是否装弹 | `IS_PED_RELOADING` | `BOOL IS_PED_RELOADING(Ped ped)` | |
| 是否步行 | `IS_PED_ON_FOOT` | `BOOL IS_PED_ON_FOOT(Ped ped)` | |
| 是否在马上 | `IS_PED_ON_MOUNT` | `BOOL IS_PED_ON_MOUNT(Ped ped)` | |
| 是否在车内 | `IS_PED_IN_ANY_VEHICLE` | `BOOL IS_PED_IN_ANY_VEHICLE(Ped ped, BOOL atGetIn)` | |
| 菜单是否打开 | `IS_PAUSE_MENU_ACTIVE` | `BOOL IS_PAUSE_MENU_ACTIVE()` | 0 参数 |
| 是否持械 | `_0xCB690F680A3EA971` | `Any _0xCB690F680A3EA971(Any p0, Any p1)` | SDK 里还没起名（= IS_PED_ARMED） |

哈希值在 `rdr2_natives.h` 里，是用工具从 `ScriptHookRDRNetAPI.dll` 的方法 IL 里读出来的
（见 `tools/hashdump`），已与已知值交叉核对 —— 哈希本身没问题，踩过的两次坑都在**参数**上。

## 踩过的坑（记录在此，避免重复）

1. **少传参数**：`GET_CURRENT_PED_WEAPON` 传 3 个（应 5 个）、`GET_AMMO_IN_CLIP` 传 2 个
   （应 3 个，且第 3 个是指针）。少传的参数从参数栈上取残留值，**时崩时不崩**。
2. **把指针参数当返回值**：`GET_AMMO_IN_CLIP` 的结果是写进 `int*` 的，返回值只是 BOOL。
3. **没有异常兜底**：任何一次访问违例都会让 ScriptHookRDR2 停掉整个脚本。
   现在每帧自己 `__try/__except` 兜住，并且记录「第几步 + 异常码」。
