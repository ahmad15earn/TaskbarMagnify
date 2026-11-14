#include "taskbar.h"

bool FindPrimaryTaskbar(TaskbarInfo& out) {
    HWND hwnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hwnd) return false;
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return false;
    out.hwnd = hwnd;
    out.bounds = r;
    out.horizontal = (r.right - r.left) >= (r.bottom - r.top);
    return true;
}
