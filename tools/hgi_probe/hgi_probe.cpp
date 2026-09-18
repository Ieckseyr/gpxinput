// tool
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.gaming.input.h>

#include <stdio.h>
#include "gp_text.h"
#include <string.h>

namespace {

using namespace ABI::Windows::Gaming::Input;
using ABI::Windows::Foundation::Collections::IVectorView;




typedef IVectorView<Gamepad*> GamepadView;

IGamepadStatics* g_statics = nullptr;
IGamepad*        g_pad     = nullptr;

BOOL OpenPad(void) {
    if (!g_statics) {
        HSTRING cls = nullptr;
        const wchar_t* name = RuntimeClass_Windows_Gaming_Input_Gamepad;
        if (FAILED(WindowsCreateString(name, (UINT32)wcslen(name), &cls))) return FALSE;
        HRESULT hr = RoGetActivationFactory(cls, __uuidof(IGamepadStatics),
                                            (void**)&g_statics);
        if (FAILED(hr) || !g_statics) {
            GpConPrintf("  RoGetActivationFactory 失败 hr=0x%08lX\n", (unsigned long)hr);
            return FALSE;
        }
    }

    GamepadView* view = nullptr;
    HRESULT hr = g_statics->get_Gamepads(&view);
    if (FAILED(hr) || !view) {
        GpConPrintf("  get_Gamepads 失败 hr=0x%08lX\n", (unsigned long)hr);
        return FALSE;
    }

    unsigned n = 0;
    view->get_Size(&n);
    GpConPrintf("  Windows.Gaming.Input 报告的手柄数: %u\n", n);
    if (n == 0) { view->Release(); return FALSE; }

    


    IGamepad* pad = nullptr;
    if (FAILED(view->GetAt(0, &pad)) || !pad) {
        GpConPrintf("  取第一个手柄失败\n");
        view->Release();
        return FALSE;
    }

    
    GamepadVibration cur = {0, 0, 0, 0};
    if (SUCCEEDED(pad->get_Vibration(&cur)))
        GpConPrintf("  当前振动值: LM=%.2f RM=%.2f LT=%.2f RT=%.2f\n",
               cur.LeftMotor, cur.RightMotor, cur.LeftTrigger, cur.RightTrigger);

    g_pad = pad;
    view->Release();
    return TRUE;
}

void SetVib(double lm, double rm, double lt, double rt) {
    if (!g_pad) return;
    GamepadVibration v = {lm, rm, lt, rt};
    HRESULT hr = g_pad->put_Vibration(v);
    if (FAILED(hr)) GpConPrintf("  put_Vibration 失败 hr=0x%08lX\n", (unsigned long)hr);
}

}  

int main(int argc, char** argv) {
    BOOL buzz = (argc > 1 && strcmp(argv[1], "--buzz") == 0);

    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    GpConPrintf("RoInitialize hr=0x%08lX\n", (unsigned long)hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        GpConPrintf("[失败] WinRT 初始化不了，这条通道不可用\n");
        return 2;
    }

    if (!OpenPad()) {
        GpConPrintf("[失败] 没有找到可用手柄（或这条通道打不开）\n");
        return 2;
    }

    if (!buzz) {
        GpConPrintf("[通过] 这条通道可用；加 --buzz 可以试一下扳机\n");
        return 0;
    }

    GpConPrintf("\n左扳机 (LT) 0.8 —— 一秒\n");
    SetVib(0, 0, 0.8, 0);
    Sleep(1000);
    SetVib(0, 0, 0, 0);
    Sleep(300);

    GpConPrintf("右扳机 (RT) 0.8 —— 一秒\n");
    SetVib(0, 0, 0, 0.8);
    Sleep(1000);
    SetVib(0, 0, 0, 0);
    Sleep(300);

    GpConPrintf("体感两个马达各 0.5 —— 一秒（对照，确认不是整条链路都没通）\n");
    SetVib(0.5, 0.5, 0, 0);
    Sleep(1000);
    SetVib(0, 0, 0, 0);

    GpConPrintf("\n刚才三次震动有没有感觉？\n");
    GpConPrintf("  三次都有      -> 这条通道完全可用，代理就该用它驱动扳机\n");
    GpConPrintf("  只有第三次有  -> 扳机通道没通（体感通），要查驱动/固件\n");
    GpConPrintf("  都没有        -> 手柄没连上或没电，先查硬件\n");
    return 0;
}
