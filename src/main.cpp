// TaskbarMagnify v0.4
// Event-driven. A low-level mouse hook posts a message only when the pointer moves.
// All UI work runs on the main thread. Idle cost is a blocked GetMessage call.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "taskbar.h"
#include "magnify.h"
#include "layout.h"
#include "icons.h"

#define WM_APP_POINTER (WM_APP + 1)
#define WM_APP_TRAY    (WM_APP + 2)
#define IDM_TOGGLE 1001
#define IDM_QUIT   1002
#define TRAY_ID    1
#define IDI_APP    1

static const wchar_t* kOverlayClass = L"TM_Overlay";
static const wchar_t* kMsgClass     = L"TM_Msg";
static const DWORD kDetectCooldownMs = 1000;

static MagnifySettings g_settings{};
static std::vector<LayoutSlot> g_icons;
static HINSTANCE g_hInst = nullptr;
static HWND g_msgWnd = nullptr;
static HWND g_overlay = nullptr;
static HHOOK g_mouseHook = nullptr;
static HANDLE g_mutex = nullptr;
static UINT g_taskbarCreatedMsg = 0;
static NOTIFYICONDATAW g_nid{};
static bool g_trayAdded = false;
static bool g_enabled = true;

// Hook -> message window handoff. The hook itself does almost nothing.
static volatile LONG g_pendingPost = 0;
static POINT g_lastPt{};

// Overlay state
static TaskbarInfo g_tb{};
static bool g_active = false;
static bool g_haveDetect = false;
static DWORD g_lastDetect = 0;
static int g_W = 0, g_H = 0, g_extra = 0, g_frameH = 0;
static HDC g_bgDC = nullptr, g_frameDC = nullptr;
static HBITMAP g_bgBmp = nullptr, g_frameBmp = nullptr;
static HGDIOBJ g_bgOld = nullptr, g_frameOld = nullptr;
static uint32_t* g_frameBits = nullptr;   // 32bpp BGRA, top-down
static HBRUSH g_fillBrush = nullptr;
static uint32_t g_fillKey = 0;            // taskbar background as 0x00RRGGBB

static void LoadSettings() {
    wchar_t ini[MAX_PATH]{};
    GetModuleFileNameW(nullptr, ini, MAX_PATH);
    wchar_t* slash = wcsrchr(ini, L'\\');
    if (slash) *slash = L'\0';
    lstrcatW(ini, L"\\settings.ini");
    int maxPct = GetPrivateProfileIntW(L"magnify", L"max_scale_percent", 160, ini);
    int sigma  = GetPrivateProfileIntW(L"magnify", L"falloff_px", 60, ini);
    if (maxPct >= 100 && maxPct <= 300) g_settings.maxScale = maxPct / 100.0;
    if (sigma >= 10 && sigma <= 300)    g_settings.sigma = sigma;
}

static void FreeBuffers() {
    if (g_bgDC)    { if (g_bgOld) SelectObject(g_bgDC, g_bgOld); DeleteDC(g_bgDC); g_bgDC = nullptr; }
    if (g_frameDC) { if (g_frameOld) SelectObject(g_frameDC, g_frameOld); DeleteDC(g_frameDC); g_frameDC = nullptr; }
    if (g_bgBmp)   { DeleteObject(g_bgBmp);   g_bgBmp = nullptr; }
    if (g_frameBmp){ DeleteObject(g_frameBmp); g_frameBmp = nullptr; }
    g_frameBits = nullptr;
    if (g_fillBrush) { DeleteObject(g_fillBrush); g_fillBrush = nullptr; }
    g_W = g_H = g_extra = g_frameH = 0;
}

// Allocates buffers. Reallocates only when the size changes, never during a move.
static bool EnsureBuffers(int w, int h, int extra) {
    if (w <= 0 || h <= 0) return false;
    if (g_bgDC && w == g_W && h == g_H && extra == g_extra) return true;
    FreeBuffers();
    const int fh = h + extra;

    HDC screen = GetDC(nullptr);
    g_bgDC    = CreateCompatibleDC(screen);
    g_bgBmp   = CreateCompatibleBitmap(screen, w, h);
    g_frameDC = CreateCompatibleDC(screen);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -fh;             // negative = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    g_frameBmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (!g_bgDC || !g_bgBmp || !g_frameDC || !g_frameBmp || !bits) { FreeBuffers(); return false; }
    g_frameBits = static_cast<uint32_t*>(bits);
    g_bgOld    = SelectObject(g_bgDC, g_bgBmp);
    g_frameOld = SelectObject(g_frameDC, g_frameBmp);
    g_W = w; g_H = h; g_extra = extra; g_frameH = fh;
    return true;
}

