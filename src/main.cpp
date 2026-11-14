// TaskbarMagnify v0.2
// Event-driven: a low-level mouse hook posts a message only when the pointer moves.
// All UI work runs on the main thread inside the message loop.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <vector>
#include "taskbar.h"
#include "magnify.h"
#include "layout.h"
#include "icons.h"

#define WM_APP_POINTER (WM_APP + 1)
static const wchar_t* kOverlayClass = L"TM_Overlay";
static const wchar_t* kMsgClass     = L"TM_Msg";
static const DWORD kDetectCooldownMs = 1000;

static MagnifySettings g_settings{};
static std::vector<LayoutSlot> g_icons;
static HWND g_msgWnd = nullptr;
static HWND g_overlay = nullptr;
static HHOOK g_mouseHook = nullptr;
static HANDLE g_mutex = nullptr;
static UINT g_taskbarCreatedMsg = 0;

// Hook -> message window handoff. The hook never does heavy work.
static volatile LONG g_pendingPost = 0;
static POINT g_lastPt{};

// Overlay state
static TaskbarInfo g_tb{};
static bool g_active = false;
static bool g_haveDetect = false;
static DWORD g_lastDetect = 0;
static int g_W = 0, g_H = 0;
static HDC g_bgDC = nullptr, g_backDC = nullptr;
static HBITMAP g_bgBmp = nullptr, g_backBmp = nullptr;
static HGDIOBJ g_bgOld = nullptr, g_backOld = nullptr;
static HBRUSH g_fillBrush = nullptr;

static void LoadSettings() {
    // Read once at startup. No disk access during runtime.
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
    if (g_bgDC)   { if (g_bgOld) SelectObject(g_bgDC, g_bgOld); DeleteDC(g_bgDC); g_bgDC = nullptr; }
    if (g_backDC) { if (g_backOld) SelectObject(g_backDC, g_backOld); DeleteDC(g_backDC); g_backDC = nullptr; }
    if (g_bgBmp)   { DeleteObject(g_bgBmp);   g_bgBmp = nullptr; }
    if (g_backBmp) { DeleteObject(g_backBmp); g_backBmp = nullptr; }
    if (g_fillBrush) { DeleteObject(g_fillBrush); g_fillBrush = nullptr; }
    g_W = g_H = 0;
}

// Allocates the two back buffers. Only reallocates when the taskbar size changes.
static bool EnsureBuffers(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (g_bgDC && w == g_W && h == g_H) return true;
    FreeBuffers();
    HDC screen = GetDC(nullptr);
    g_bgDC   = CreateCompatibleDC(screen);
    g_backDC = CreateCompatibleDC(screen);
    g_bgBmp   = CreateCompatibleBitmap(screen, w, h);
    g_backBmp = CreateCompatibleBitmap(screen, w, h);
    ReleaseDC(nullptr, screen);
    if (!g_bgDC || !g_backDC || !g_bgBmp || !g_backBmp) { FreeBuffers(); return false; }
    g_bgOld   = SelectObject(g_bgDC, g_bgBmp);
    g_backOld = SelectObject(g_backDC, g_backBmp);
    g_W = w; g_H = h;
    return true;
}

// Copies the taskbar pixels while the overlay is hidden.
static void CaptureTaskbar() {
    HDC screen = GetDC(nullptr);
    BitBlt(g_bgDC, 0, 0, g_W, g_H, screen, g_tb.bounds.left, g_tb.bounds.top, SRCCOPY);
    ReleaseDC(nullptr, screen);
    if (g_fillBrush) DeleteObject(g_fillBrush);
    // Taskbar background is a solid color, so sample a corner for the erase fill.
    g_fillBrush = CreateSolidBrush(GetPixel(g_bgDC, 1, 1));
}

