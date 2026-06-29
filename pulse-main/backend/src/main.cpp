// Pulse Network Optimizer - Win32 C++ port
// Dark themed, borderless window with GDI+ rendering
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <sstream>
#include "OptimizationEngine.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;

// ─── Constants ───────────────────────────────────────────────────────────────
#define WND_WIDTH   720
#define WND_HEIGHT  500
#define TITLEBAR_H   50
#define FOOTER_H     60

#define CLR_BG          Color(255, 11, 12, 21)
#define CLR_PANEL       Color(255, 18, 21, 38)
#define CLR_ACCENT      Color(255, 255, 59, 63)
#define CLR_ACCENT_H    Color(255, 255, 84, 87)
#define CLR_ACCENT_P    Color(255, 204, 47, 50)
#define CLR_TEXT        Color(255, 255, 255, 255)
#define CLR_MUTED       Color(255, 142, 146, 162)
#define CLR_BORDER      Color(255, 37, 42, 69)
#define CLR_FOOTER      Color(255, 7, 8, 14)
#define CLR_CTRL_BG     Color(255, 28, 33, 56)
#define CLR_TRANSPARENT Color(0, 0, 0, 0)

// Control IDs
#define ID_BTN_CLOSE     101
#define ID_BTN_MINIMIZE  102
#define ID_BTN_RUN       103
#define ID_BTN_RESTORE   104
#define ID_CHK_SMART     105
#define ID_SLD_HITREG    106
#define ID_SLD_KBRED     107
#define ID_CMB_CONNTYPE  108
#define ID_TIMER_ANIM    200
#define WM_UPDATESTATUS  (WM_USER + 1)

// ─── Global state ─────────────────────────────────────────────────────────────
struct AppState {
    bool smartPackets   = true;
    double hitReg       = 100.0;
    double kbReduction  = 100.0;
    std::wstring connType = L"Fiber";
    std::wstring statusText = L"Ready to pulse.";
    bool isOptimized    = false;
    bool isProcessing   = false;
    HWND hwndMain       = nullptr;
    HWND hwndStatus     = nullptr;
    HWND hwndBtnRun     = nullptr;
    HWND hwndBtnRestore = nullptr;
    HWND hwndChkSmart   = nullptr;
    HWND hwndSldHitReg  = nullptr;
    HWND hwndSldKbRed   = nullptr;
    HWND hwndCmbConn    = nullptr;
    HWND hwndHitVal     = nullptr;
    HWND hwndKbVal      = nullptr;
    ULONG_PTR gdiplusToken = 0;
};

static AppState g_app;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static void PostStatus(const std::wstring& msg) {
    // Thread-safe status update via PostMessage
    wchar_t* copy = new wchar_t[msg.size() + 1];
    wcscpy_s(copy, msg.size() + 1, msg.c_str());
    PostMessageW(g_app.hwndMain, WM_UPDATESTATUS, 0, (LPARAM)copy);
}

static void UpdateControlStates() {
    bool canOptimize = !g_app.isOptimized && !g_app.isProcessing;
    bool canRestore  =  g_app.isOptimized && !g_app.isProcessing;

    EnableWindow(g_app.hwndBtnRun,     canOptimize ? TRUE : FALSE);
    EnableWindow(g_app.hwndBtnRestore, canRestore  ? TRUE : FALSE);
    EnableWindow(g_app.hwndChkSmart,   canOptimize ? TRUE : FALSE);
    EnableWindow(g_app.hwndSldHitReg,  canOptimize ? TRUE : FALSE);
    EnableWindow(g_app.hwndSldKbRed,   canOptimize ? TRUE : FALSE);
    EnableWindow(g_app.hwndCmbConn,    canOptimize ? TRUE : FALSE);

    if (g_app.isProcessing) {
        SetWindowTextW(g_app.hwndBtnRun, L"PULSING...");
    } else if (g_app.isOptimized) {
        SetWindowTextW(g_app.hwndBtnRun, L"OPTIMIZED");
    } else {
        SetWindowTextW(g_app.hwndBtnRun, L"RUN PULSE");
    }

    ShowWindow(g_app.hwndBtnRestore, canRestore ? SW_SHOW : SW_HIDE);

    wchar_t valBuf[8];
    swprintf_s(valBuf, L"%.0f", g_app.hitReg);
    SetWindowTextW(g_app.hwndHitVal, valBuf);
    swprintf_s(valBuf, L"%.0f", g_app.kbReduction);
    SetWindowTextW(g_app.hwndKbVal, valBuf);

    InvalidateRect(g_app.hwndMain, nullptr, FALSE);
}

