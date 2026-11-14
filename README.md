# TaskbarMagnify v0.2

Native C++ / Win32 utility that adds a macOS-style magnification effect to the Windows 11 taskbar.

## Status
- Source complete for: taskbar detection, icon detection (UI Automation), event-driven mouse hook, layout and magnification math, overlay rendering, Explorer-restart recovery, clean shutdown.
- Math and layout are unit-tested and pass on Linux (`tests/test_math.cpp`).
- The Windows code (`main.cpp`, `icons.cpp`, `taskbar.cpp`) has NOT been compiled or run on Windows yet.
- Performance (CPU, memory, startup, idle, animation smoothness) is NOT measured yet.

## Known limitations
- Magnification is capped at the taskbar's height, so the icon grows only within the bar. No overflow above the taskbar yet.
- Icon backgrounds are erased with one solid color sampled from the taskbar. Acrylic or gradient backgrounds may show a faint patch.
- Only horizontal taskbars are supported. Auto-hide taskbars are not handled.
- UI Automation may also report tray-area buttons, which get magnified too.
- Each pointer event calls `FindWindow` for the taskbar. This is cheap, but it is a candidate for optimization after measurement.
- No tray icon or quit UI. End the process from Task Manager, or log out or shut down (the app exits on session end).

## Build (Windows, Visual Studio 2022 Build Tools with CMake)
    cmake -B build -A x64
    cmake --build build --config Release

## Run tests (any OS with a C++17 compiler)
    g++ -std=c++17 -Isrc tests/test_math.cpp -o test_math && ./test_math

## Settings
`settings.ini` next to the EXE, read once at startup:
- `max_scale_percent` (100 to 300, default 160)
- `falloff_px` (10 to 300, default 60)