// Copies the taskbar pixels while the overlay is still hidden.
static void CaptureTaskbar() {
    HDC screen = GetDC(nullptr);
    BitBlt(g_bgDC, 0, 0, g_W, g_H, screen, g_tb.bounds.left, g_tb.bounds.top, SRCCOPY);
    ReleaseDC(nullptr, screen);

    COLORREF c = GetPixel(g_bgDC, 1, 1);     // taskbar background is a solid color
    g_fillKey = (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
    if (g_fillBrush) DeleteObject(g_fillBrush);
    g_fillBrush = CreateSolidBrush(c);
}

// Draws the frame into the 32bpp DIB and sets per-pixel alpha:
//  - taskbar strip: fully opaque (it replaces the real taskbar pixels underneath)
//  - area above the taskbar: opaque only where a magnified icon's shape is,
//    transparent everywhere else, so the desktop shows through.
static void RenderFrame() {
    if (!g_frameDC || !g_bgDC || !g_fillBrush) return;
    const int oy = g_extra;   // strip starts at this row

    // 1. Clear the area above the strip.
    if (oy > 0) std::memset(g_frameBits, 0, sizeof(uint32_t) * (size_t)g_W * (size_t)oy);

    // 2. Taskbar strip: copy background, erase the original icons.
    BitBlt(g_frameDC, 0, oy, g_W, g_H, g_bgDC, 0, 0, SRCCOPY);
    for (const auto& ic : g_icons) {
        RECT r{ ic.l, ic.t + oy, ic.r, ic.b + oy };
        FillRect(g_frameDC, &r, g_fillBrush);
    }

    // 3. Draw the magnified icons on top.
    SetStretchBltMode(g_frameDC, HALFTONE);
    SetBrushOrgEx(g_frameDC, 0, 0, nullptr);
    for (const auto& ic : g_icons) {
        StretchBlt(g_frameDC, ic.dl, ic.dt + oy, ic.dr - ic.dl, ic.db - ic.dt,
                   g_bgDC, ic.l, ic.t, ic.r - ic.l, ic.b - ic.t, SRCCOPY);
    }

    // 4. Per-pixel alpha.
    for (int y = oy; y < g_frameH; ++y) {
        uint32_t* row = g_frameBits + (size_t)y * g_W;
        for (int x = 0; x < g_W; ++x) row[x] |= 0xFF000000u;
    }
    for (const auto& ic : g_icons) {
        int y0 = ic.dt + oy; if (y0 < 0) y0 = 0;
        int x0 = ic.dl;      if (x0 < 0) x0 = 0;
        int x1 = ic.dr;      if (x1 > g_W) x1 = g_W;
        for (int y = y0; y < oy; ++y) {
            uint32_t* row = g_frameBits + (size_t)y * g_W;
            for (int x = x0; x < x1; ++x) {
                uint32_t px = row[x] & 0xFFFFFFu;
                row[x] = (px != g_fillKey) ? (0xFF000000u | px) : 0u;
            }
        }
    }
}

static void Present() {
    POINT dst{ g_tb.bounds.left, g_tb.bounds.top - g_extra };
    SIZE  sz{ g_W, g_frameH };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(g_overlay, screen, &dst, &sz, g_frameDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

static void HideOverlay() {
    if (g_active) {
        g_active = false;
        ShowWindow(g_overlay, SW_HIDE);
    }
}

static void OnPointer(POINT p) {
    if (!g_enabled) { HideOverlay(); return; }

    TaskbarInfo tb{};
    bool found = FindPrimaryTaskbar(tb);
    bool inside = found && tb.horizontal && PtInRect(&tb.bounds, p);
    if (!inside) { HideOverlay(); return; }
    g_tb = tb;

    const int w = g_tb.bounds.right - g_tb.bounds.left;
    const int h = g_tb.bounds.bottom - g_tb.bounds.top;

    if (!g_active) {
        const int extra = (int)std::ceil(h * (g_settings.maxScale - 1.0)) + 6;
        if (!EnsureBuffers(w, h, extra)) return;

        DWORD now = GetTickCount();
        if (!g_haveDetect || now - g_lastDetect >= kDetectCooldownMs) {
            DetectTaskbarIcons(g_tb.hwnd, g_tb.bounds, g_icons);
            g_lastDetect = now;
            g_haveDetect = true;
        }
        if (g_icons.empty()) return;

        CaptureTaskbar();
        LayoutIcons(g_icons.data(), g_icons.size(), p.x - g_tb.bounds.left, g_W, g_settings);
        RenderFrame();
        Present();
        g_active = true;
        ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
        return;
    }

    LayoutIcons(g_icons.data(), g_icons.size(), p.x - g_tb.bounds.left, g_W, g_settings);
    RenderFrame();
    Present();
}

static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == WM_MOUSEMOVE) {
        g_lastPt = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp)->pt;
        if (InterlockedCompareExchange(&g_pendingPost, 1, 0) == 0)
            PostMessageW(g_msgWnd, WM_APP_POINTER, 0, 0);
    }
    return CallNextHookEx(g_mouseHook, code, wp, lp);
}