// ─── Dark-mode checkbox subclass ──────────────────────────────────────────────
static WNDPROC g_origChkProc = nullptr;
static LRESULT CALLBACK ChkSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        wchar_t text[128] = {};
        GetWindowTextW(hwnd, text, 128);
        bool checked = (SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
        bool enabled = IsWindowEnabled(hwnd);

        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        // Clear
        SolidBrush bgBr(CLR_BG);
        g.FillRectangle(&bgBr, 0, 0, rc.right, rc.bottom);

        // Label
        Font font(L"Segoe UI", 12.0f, FontStyleRegular, UnitPoint);
        SolidBrush textBr(enabled ? CLR_TEXT : CLR_MUTED);
        RectF textRc(0.0f, 0.0f, (REAL)(rc.right - 56), (REAL)rc.bottom);
        StringFormat sf; sf.SetAlignment(StringAlignmentNear); sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(text, -1, &font, textRc, &sf, &textBr);

        // Toggle switch background
        int sw = 44, sh = 22;
        int sx = rc.right - sw, sy = (rc.bottom - sh) / 2;
        GraphicsPath trackPath;
        trackPath.AddArc(sx, sy, sh, sh, 90, 180);
        trackPath.AddArc(sx + sw - sh, sy, sh, sh, 270, 180);
        trackPath.CloseFigure();
        Color trackColor = checked ? CLR_ACCENT : CLR_CTRL_BG;
        if (!enabled) trackColor = Color(60, trackColor.GetR(), trackColor.GetG(), trackColor.GetB());
        SolidBrush trackBr(trackColor);
        g.FillPath(&trackBr, &trackPath);

        // Thumb
        int thumbX = checked ? (sx + sw - sh + 3) : (sx + 3);
        int thumbY = sy + 3;
        int thumbD = sh - 6;
        SolidBrush thumbBr(checked ? CLR_TEXT : CLR_MUTED);
        g.FillEllipse(&thumbBr, thumbX, thumbY, thumbD, thumbD);

        EndPaint(hwnd, &ps);
        return 0;
    }
    return CallWindowProcW(g_origChkProc, hwnd, msg, wp, lp);
}

// ─── Custom button drawing (dark style) ──────────────────────────────────────
static void DrawDarkButton(DRAWITEMSTRUCT* dis) {
    Graphics g(dis->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    RECT& r = dis->rcItem;
    int w = r.right - r.left, h = r.bottom - r.top;
    UINT ctrlId = dis->CtlID;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool hovered = false;
    POINT pt; GetCursorPos(&pt); ScreenToClient(dis->hwndItem, &pt);
    RECT clientRc; GetClientRect(dis->hwndItem, &clientRc);
    hovered = PtInRect(&clientRc, pt) != 0;

    wchar_t text[64] = {};
    GetWindowTextW(dis->hwndItem, text, 64);

    if (ctrlId == ID_BTN_RUN) {
        Color bg = disabled ? Color(100, 180, 40, 42) : (pressed ? CLR_ACCENT_P : (hovered ? CLR_ACCENT_H : CLR_ACCENT));
        SolidBrush br(bg);
        GraphicsPath path;
        path.AddArc(0, 0, 12, 12, 180, 90);
        path.AddArc(w - 12, 0, 12, 12, 270, 90);
        path.AddArc(w - 12, h - 12, 12, 12, 0, 90);
        path.AddArc(0, h - 12, 12, 12, 90, 90);
        path.CloseFigure();
        g.FillPath(&br, &path);
    } else if (ctrlId == ID_BTN_RESTORE) {
        Color borderCol = hovered ? CLR_ACCENT_H : CLR_ACCENT;
        Pen pen(borderCol, 1.5f);
        Color bgCol = (hovered || pressed) ? Color(37, 255, 59, 63) : CLR_TRANSPARENT;
        SolidBrush br(bgCol);
        GraphicsPath path;
        path.AddArc(0, 0, 12, 12, 180, 90);
        path.AddArc(w - 12, 0, 12, 12, 270, 90);
        path.AddArc(w - 12, h - 12, 12, 12, 0, 90);
        path.AddArc(0, h - 12, 12, 12, 90, 90);
        path.CloseFigure();
        g.FillPath(&br, &path);
        g.DrawPath(&pen, &path);
    } else {
        // Minimize / Close
        SolidBrush clearBr(CLR_BG);
        g.FillRectangle(&clearBr, 0, 0, w, h);
    }

    Font font(L"Segoe UI", 10.0f, FontStyleBold, UnitPoint);
    SolidBrush textBr(disabled ? CLR_MUTED : CLR_TEXT);
    RectF textRc(0.0f, 0.0f, (REAL)w, (REAL)h);
    StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text, -1, &font, textRc, &sf, &textBr);
}

