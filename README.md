# TaskbarMagnify v0.4

A small native Windows 11 utility that adds a macOS Dock-style magnification effect to the taskbar. It does not change any files, settings, or windows. It only draws a visual effect over the taskbar while the pointer is on it.

## How it works
- A low-level mouse hook wakes the app only when the pointer moves. There is no polling.
- On first hover the taskbar pixels are copied, and the taskbar's icon positions are read through UI Automation.
- While the pointer is on the taskbar, icons near the cursor grow upward, and neighbours shift to make room, like the macOS Dock.
- A transparent layered window draws the result. It is click-through, so clicks reach the real taskbar.
- Tray icon: right-click for "Magnification" (on/off) and "Quit".

## Settings
`settings.ini` next to the EXE, read once at startup:
- `max_scale_percent`: 100 to 300, default 160
- `falloff_px`: 10 to 300, default 60 (how far the effect spreads)

## Status
- Layout and math are unit-tested on Linux (`tests/test_math.cpp`, all passing).
- The Windows code has not yet been compiled or run on Windows. Check the GitHub Actions build first.
- Performance (CPU, memory, startup, idle use, animation smoothness) has not been measured.

## Known limitations
- Icon backgrounds are detected by color. Icons whose background is not a single solid color may show a faint edge.
- Only horizontal taskbars and the default taskbar alignment are supported. Auto-hide is not handled.
- Tray-area buttons may also be magnified.
- Not started automatically at login yet.
- The app is not code-signed, so Windows SmartScreen will warn on first run.

## Build
Windows, Visual Studio 2022 Build Tools with CMake:

    cmake -B build -A x64
    cmake --build build --config Release

GitHub Actions builds the EXE automatically on each push (see `.github/workflows/build.yml`).

## Run the math tests (any OS with a C++17 compiler)

    g++ -std=c++17 -Isrc tests/test_math.cpp -o test_math && ./test_math
