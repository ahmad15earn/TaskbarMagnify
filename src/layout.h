// Pure icon layout. Works in taskbar-relative pixels, so it is unit-testable without Win32.
#pragma once
#include <cmath>
#include <cstddef>
#include "magnify.h"

struct LayoutSlot {
    int l = 0, t = 0, r = 0, b = 0;     // original rect (relative to taskbar top-left)
    double scale = 1.0;                  // applied scale
    int dl = 0, dt = 0, dr = 0, db = 0;  // magnified rect (relative to taskbar top-left)
};

// Slots must be sorted left to right and must not overlap.
// W, H: taskbar size in pixels. cursorX: pointer x relative to taskbar left.
inline void LayoutIcons(LayoutSlot* s, std::size_t n, double cursorX,
                        int W, int H, const MagnifySettings& cfg) {
    if (n == 0) return;

    // 1. Scale per icon, never taller than the taskbar.
    for (std::size_t i = 0; i < n; ++i) {
        double cx = (s[i].l + s[i].r) / 2.0;
        double sc = MagnifyScale(cursorX - cx, cfg);
        double hIcon = s[i].b - s[i].t;
        double fit = hIcon > 0 ? H / hIcon : 1.0;
        if (sc > fit) sc = fit;
        if (sc < 1.0) sc = 1.0;
        s[i].scale = sc;
    }

    // 2. Lay out left to right, keeping original gaps.
    const double oldCenter = (s[0].l + s[n - 1].r) / 2.0;
    double x = s[0].l;
    for (std::size_t i = 0; i < n; ++i) {
        double nw = (s[i].r - s[i].l) * s[i].scale;
        s[i].dl = static_cast<int>(std::lround(x));
        s[i].dr = static_cast<int>(std::lround(x + nw));
        x += nw;
        if (i + 1 < n) x += s[i + 1].l - s[i].r;
    }

    // 3. Keep the group centered, but inside the taskbar.
    const double newCenter = (s[0].dl + x) / 2.0;
    int shift = static_cast<int>(std::lround(oldCenter - newCenter));
    if (s[0].dl + shift < 0) shift = -s[0].dl;
    if (s[n - 1].dr + shift > W) shift = W - s[n - 1].dr;

    // 4. Apply shift and center vertically.
    for (std::size_t i = 0; i < n; ++i) {
        s[i].dl += shift;
        s[i].dr += shift;
        int hh = static_cast<int>(std::lround((s[i].b - s[i].t) * s[i].scale));
        s[i].dt = (H - hh) / 2;
        s[i].db = s[i].dt + hh;
    }
}
