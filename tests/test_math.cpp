// Unit tests for magnification math and layout. Builds and runs on any platform.
#include <cstdio>
#include <cmath>
#include "magnify.h"
#include "layout.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main() {
    MagnifySettings cfg;  // 1.6x peak, 60px falloff

    CHECK(std::fabs(MagnifyScale(0, cfg) - 1.6) < 1e-9);
    CHECK(std::fabs(MagnifyScale(1000, cfg) - 1.0) < 1e-6);
    CHECK(MagnifyScale(30, cfg) > MagnifyScale(60, cfg));
    CHECK(std::fabs(MagnifyScale(-45, cfg) - MagnifyScale(45, cfg)) < 1e-9);

    // Five 40x40 icons on a 1000px-wide taskbar, cursor over the middle icon.
    LayoutSlot s[5];
    for (int i = 0; i < 5; ++i) {
        s[i].l = 100 + i * 48; s[i].r = s[i].l + 40;
        s[i].t = 4;            s[i].b = 44;
    }
    const int W = 1000;
    LayoutIcons(s, 5, 216.0, W, cfg);   // middle icon center = 216

    CHECK(std::fabs(s[2].scale - 1.6) < 1e-6);                 // peak at cursor
    CHECK(s[2].scale >= s[1].scale && s[2].scale >= s[3].scale);
    for (int i = 0; i < 5; ++i) {
        CHECK(s[i].scale >= 1.0);
        CHECK(s[i].dl >= 0 && s[i].dr <= W);
        CHECK(s[i].db == s[i].b);                               // grows upward from baseline
        CHECK(s[i].dt < s[i].db);
        if (i > 0) CHECK(s[i].dl >= s[i - 1].dr);               // no overlap
    }
    // Magnified height matches scale.
    CHECK(s[2].db - s[2].dt == (int)std::lround(40 * 1.6));

    // Icons at the far left stay inside the taskbar.
    LayoutSlot e[2] = { {0, 4, 40, 44}, {48, 4, 88, 44} };
    LayoutIcons(e, 2, 0.0, 200, cfg);
    CHECK(e[0].dl >= 0);

    if (g_fail == 0) std::printf("all tests passed\n");
    return g_fail ? 1 : 0;
}
