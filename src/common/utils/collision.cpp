#include "common/utils/collision.hpp"
#include "common/RepGame.hpp"

#include <cmath>

// Player collision AABB half-extents. The camera position is the eye; the body
// is centered on (pos.x, pos.z) and spans PLAYER_HEIGHT tall with the eye
// EYE_POSITION_OFFSET above the body center.
static const float HALF_WIDTH = PLAYER_WIDTH / 2.0f;
static const float HALF_HEIGHT = PLAYER_HEIGHT / 2.0f;
// Small skin kept between the player and block faces so floating-point error
// never causes a re-collision on the next substep.
static const float SKIN = 1e-4f;
// Sentinel used when no clamping block has been found yet.
static const float NO_CLAMP_POS = 1e9f;
static const float NO_CLAMP_NEG = -1e9f;

// Build the player collision AABB from the eye position.
static void player_aabb( const glm::vec3 &eye, glm::vec3 &out_min, glm::vec3 &out_max ) {
    out_min = glm::vec3( eye.x - HALF_WIDTH, eye.y - EYE_POSITION_OFFSET - HALF_HEIGHT, eye.z - HALF_WIDTH );
    out_max = glm::vec3( eye.x + HALF_WIDTH, eye.y - EYE_POSITION_OFFSET + HALF_HEIGHT, eye.z + HALF_WIDTH );
}

// True if two AABBs overlap (strict inequalities so flush contact does not
// count as a collision).
static bool aabb_overlap( const glm::vec3 &min1, const glm::vec3 &max1, const glm::vec3 &min2, const glm::vec3 &max2 ) {
    return min1.x < max2.x && max1.x > min2.x && //
           min1.y < max2.y && max1.y > min2.y && //
           min1.z < max2.z && max1.z > min2.z;
}

// Computes the collision AABB of the block at `block_pos`. Returns false if the
// block does not collide with the player (air, non-collidable). Unloaded
// blocks are treated as solid full blocks so the player can't fall into the
// unloaded void (matches the previous collide_with_unloaded behavior).
static bool block_collision_aabb( World &world, const glm::ivec3 &block_pos, glm::vec3 &out_min, glm::vec3 &out_max ) {
    BlockState blockState = world.get_loaded_block( block_pos );
    BlockID blockID = blockState.id;
    if ( blockID >= LAST_BLOCK_ID ) {
        out_min = glm::vec3( block_pos );
        out_max = glm::vec3( block_pos ) + glm::vec3( 1.0f );
        return true;
    }
    Block *block = block_definition_get_definition( blockID );
    if ( !block->collides_with_player ) {
        return false;
    }
    if ( !block->non_full_size ) {
        out_min = glm::vec3( block_pos );
        out_max = glm::vec3( block_pos ) + glm::vec3( 1.0f );
        return true;
    }
    // Non-full-size block: apply the same horizontal rotation logic used by
    // ray_traversal's contains_pixel so partial blocks (slabs, fences, etc.)
    // collide with the correct footprint.
    short scale_x = block->scale.x;
    short scale_z = block->scale.z;
    short offset_x = block->offset.x;
    short offset_z = block->offset.z;
    const unsigned char rot = blockState.rotation;
    if ( rot == BLOCK_ROTATE_90 ) {
        scale_x = block->scale.z;
        scale_z = block->scale.x;
        offset_x = 16 - block->scale.z - block->offset.z;
        offset_z = block->offset.x;
    } else if ( rot == BLOCK_ROTATE_180 ) {
        offset_x = 16 - block->scale.x - block->offset.x;
        offset_z = 16 - block->scale.z - block->offset.z;
    } else if ( rot == BLOCK_ROTATE_270 ) {
        scale_x = block->scale.z;
        scale_z = block->scale.x;
        offset_x = block->offset.z;
        offset_z = 16 - block->scale.x - block->offset.x;
    }
    out_min.x = static_cast<float>( block_pos.x ) + PIXEL_TO_FLOAT( offset_x );
    out_max.x = out_min.x + PIXEL_TO_FLOAT( scale_x );
    out_min.z = static_cast<float>( block_pos.z ) + PIXEL_TO_FLOAT( offset_z );
    out_max.z = out_min.z + PIXEL_TO_FLOAT( scale_z );
    out_min.y = static_cast<float>( block_pos.y ) + PIXEL_TO_FLOAT( block->offset.y );
    out_max.y = out_min.y + PIXEL_TO_FLOAT( block->scale.y );
    return true;
}

