#include "backdrop.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_MICA_EFFECT
#define DWMWA_MICA_EFFECT 1029
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif

enum ACCENT_STATE
{
    ACCENT_DISABLED                 = 0,
    ACCENT_ENABLE_BLURBEHIND        = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
};

struct ACCENT_POLICY
{
    int   AccentState;
    int   AccentFlags;
    DWORD GradientColor;
    int   AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB { WCA_ACCENT_POLICY = 19 };

struct WINDOWCOMPOSITIONATTRIBDATA
{
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID                   pvData;
    SIZE_T                  cbData;
};

using SetWindowCompositionAttribute_t = BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

namespace
{
    void extend_glass(HWND hwnd)
    {
        const MARGINS margins = { -1, -1, -1, -1 };
        ::DwmExtendFrameIntoClientArea(hwnd, &margins);
    }

    bool try_win11_backdrop(HWND hwnd)
    {
        BOOL dark = TRUE;
        ::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        const int backdrop = DWMSBT_TRANSIENTWINDOW;
        if (SUCCEEDED(::DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop))))
            return true;

        const BOOL mica = TRUE;
        return SUCCEEDED(::DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFECT, &mica, sizeof(mica)));
    }

    void apply_win10_acrylic(HWND hwnd)
    {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (!user32)
            return;

        auto fn = reinterpret_cast<SetWindowCompositionAttribute_t>(
            ::GetProcAddress(user32, "SetWindowCompositionAttribute"));
        if (!fn)
            return;

        ACCENT_POLICY policy = {};
        policy.AccentState   = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.AccentFlags   = 2;
        policy.GradientColor = 0x8018181C;

        WINDOWCOMPOSITIONATTRIBDATA data = {};
        data.Attrib = WCA_ACCENT_POLICY;
        data.pvData = &policy;
        data.cbData = sizeof(policy);
        fn(hwnd, &data);
    }
}

void backdrop_apply(HWND hwnd)
{
    if (!hwnd)
        return;

    extend_glass(hwnd);
    if (!try_win11_backdrop(hwnd))
        apply_win10_acrylic(hwnd);
}

void backdrop_remove(HWND hwnd)
{
    if (!hwnd)
        return;

    const int backdrop = DWMSBT_MAINWINDOW;
    ::DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (!user32)
        return;

    auto fn = reinterpret_cast<SetWindowCompositionAttribute_t>(
        ::GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!fn)
        return;

    ACCENT_POLICY policy = {};
    policy.AccentState = ACCENT_DISABLED;

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &policy;
    data.cbData = sizeof(policy);
    fn(hwnd, &data);
}