// ─── Main window paint ────────────────────────────────────────────────────────
static void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT clientRc;
    GetClientRect(hwnd, &clientRc);
    int W = clientRc.right, H = clientRc.bottom;

    // Double-buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
    SelectObject(memDC, memBmp);

    {
        Graphics g(memDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        // Window background (rounded)
        LinearGradientBrush bgBr(Point(0, 0), Point(W, H),
            Color(255, 16, 19, 36), Color(255, 9, 10, 16));
        GraphicsPath winPath;
        winPath.AddArc(0, 0, 24, 24, 180, 90);
        winPath.AddArc(W - 24, 0, 24, 24, 270, 90);
        winPath.AddArc(W - 24, H - 24, 24, 24, 0, 90);
        winPath.AddArc(0, H - 24, 24, 24, 90, 90);
        winPath.CloseFigure();
        g.FillPath(&bgBr, &winPath);

        // Border
        Pen borderPen(CLR_ACCENT, 1.0f);
        g.DrawPath(&borderPen, &winPath);

        // ── Title bar ──
        // Logo pulse waveform
        Pen wavePen(CLR_ACCENT, 2.5f);
        wavePen.SetStartCap(LineCapRound);
        wavePen.SetEndCap(LineCapRound);
        PointF wavePts[] = {
            {20, 25}, {26, 25}, {29, 18}, {32, 35}, {35, 15},
            {38, 35}, {41, 25}, {56, 25}
        };
        g.DrawLines(&wavePen, wavePts, 8);

        // Title text
        Font titleFont(L"Segoe UI", 14.0f, FontStyleBold, UnitPoint);
        SolidBrush whiteBr(CLR_TEXT);
        g.DrawString(L"P U L S E", -1, &titleFont, PointF(68, 15), &whiteBr);

        // ── Main content area ──
        // Section labels
        Font labelFont(L"Segoe UI", 11.0f, FontStyleBold, UnitPoint);
        Font subFont(L"Segoe UI", 10.0f, FontStyleRegular, UnitPoint);
        SolidBrush accentBr(CLR_ACCENT);

        // Hit Reg label
        g.DrawString(L"Hit Registration", -1, &labelFont, PointF(30, 115), &whiteBr);
        // Hit Reg value (dynamic, updated separately - drawn here too)
        wchar_t valBuf[8];
        swprintf_s(valBuf, L"%.0f", g_app.hitReg);
        StringFormat rightSF; rightSF.SetAlignment(StringAlignmentFar);
        RectF valRc(30.0f, 115.0f, (REAL)(W - 60), 20.0f);
        g.DrawString(valBuf, -1, &labelFont, valRc, &rightSF, &accentBr);

        // KB Reduction label
        g.DrawString(L"Knockback Reduction", -1, &labelFont, PointF(30, 205), &whiteBr);
        swprintf_s(valBuf, L"%.0f", g_app.kbReduction);
        RectF valRc2(30.0f, 205.0f, (REAL)(W - 60), 20.0f);
        g.DrawString(valBuf, -1, &labelFont, valRc2, &rightSF, &accentBr);

        // Connection type label
        SolidBrush mutedBr(CLR_MUTED);
        g.DrawString(L"Connection Type:", -1, &subFont, PointF(30, 305), &whiteBr);

        // ── Footer ──
        REAL footerY = (REAL)(H - FOOTER_H);
        // Separator
        Pen sepPen(CLR_CTRL_BG, 1.0f);
        g.DrawLine(&sepPen, 0, (int)footerY, W, (int)footerY);
        // Footer bg
        SolidBrush footerBr(CLR_FOOTER);
        RectF footerRc(0, footerY + 1, (REAL)W, (REAL)FOOTER_H);
        // (rounded bottom corners)
        GraphicsPath footerPath;
        footerPath.AddRectangle(footerRc);
        g.FillPath(&footerBr, &footerPath);

        // Status dot
        bool optimized = g_app.isOptimized;
        SolidBrush dotBr(optimized ? CLR_ACCENT : CLR_MUTED);
        g.FillEllipse(&dotBr, 20, (int)footerY + (FOOTER_H - 8) / 2, 8, 8);

        // Status text
        Font statusFont(L"Segoe UI", 9.5f, FontStyleRegular, UnitPoint);
        RectF statusRc(35.0f, footerY + 2.0f, (REAL)(W - 280), (REAL)(FOOTER_H - 4));
        StringFormat statusSF; statusSF.SetAlignment(StringAlignmentNear); statusSF.SetLineAlignment(StringAlignmentCenter);
        statusSF.SetTrimming(StringTrimmingEllipsisCharacter);
        g.DrawString(g_app.statusText.c_str(), -1, &statusFont, statusRc, &statusSF, &mutedBr);
    }

    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// ─── Layout helper ───────────────────────────────────────────────────────────
static void LayoutControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Window buttons (top-right)
    MoveWindow(g_app.hwndBtnClose,    W - 45,  10, 30, 30, TRUE);
    MoveWindow(g_app.hwndBtnMinimize, W - 80,  10, 30, 30, TRUE);

    // Content area: top=TITLEBAR_H, bottom=H-FOOTER_H
    int contentTop = TITLEBAR_H;
    int contentH   = H - TITLEBAR_H - FOOTER_H;

    // Smart Packets toggle (top of content)
    int chkY = contentTop + 15;
    int chkW = 200;
    MoveWindow(g_app.hwndChkSmart, (W - chkW) / 2, chkY, chkW, 28, TRUE);

    // Hit Reg slider
    int sliderY = contentTop + 60;
    MoveWindow(g_app.hwndSldHitReg, 30, sliderY + 30, W - 60, 30, TRUE);

    // KB Reduction slider
    int sliderY2 = contentTop + 155;
    MoveWindow(g_app.hwndSldKbRed, 30, sliderY2 + 30, W - 60, 30, TRUE);

    // Connection type combo
    MoveWindow(g_app.hwndCmbConn, 190, contentTop + 255, W - 220, 36, TRUE);

    // Footer buttons
    int footerY = H - FOOTER_H;
    int btnW = 120, btnH = 36;
    MoveWindow(g_app.hwndBtnRun,     W - 30 - btnW,          footerY + (FOOTER_H - btnH) / 2, btnW, btnH, TRUE);
    MoveWindow(g_app.hwndBtnRestore, W - 30 - btnW - 12 - 100, footerY + (FOOTER_H - btnH) / 2, 100, btnH, TRUE);

    // Hidden value labels (not rendered visibly, GDI+ handles drawing)
    MoveWindow(g_app.hwndHitVal, 0, 0, 0, 0, FALSE);
    MoveWindow(g_app.hwndKbVal,  0, 0, 0, 0, FALSE);
}

