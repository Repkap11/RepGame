#pragma once

#include "common/Platforms.hpp"

enum WorldDrawQuality { LOW, MEDIUM, HIGH, X_HIGH, LAST };
// Every quality above LOW needs the frame buffer; platforms without one
// (WASM, Android) only cycle through the levels they actually render.
constexpr int WORLD_DRAW_QUALITY_COUNT = SUPPORTS_FRAME_BUFFER ? WorldDrawQuality::LAST : WorldDrawQuality::MEDIUM;
inline WorldDrawQuality world_draw_quality_step( WorldDrawQuality q, int delta ) {
    return static_cast<WorldDrawQuality>( ( ( q + delta ) % WORLD_DRAW_QUALITY_COUNT + WORLD_DRAW_QUALITY_COUNT ) % WORLD_DRAW_QUALITY_COUNT );
}
