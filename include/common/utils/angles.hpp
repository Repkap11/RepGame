#pragma once

// Degree wrapping shared by the tick, VR input, render interpolation and
// mob yaw math.

// Wraps into [0, 360).
inline float wrap_degrees_360( float deg ) {
    while ( deg >= 360.0f ) {
        deg -= 360.0f;
    }
    while ( deg < 0.0f ) {
        deg += 360.0f;
    }
    return deg;
}

// Wraps into [-180, 180] — shortest-path deltas (mob yaw, angle interp).
inline float wrap_degrees_180( float deg ) {
    while ( deg > 180.0f ) {
        deg -= 360.0f;
    }
    while ( deg < -180.0f ) {
        deg += 360.0f;
    }
    return deg;
}
