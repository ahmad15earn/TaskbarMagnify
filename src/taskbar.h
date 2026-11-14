// Taskbar detection. Finds the primary taskbar window and its bounds.
#pragma once
#include <windows.h>

struct TaskbarInfo {
    HWND hwnd = nullptr;
    RECT bounds{};
    bool horizontal = true;
};

// Returns false if the taskbar cannot be found (e.g. Explorer is restarting).
bool FindPrimaryTaskbar(TaskbarInfo& out);
