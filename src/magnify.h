// Pure magnification math. No Win32 dependencies, so it can be unit-tested anywhere.
#pragma once
#include <cmath>

struct MagnifySettings {
    double maxScale = 1.6;   // peak scale at the cursor
    double sigma    = 60.0;  // falloff radius in pixels
};

// Scale factor (>= 1.0) for an icon whose center is `distance` pixels from the cursor.
inline double MagnifyScale(double distance, const MagnifySettings& s) {
    double g = std::exp(-(distance * distance) / (2.0 * s.sigma * s.sigma));
    return 1.0 + (s.maxScale - 1.0) * g;
}
