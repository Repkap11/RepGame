#pragma once

// Sign text: a placed standing, wall or hanging sign block owns four
// short lines of front-side text, stored per-chunk on the client and
// authoritatively on
// the server. This header is intentionally low-level (no renderer/GL
// types, no block_definitions table) so it can be shared by the client,
// the server, and the wire protocol.

#include <array>
#include <glm/glm.hpp>
#include <map>
#include <string>

#include "common/block_ids_gen.hpp"
#include "common/chest.hpp" // Compare_I_Vec3

#define SIGN_LINES 4
// Chars per line — fits the board at the world-text glyph scale. The same
// bound is enforced by the editor, the save file, and the wire protocol.
#define SIGN_LINE_MAX 15
// Total bytes allowed in one SIGN_TEXT message / one sign record.
#define SIGN_TEXT_MAX ( SIGN_LINES * SIGN_LINE_MAX )

// One sign's front text. Lines are printable ASCII only; empty() means
// "no text" and erases the record rather than storing four blanks.
struct SignText {
    std::array<std::string, SIGN_LINES> lines;

    bool empty( ) const {
        for ( const std::string &line : lines ) {
            if ( !line.empty( ) ) {
                return false;
            }
        }
        return true;
    }
};

// Signs are identified by block id, not a Block-definition flag, so the
// server (which never initializes the GL-backed block_definitions table)
// can answer the same question. Covers every *_STANDING_SIGN and
// *_WALL_SIGN pair (the base pair is oak) plus every *_HANGING_SIGN.
// test_signs cross-checks this switch against the sign flags in the
// definition table so a new sign wood fails the test instead of being
// silently skipped.
bool block_is_sign( BlockID id );

// Wall-mounted and hanging subsets of block_is_sign — the renderer needs
// to know which board placement (wall plate, hanging plate, standing
// board+post) to draw text on.
bool block_is_wall_sign( BlockID id );
bool block_is_hanging_sign( BlockID id );

// Strip characters outside printable ASCII (32-126, incl. UTF-8 multibyte
// bytes) and truncate to SIGN_LINE_MAX. Idempotent.
void sign_sanitize_line( std::string &line );

// Sanitize every line of a SignText.
void sign_sanitize( SignText &text );

// Sign texts keyed by world block position (file + server form). The
// client-side Chunk instead keys by in-chunk block index, like chests.
typedef std::map<glm::ivec3, SignText, Compare_I_Vec3> SignTextMap;