static void AddTrayIcon() {
    std::memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_msgWnd;
    g_nid.uID = TRAY_ID;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP));
    lstrcpynW(g_nid.szTip, L"TaskbarMagnify", ARRAYSIZE(g_nid.szTip));
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

static void RemoveTrayIcon() {
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayAdded = false;
    }
}

static void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : 0), IDM_TOGGLE, L"Magnification");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quit");
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(g_msgWnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_msgWnd, nullptr);
    PostMessageW(g_msgWnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_APP_POINTER) {
        InterlockedExchange(&g_pendingPost, 0);
        OnPointer(g_lastPt);
        return 0;
    }
    if (msg == WM_APP_TRAY) {
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ShowTrayMenu();
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDM_TOGGLE) {
            g_enabled = !g_enabled;
            if (!g_enabled) HideOverlay();
        } else if (LOWORD(wp) == IDM_QUIT) {
            PostQuitMessage(0);
        }
        return 0;
    }
    if (g_taskbarCreatedMsg && msg == g_taskbarCreatedMsg) {
        // Explorer restarted: the taskbar and our tray icon are both new.
        HideOverlay();
        g_icons.clear();
        g_haveDetect = false;
        FindPrimaryTaskbar(g_tb);
        g_trayAdded = false;
        AddTrayIcon();
        return 0;
    }
    if (msg == WM_ENDSESSION || msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void Cleanup() {
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
    HideOverlay();
    RemoveTrayIcon();
    if (g_overlay) { DestroyWindow(g_overlay); g_overlay = nullptr; }
    if (g_msgWnd)  { DestroyWindow(g_msgWnd);  g_msgWnd = nullptr; }
    FreeBuffers();
    g_icons.clear();
    CoUninitialize();
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); g_mutex = nullptr; }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_hInst = hInst;

    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\TaskbarMagnify.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_mutex) CloseHandle(g_mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadSettings();
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst;
    wc.lpfnWndProc = MsgProc;
    wc.lpszClassName = kMsgClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);

    // Hidden top-level window (not message-only) so it receives broadcast messages.
    g_msgWnd = CreateWindowExW(0, kMsgClass, L"", WS_POPUP, 0, 0, 0, 0,
                               nullptr, nullptr, hInst, nullptr);
    g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kOverlayClass, L"", WS_POPUP, 0, 0, 1, 1,
                                nullptr, nullptr, hInst, nullptr);
    if (!g_msgWnd || !g_overlay) { Cleanup(); return 1; }

    AddTrayIcon();
    FindPrimaryTaskbar(g_tb);
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, hInst, 0);
    if (!g_mouseHook) { Cleanup(); return 1; }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Cleanup();
    return 0;
}
