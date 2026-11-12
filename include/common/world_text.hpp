#pragma once

// World-space sign text: batched glyph quads laid out on each sign's
// board face, drawn with depth testing as part of the world pass.
// Reuses the FontRenderer atlas + metrics but owns its own VAO/shader —
// screen text snaps to the pixel grid, sign text floats in the world.

#include <glm/glm.hpp>

#include "common/block_types.hpp"
#include "common/renderer/shader.hpp"
#include "common/renderer/renderer.hpp"
#include "common/sign.hpp"

class FontRenderer;

// A stored sign within the world-text draw radius, for
// World::collect_signs_near.
struct WorldSign {
    glm::ivec3 pos;
    BlockState state;
    const SignText *text;
};

class WorldTextRenderer {
    const FontRenderer *font = nullptr;
    Shader shader;
    unsigned int vao_id = 0;
    unsigned int vbo_id = 0;
    unsigned int ibo_id = 0;
    int max_quads = 0;
    bool initialized = false;

    // CPU-side staging, same pattern as FontRenderer's text batch:
    // append_sign queues quads, flush uploads + draws in max_quads chunks.
    struct WorldTextVertex *batch = nullptr;
    int batch_capacity = 0;
    int batch_quads = 0;

  public:
    void init( const FontRenderer &font );
    void cleanup( );
    void begin( );
    // Queue the four front lines on `text`'s board face. sign_pos is the
    // sign cell rebased by the render origin (world pos - renderOrigin).
    void append_sign( const glm::vec3 &sign_pos, const BlockState &state, const SignText &text, const float tint[ 4 ] );
    // Fog/water uniforms mirror object_fragment's set so sign text fades
    // and underwater-tints like everything else in the world pass.
    void flush( const Renderer &renderer, const glm::mat4 &mvp, const glm::vec3 &camera_pos_rebased, const glm::vec3 &origin, float fog_near, float fog_far, float water_fog_near, float water_fog_far,
                const glm::vec3 &sky_avg_color, int underwater_wash, int opaque_fog, int water_tint_type );
};

// Signs this many blocks away (chebyshev) get their text drawn — text is
// unreadable past ~20 blocks anyway, so a small radius keeps the
// per-frame chunk scan + batch build cheap.
#define SIGN_TEXT_DRAW_RADIUS 24