// Returns true if the player AABB overlaps any solid block.
static bool aabb_collides_world( World &world, const glm::vec3 &amin, const glm::vec3 &amax ) {
    const float eps = 1e-4f;
    const int x0 = static_cast<int>( floorf( amin.x ) );
    const int x1 = static_cast<int>( floorf( amax.x - eps ) );
    const int y0 = static_cast<int>( floorf( amin.y ) );
    const int y1 = static_cast<int>( floorf( amax.y - eps ) );
    const int z0 = static_cast<int>( floorf( amin.z ) );
    const int z1 = static_cast<int>( floorf( amax.z - eps ) );
    for ( int x = x0; x <= x1; x++ ) {
        for ( int y = y0; y <= y1; y++ ) {
            for ( int z = z0; z <= z1; z++ ) {
                glm::vec3 bmin, bmax;
                if ( block_collision_aabb( world, glm::ivec3( x, y, z ), bmin, bmax ) ) {
                    if ( aabb_overlap( amin, amax, bmin, bmax ) ) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Moves the AABB by `delta` on `axis` (0=x, 1=y, 2=z) and clamps it against any
// solid block it penetrated. Sets `hit` if a block was hit, and `standing` if a
// downward Y move was blocked (player is resting on a surface).
static void resolve_axis( World &world, glm::vec3 &amin, glm::vec3 &amax, int axis, float delta, bool &hit, bool &standing ) {
    hit = false;
    if ( delta == 0.0f ) {
        return;
    }
    // Record the pre-move extent on this axis so we can tell whether a block
    // face was actually crossed by this movement (vs. already overlapping the
    // player's body from before — e.g. a wall block at body height that the
    // player is standing next to).
    const float pre_min = amin[ axis ];
    const float pre_max = amax[ axis ];

    amin[ axis ] += delta;
    amax[ axis ] += delta;

    const float eps = 1e-4f;
    const int x0 = static_cast<int>( floorf( amin.x ) );
    const int x1 = static_cast<int>( floorf( amax.x - eps ) );
    const int y0 = static_cast<int>( floorf( amin.y ) );
    const int y1 = static_cast<int>( floorf( amax.y - eps ) );
    const int z0 = static_cast<int>( floorf( amin.z ) );
    const int z1 = static_cast<int>( floorf( amax.z - eps ) );

    float best = delta > 0.0f ? NO_CLAMP_POS : NO_CLAMP_NEG;
    bool found = false;
    for ( int x = x0; x <= x1; x++ ) {
        for ( int y = y0; y <= y1; y++ ) {
            for ( int z = z0; z <= z1; z++ ) {
                glm::vec3 bmin, bmax;
                if ( !block_collision_aabb( world, glm::ivec3( x, y, z ), bmin, bmax ) ) {
                    continue;
                }
                if ( !aabb_overlap( amin, amax, bmin, bmax ) ) {
                    continue;
                }
                if ( delta > 0.0f ) {
                    // Moving +: clamp to the block's min face, but only if the
                    // player's leading edge (pre_max) actually crossed it.
                    if ( bmin[ axis ] >= pre_max && bmin[ axis ] < amax[ axis ] ) {
                        if ( bmin[ axis ] < best ) {
                            best = bmin[ axis ];
                        }
                        found = true;
                    }
                } else {
                    // Moving -: clamp to the block's max face, but only if the
                    // player's leading edge (pre_min) actually crossed it.
                    if ( bmax[ axis ] > amin[ axis ] && bmax[ axis ] <= pre_min ) {
                        if ( bmax[ axis ] > best ) {
                            best = bmax[ axis ];
                        }
                        found = true;
                    }
                }
            }
        }
    }

    if ( !found ) {
        return;
    }
    hit = true;
    const float size = amax[ axis ] - amin[ axis ];
    if ( delta > 0.0f ) {
        amax[ axis ] = best - SKIN;
        amin[ axis ] = amax[ axis ] - size;
    } else {
        amin[ axis ] = best + SKIN;
        amax[ axis ] = amin[ axis ] + size;
        if ( axis == 1 ) {
            standing = true;
        }
    }
}

// Attempts to step the player up by STEP_HEIGHT to clear a small ledge (slab,
// snow layer) that blocked a horizontal move on `axis`. On success the AABB is
// updated to the stepped-up-and-settled position; on failure it is left as the
// clamped position produced by resolve_axis.
static void try_step_up( World &world, glm::vec3 &amin, glm::vec3 &amax, int axis, float requested_delta ) {
    const glm::vec3 clamped_min = amin;
    const glm::vec3 clamped_max = amax;

    // Raise the player by STEP_HEIGHT and check there is headroom.
    glm::vec3 up_min = amin;
    glm::vec3 up_max = amax;
    up_min.y += STEP_HEIGHT;
    up_max.y += STEP_HEIGHT;
    if ( aabb_collides_world( world, up_min, up_max ) ) {
        amin = clamped_min;
        amax = clamped_max;
        return;
    }

    // Try to move forward by the full requested delta at the raised height.
    glm::vec3 test_min = up_min;
    glm::vec3 test_max = up_max;
    test_min[ axis ] += requested_delta;
    test_max[ axis ] += requested_delta;
    if ( aabb_collides_world( world, test_min, test_max ) ) {
        amin = clamped_min;
        amax = clamped_max;
        return;
    }

    // Success: accept the raised + forward position, then settle straight down
    // by STEP_HEIGHT so the player rests on the step surface.
    amin = test_min;
    amax = test_max;
    bool yhit = false;
    bool standing = false;
    resolve_axis( world, amin, amax, 1, -STEP_HEIGHT, yhit, standing );
    // resolve_axis can't write back into the caller's standing flag, so we
    // don't propagate it here; the next gravity substep will re-detect ground.
    ( void ) yhit;
    ( void ) standing;
}

int Collision::check_collides_with_block( World &world, const glm::vec3 &player, const glm::vec3 &block ) {
    // Used by block placement validation: reject if the player overlaps the
    // target block cell. Treat the target as a full 1x1x1 block (the caller
    // already gated on collides_with_player), matching the original behavior.
    const glm::ivec3 block_pos = glm::ivec3( static_cast<int>( floorf( block.x ) ), //
                                             static_cast<int>( floorf( block.y ) ), //
                                             static_cast<int>( floorf( block.z ) ) );
    const glm::vec3 block_min = glm::vec3( block_pos );
    const glm::vec3 block_max = block_min + glm::vec3( 1.0f );

    glm::vec3 pmin, pmax;
    player_aabb( player, pmin, pmax );
    return aabb_overlap( pmin, pmax, block_min, block_max ) ? 1 : 0;
}

void Collision::check_move( World &world, glm::vec3 &movement_vec, glm::vec3 &position, int *out_standing ) {
    bool standing = false;

    glm::vec3 amin, amax;
    player_aabb( position, amin, amax );
    const glm::vec3 initial_center = ( amin + amax ) * 0.5f;

    // Substep so no single resolve moves more than COLLISION_MAX_SUBSTEP on any
    // axis. This guarantees a substep can't tunnel through a 1-block-thick wall.
    const float max_delta = std::fmax( std::fabs( movement_vec.x ), std::fmax( std::fabs( movement_vec.y ), std::fabs( movement_vec.z ) ) );
    int steps = static_cast<int>( ceilf( max_delta / COLLISION_MAX_SUBSTEP ) );
    if ( steps < 1 ) {
        steps = 1;
    }
    const glm::vec3 step = movement_vec / static_cast<float>( steps );

    for ( int i = 0; i < steps; i++ ) {
        bool hit = false;
        // Resolve Y first so gravity/landing is settled before horizontal
        // moves and so step-up has a stable base to work from.
        resolve_axis( world, amin, amax, 1, step.y, hit, standing );

        // X with step-up. Only attempt step-up when the player is standing on
        // the ground this tick — otherwise jumping/falling next to a wall would
        // launch the player up and over the wall.
        glm::vec3 before = amin;
        resolve_axis( world, amin, amax, 0, step.x, hit, standing );
        if ( standing && std::fabs( amin.x - before.x ) < std::fabs( step.x ) - 1e-5f ) {
            try_step_up( world, amin, amax, 0, step.x );
        }

        // Z with step-up.
        before = amin;
        resolve_axis( world, amin, amax, 2, step.z, hit, standing );
        if ( standing && std::fabs( amin.z - before.z ) < std::fabs( step.z ) - 1e-5f ) {
            try_step_up( world, amin, amax, 2, step.z );
        }
    }

    // Write back the actual applied displacement (eye-space). The caller adds
    // this to position, so we must not modify position here.
    const glm::vec3 final_center = ( amin + amax ) * 0.5f;
    movement_vec = final_center - initial_center;
    *out_standing = standing ? 1 : 0;
}
