// Pure icon layout in taskbar-relative pixels. No Win32 dependencies, so it is unit-testable.
#pragma once
#include <cmath>
#include <cstddef>
#include "magnify.h"

struct LayoutSlot {
    int l = 0, t = 0, r = 0, b = 0;      // original rect (taskbar-relative)
    double scale = 1.0;                   // applied scale
    int dl = 0, dt = 0, dr = 0, db = 0;   // magnified rect (taskbar-relative; dt may be < 0)
};

// Slots must be sorted left to right and must not overlap.
// Icons grow upward from their original bottom edge, like the macOS Dock.
inline void LayoutIcons(LayoutSlot* s, std::size_t n, double cursorX, int W,
                        const MagnifySettings& cfg) {
    if (n == 0) return;

    for (std::size_t i = 0; i < n; ++i) {
        double cx = (s[i].l + s[i].r) / 2.0;
        double sc = MagnifyScale(cursorX - cx, cfg);
        s[i].scale = sc < 1.0 ? 1.0 : sc;
    }

    const double oldCenter = (s[0].l + s[n - 1].r) / 2.0;
    double x = s[0].l;
    for (std::size_t i = 0; i < n; ++i) {
        double nw = (s[i].r - s[i].l) * s[i].scale;
        s[i].dl = static_cast<int>(std::lround(x));
        s[i].dr = static_cast<int>(std::lround(x + nw));
        x += nw;
        if (i + 1 < n) x += s[i + 1].l - s[i].r;
    }

    const double newCenter = (s[0].dl + x) / 2.0;
    int shift = static_cast<int>(std::lround(oldCenter - newCenter));
    if (s[0].dl + shift < 0) shift = -s[0].dl;
    if (s[n - 1].dr + shift > W) shift = W - s[n - 1].dr;

    for (std::size_t i = 0; i < n; ++i) {
        s[i].dl += shift;
        s[i].dr += shift;
        int hh = static_cast<int>(std::lround((s[i].b - s[i].t) * s[i].scale));
        s[i].db = s[i].b;
        s[i].dt = s[i].db - hh;
    }
}