// ─── WndProc ─────────────────────────────────────────────────────────────────
static HWND g_hwndBtnClose    = nullptr;
static HWND g_hwndBtnMinimize = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        // Transparent layered window with DWM blur would go here
        // instead we use GDI+ painted borderless window
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);

        // Close button
        g_hwndBtnClose = CreateWindowExW(0, L"BUTTON", L"×",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 30, 30, hwnd, (HMENU)ID_BTN_CLOSE, nullptr, nullptr);

        // Minimize button
        g_hwndBtnMinimize = CreateWindowExW(0, L"BUTTON", L"−",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 30, 30, hwnd, (HMENU)ID_BTN_MINIMIZE, nullptr, nullptr);

        // Smart Packets checkbox
        g_app.hwndChkSmart = CreateWindowExW(0, L"BUTTON", L"Smart Packets",
            WS_CHILD | WS_VISIBLE | BS_CHECKBOX | BS_AUTOCHECKBOX,
            0, 0, 200, 28, hwnd, (HMENU)ID_CHK_SMART, nullptr, nullptr);
        SendMessageW(g_app.hwndChkSmart, BM_SETCHECK, g_app.smartPackets ? BST_CHECKED : BST_UNCHECKED, 0);
        g_origChkProc = (WNDPROC)SetWindowLongPtrW(g_app.hwndChkSmart, GWLP_WNDPROC, (LONG_PTR)ChkSubclassProc);

        // Hit Reg slider
        g_app.hwndSldHitReg = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            0, 0, 200, 30, hwnd, (HMENU)ID_SLD_HITREG, nullptr, nullptr);
        SendMessageW(g_app.hwndSldHitReg, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(g_app.hwndSldHitReg, TBM_SETPOS, TRUE, (LPARAM)(int)g_app.hitReg);

        // KB Reduction slider
        g_app.hwndSldKbRed = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            0, 0, 200, 30, hwnd, (HMENU)ID_SLD_KBRED, nullptr, nullptr);
        SendMessageW(g_app.hwndSldKbRed, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(g_app.hwndSldKbRed, TBM_SETPOS, TRUE, (LPARAM)(int)g_app.kbReduction);

        // Connection type combo
        g_app.hwndCmbConn = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            0, 0, 200, 120, hwnd, (HMENU)ID_CMB_CONNTYPE, nullptr, nullptr);
        SendMessageW(g_app.hwndCmbConn, CB_ADDSTRING, 0, (LPARAM)L"Fiber");
        SendMessageW(g_app.hwndCmbConn, CB_ADDSTRING, 0, (LPARAM)L"Wi-Fi");
        SendMessageW(g_app.hwndCmbConn, CB_SETCURSEL, 0, 0);

        // Run button
        g_app.hwndBtnRun = CreateWindowExW(0, L"BUTTON", L"RUN PULSE",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 120, 36, hwnd, (HMENU)ID_BTN_RUN, nullptr, nullptr);

        // Restore button (hidden initially)
        g_app.hwndBtnRestore = CreateWindowExW(0, L"BUTTON", L"RESTORE",
            WS_CHILD | BS_OWNERDRAW,
            0, 0, 100, 36, hwnd, (HMENU)ID_BTN_RESTORE, nullptr, nullptr);

        // Hidden value labels
        g_app.hwndHitVal = CreateWindowExW(0, L"STATIC", L"100", WS_CHILD, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        g_app.hwndKbVal  = CreateWindowExW(0, L"STATIC", L"100", WS_CHILD, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);

        g_app.hwndBtnClose    = g_hwndBtnClose;
        g_app.hwndBtnMinimize = g_hwndBtnMinimize;

        LayoutControls(hwnd);

        // Detect network connection type
        NetworkAdapterInfo adapter = OptimizationEngine::Instance().GetActiveInterface();
        if (adapter.isValid) {
            g_app.connType = adapter.isWireless ? L"Wi-Fi" : L"Fiber";
            g_app.statusText = (adapter.isWireless ? L"Detected active wireless connection: " : L"Detected active wired connection: ") + adapter.name;
            int idx = (g_app.connType == L"Wi-Fi") ? 1 : 0;
            SendMessageW(g_app.hwndCmbConn, CB_SETCURSEL, idx, 0);
        }
        UpdateControlStates();
        return 0;
    }

    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1; // handled in WM_PAINT

    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;

    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            if (pt.y < TITLEBAR_H) return HTCAPTION;
        }
        return hit;
    }

    case WM_UPDATESTATUS: {
        wchar_t* msg_text = (wchar_t*)lp;
        if (msg_text) {
            g_app.statusText = msg_text;
            delete[] msg_text;
            UpdateControlStates();
        }
        return 0;
    }

    case WM_HSCROLL: {
        HWND sliderHwnd = (HWND)lp;
        if (sliderHwnd == g_app.hwndSldHitReg) {
            g_app.hitReg = (double)SendMessageW(g_app.hwndSldHitReg, TBM_GETPOS, 0, 0);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (sliderHwnd == g_app.hwndSldKbRed) {
            g_app.kbReduction = (double)SendMessageW(g_app.hwndSldKbRed, TBM_GETPOS, 0, 0);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        DrawDarkButton(dis);
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == ID_BTN_CLOSE) {
            // Restore before closing if optimized
            if (g_app.isOptimized) {
                OptimizationEngine::Instance().RestoreAll([](const std::wstring&) {});
            }
            DestroyWindow(hwnd);
        } else if (id == ID_BTN_MINIMIZE) {
            ShowWindow(hwnd, SW_MINIMIZE);
        } else if (id == ID_BTN_RUN && !g_app.isProcessing && !g_app.isOptimized) {
            g_app.isProcessing = true;
            g_app.statusText = L"Starting optimization engine...";
            UpdateControlStates();

            bool   smartPkt = g_app.smartPackets;
            double hitReg   = g_app.hitReg;
            double kbRed    = g_app.kbReduction;
            std::wstring conn = g_app.connType;

            std::thread([smartPkt, hitReg, kbRed, conn]() {
                bool ok = OptimizationEngine::Instance().ApplyOptimizations(
                    smartPkt, hitReg, kbRed, conn,
                    [](const std::wstring& s) { PostStatus(s); }
                );
                g_app.isOptimized  = ok;
                g_app.isProcessing = false;
                PostStatus(ok ? L"Pulse Engine: Active & Optimized." : L"Optimization failed.");
            }).detach();

        } else if (id == ID_BTN_RESTORE && g_app.isOptimized && !g_app.isProcessing) {
            g_app.isProcessing = true;
            g_app.statusText = L"Restoring defaults...";
            UpdateControlStates();

            std::thread([]() {
                OptimizationEngine::Instance().RestoreAll(
                    [](const std::wstring& s) { PostStatus(s); }
                );
                g_app.isOptimized  = false;
                g_app.isProcessing = false;
                PostStatus(L"Pulse Engine: Cleaned and Restored.");
            }).detach();

        } else if (id == ID_CHK_SMART) {
            g_app.smartPackets = (SendMessageW(g_app.hwndChkSmart, BM_GETCHECK, 0, 0) == BST_CHECKED);
            InvalidateRect(g_app.hwndChkSmart, nullptr, FALSE);
        } else if (id == ID_CMB_CONNTYPE && HIWORD(wp) == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_app.hwndCmbConn, CB_GETCURSEL, 0, 0);
            g_app.connType = (sel == 1) ? L"Wi-Fi" : L"Fiber";
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── WinMain ─────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // Request admin (app.manifest handles UAC)
    GdiplusStartupInput gdiplusInput;
    GdiplusStartup(&g_app.gdiplusToken, &gdiplusInput, nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"PulseNetworkOptimizer";
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    // Center on screen
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int x = (scrW - WND_WIDTH)  / 2;
    int y = (scrH - WND_HEIGHT) / 2;

    g_app.hwndMain = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_APPWINDOW,
        L"PulseNetworkOptimizer",
        L"Pulse Network Optimizer",
        WS_POPUP,
        x, y, WND_WIDTH, WND_HEIGHT,
        nullptr, nullptr, hInst, nullptr
    );

    // Make layered window opaque with color-key for per-pixel alpha
    SetLayeredWindowAttributes(g_app.hwndMain, 0, 255, LWA_ALPHA);

    // DWM corner rounding (Windows 11)
    DWMWINDOWATTRIBUTE attr = DWMWA_WINDOW_CORNER_PREFERENCE;
    DWM_WINDOW_CORNER_PREFERENCE val = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_app.hwndMain, attr, &val, sizeof(val));

    ShowWindow(g_app.hwndMain, SW_SHOW);
    UpdateWindow(g_app.hwndMain);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    GdiplusShutdown(g_app.gdiplusToken);
    return (int)message.wParam;
}