static void RenderFrame() {
    if (!g_bgDC || !g_backDC || !g_fillBrush) return;
    BitBlt(g_backDC, 0, 0, g_W, g_H, g_bgDC, 0, 0, SRCCOPY);
    for (const auto& ic : g_icons) {          // erase original icons
        RECT r{ ic.l, ic.t, ic.r, ic.b };
        FillRect(g_backDC, &r, g_fillBrush);
    }
    SetStretchBltMode(g_backDC, HALFTONE);
    SetBrushOrgEx(g_backDC, 0, 0, nullptr);
    for (const auto& ic : g_icons) {          // draw magnified icons
        StretchBlt(g_backDC, ic.dl, ic.dt, ic.dr - ic.dl, ic.db - ic.dt,
                   g_bgDC, ic.l, ic.t, ic.r - ic.l, ic.b - ic.t, SRCCOPY);
    }
}

static void HideOverlay() {
    if (g_active) {
        g_active = false;
        ShowWindow(g_overlay, SW_HIDE);
    }
}

static void OnPointer(POINT p) {
    TaskbarInfo tb{};
    bool found = FindPrimaryTaskbar(tb);
    bool inside = found && tb.horizontal && PtInRect(&tb.bounds, p);
    if (!inside) { HideOverlay(); return; }
    g_tb = tb;

    int w = g_tb.bounds.right - g_tb.bounds.left;
    int h = g_tb.bounds.bottom - g_tb.bounds.top;

    if (!g_active) {
        if (!EnsureBuffers(w, h)) return;
        DWORD now = GetTickCount();
        if (!g_haveDetect || now - g_lastDetect >= kDetectCooldownMs) {
            DetectTaskbarIcons(g_tb.hwnd, g_tb.bounds, g_icons);
            g_lastDetect = now;
            g_haveDetect = true;
        }
        if (g_icons.empty()) return;          // nothing to magnify
        CaptureTaskbar();
        LayoutIcons(g_icons.data(), g_icons.size(), p.x - g_tb.bounds.left, g_W, g_H, g_settings);
        g_active = true;
        SetWindowPos(g_overlay, HWND_TOPMOST, g_tb.bounds.left, g_tb.bounds.top, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }

    LayoutIcons(g_icons.data(), g_icons.size(), p.x - g_tb.bounds.left, g_W, g_H, g_settings);
    InvalidateRect(g_overlay, nullptr, FALSE);
}

static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == WM_MOUSEMOVE) {
        g_lastPt = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp)->pt;
        if (InterlockedCompareExchange(&g_pendingPost, 1, 0) == 0)
            PostMessageW(g_msgWnd, WM_APP_POINTER, 0, 0);
    }
    return CallNextHookEx(g_mouseHook, code, wp, lp);
}

static LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_APP_POINTER) {
        InterlockedExchange(&g_pendingPost, 0);
        OnPointer(g_lastPt);
        return 0;
    }
    if (msg == g_taskbarCreatedMsg) {         // Explorer restarted: taskbar window is new
        HideOverlay();
        g_icons.clear();
        g_haveDetect = false;
        FindPrimaryTaskbar(g_tb);
        return 0;
    }
    if (msg == WM_ENDSESSION || msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RenderFrame();
        BitBlt(hdc, 0, 0, g_W, g_H, g_backDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void Cleanup() {
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
    HideOverlay();
    if (g_overlay) { DestroyWindow(g_overlay); g_overlay = nullptr; }
    if (g_msgWnd)  { DestroyWindow(g_msgWnd);  g_msgWnd = nullptr; }
    FreeBuffers();
    g_icons.clear();
    CoUninitialize();
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); g_mutex = nullptr; }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
    wc.lpfnWndProc = OverlayProc;
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);

    g_msgWnd = CreateWindowExW(0, kMsgClass, L"", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, hInst, nullptr);
    g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kOverlayClass, L"", WS_POPUP, 0, 0, 1, 1,
                                nullptr, nullptr, hInst, nullptr);
    if (!g_msgWnd || !g_overlay) { Cleanup(); return 1; }
    SetLayeredWindowAttributes(g_overlay, 0, 255, LWA_ALPHA);

    FindPrimaryTaskbar(g_tb);                 // may fail; retried on every pointer event
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, hInst, 0);
    if (!g_mouseHook) { Cleanup(); return 1; }

    MSG msg;                                  // zero CPU while idle
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Cleanup();
    return 0;
}
