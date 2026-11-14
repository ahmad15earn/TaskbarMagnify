// Unit tests for magnification math and layout. Builds and runs on any platform.
#include <cstdio>
#include <cmath>
#include "magnify.h"
#include "layout.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main() {
    MagnifySettings cfg;  // defaults: 1.6x peak, 60px falloff

    CHECK(std::fabs(MagnifyScale(0, cfg) - 1.6) < 1e-9);          // peak at cursor
    CHECK(std::fabs(MagnifyScale(1000, cfg) - 1.0) < 1e-6);       // far away: no scale
    CHECK(MagnifyScale(30, cfg) > MagnifyScale(60, cfg));         // monotonic falloff
    CHECK(std::fabs(MagnifyScale(-45, cfg) - MagnifyScale(45, cfg)) < 1e-9);  // symmetric

    // Five 40x40 icons on a 1000x48 taskbar, cursor over the middle icon.
    LayoutSlot s[5];
    for (int i = 0; i < 5; ++i) {
        s[i].l = 100 + i * 48; s[i].r = s[i].l + 40;
        s[i].t = 4;            s[i].b = 44;
    }
    const int W = 1000, H = 48;
    LayoutIcons(s, 5, 216.0, W, H, cfg);   // middle icon center = 100 + 2*48 + 20 = 216

    const double fit = (double)H / 40.0;   // 1.2: icons may not grow taller than the taskbar
    for (int i = 0; i < 5; ++i) {
        CHECK(s[i].scale >= 1.0 && s[i].scale <= fit + 1e-9);
        CHECK(s[i].dl >= 0 && s[i].dr <= W);
        CHECK(s[i].dt >= 0 && s[i].db <= H);
        if (i > 0) CHECK(s[i].dl >= s[i - 1].dr);                  // no overlap
    }
    CHECK(s[2].scale >= s[1].scale && s[2].scale >= s[3].scale);  // peak at cursor
    CHECK(std::fabs(s[2].scale - fit) < 1e-9);                    // clamped to fit

    // Edge case: icons at the far left must stay inside the taskbar.
    LayoutSlot e[2] = { {0, 4, 40, 44}, {48, 4, 88, 44} };
    LayoutIcons(e, 2, 0.0, 200, H, cfg);
    CHECK(e[0].dl >= 0);

    if (g_fail == 0) std::printf("all tests passed\n");
    return g_fail ? 1 : 0;
}
