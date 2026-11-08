#pragma once

#include "common/constants.hpp"
#include <glm/glm.hpp>

// Bedrock box-uv unwrap for a mob cube. Given the cube's (u, v) skin-pixel
// corner and its (w, h, d) un-inflated size, returns the skin-pixel rect
// (x, y, w, h) for the rendered cube face `face` (a FACE_* tag from
// vd_data_player_object). The layout is the standard Bedrock strip:
//
//          [top][bottom]
//   [right][front][left][back]
//
// The unit cube's z=0 vertex set (FACE_BACK tag) is the model's -Z side —
// the mob's front — and z=1 (FACE_FRONT) is the back; x=1 (FACE_RIGHT) is
// the model's +x side, the entity's right.
//
// `mirror` (a mirrored Bedrock cube) swaps the left/right face rects; the
// caller also flips u inside every rect (fu -> 1 - fu), which is what the
// mirror flag in MobPartInstance::uv_b.z drives in mob_vertex.glsl.
static inline glm::vec4 mob_face_rect( unsigned int face, float u, float v, float w, float h, float d, bool mirror ) {
    switch ( face ) {
        case FACE_TOP:
            return glm::vec4( u + d, v, w, d );
        case FACE_BOTTOM:
            return glm::vec4( u + d + w, v, w, d );
        case FACE_RIGHT:
            return glm::vec4( mirror ? u + d + w : u, v + d, d, h );
        case FACE_LEFT:
            return glm::vec4( mirror ? u : u + d + w, v + d, d, h );
        case FACE_FRONT:
            return glm::vec4( u + 2 * d + w, v + d, w, h );
        case FACE_BACK:
        default:
            return glm::vec4( u + d, v + d, w, h );
    }
}
