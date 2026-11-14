// Taskbar icon detection through UI Automation. Called on pointer-enter only, never per frame.
#pragma once
#include <windows.h>
#include <vector>
#include "layout.h"

// Fills `out` with taskbar button rects relative to the taskbar's top-left, sorted left to right.
bool DetectTaskbarIcons(HWND taskbar, const RECT& tbBounds, std::vector<LayoutSlot>& out);
