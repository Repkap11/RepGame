#include "common/utils/collision.hpp"
#include "common/RepGame.hpp"

#include <cmath>

// The camera position is the eye; the body is centered on (pos.x, pos.z) with
// the feet eye_height below the eye and the head (player_height - eye_height)
// above it. half_width is the AABB's horizontal half-extent.
// Small skin kept between the player and block faces so floating-point error
// never causes a re-collision on the next substep.
static const double SKIN = 1e-4;
// Sentinel used when no clamping block has been found yet.
static const double NO_CLAMP_POS = 1e9;
static const double NO_CLAMP_NEG = -1e9;

// Build the player collision AABB from the eye position.
static void player_aabb( const glm::dvec3 &eye, double player_height, double eye_height, double half_width, glm::dvec3 &out_min, glm::dvec3 &out_max ) {
    out_min = glm::dvec3( eye.x - half_width, eye.y - eye_height, eye.z - half_width );
    out_max = glm::dvec3( eye.x + half_width, out_min.y + player_height, eye.z + half_width );
}

// True if two AABBs overlap (strict inequalities so flush contact does not
// count as a collision).
static bool aabb_overlap( const glm::dvec3 &min1, const glm::dvec3 &max1, const glm::dvec3 &min2, const glm::dvec3 &max2 ) {
    return min1.x < max2.x && max1.x > min2.x && //
           min1.y < max2.y && max1.y > min2.y && //
           min1.z < max2.z && max1.z > min2.z;
}

// Computes the collision AABBs of the block at `block_pos`. Returns the box
// count (0 = no collision). Unloaded
// blocks are treated as solid full blocks so the player can't fall into the
// unloaded void (matches the previous collide_with_unloaded behavior).
// Fences emit the post plus full-height bars toward connected sides
// (fence_collision_boxes); every other type emits exactly one box.
static int block_collision_boxes( World &world, const glm::ivec3 &block_pos, glm::dvec3 mins[ FENCE_MAX_BOXES ], glm::dvec3 maxs[ FENCE_MAX_BOXES ] ) {
    BlockState blockState = world.get_loaded_block( block_pos );
    BlockID blockID = blockState.id;
    if ( blockID >= LAST_BLOCK_ID ) {
        mins[ 0 ] = glm::dvec3( block_pos );
        maxs[ 0 ] = glm::dvec3( block_pos ) + glm::dvec3( 1.0 );
        return 1;
    }
    Block *block = block_definition_get_definition( blockID );
    if ( !block->collides_with_player ) {
        return 0;
    }
    // Open fence gates don't collide — an open gate can be walked through.
    if ( block->is_fence_gate && ( blockState.rotation & BLOCK_ROTATE_GATE_OPEN ) != 0 ) {
        return 0;
    }
    if ( block->is_fence ) {
        // Build the connection mask from the 4 horizontal neighbours, the
        // same rules as the mesher in chunk.cpp phase 2; an unloaded cell
        // (LAST_BLOCK_ID) doesn't connect.
        unsigned connections = 0;
        static const int conn_dir[ 4 ][ 4 ] = { // dx, dz, connect bit, along_x
            { 1, 0, FENCE_CONNECT_POS_X, 1 },
            { -1, 0, FENCE_CONNECT_NEG_X, 1 },
            { 0, 1, FENCE_CONNECT_POS_Z, 0 },
            { 0, -1, FENCE_CONNECT_NEG_Z, 0 },
        };
        for ( int d = 0; d < 4; d++ ) {
            const BlockState neighbor = world.get_loaded_block( block_pos + glm::ivec3( conn_dir[ d ][ 0 ], 0, conn_dir[ d ][ 1 ] ) );
            if ( neighbor.id != LAST_BLOCK_ID && fence_connects_to( block, neighbor.id, neighbor.rotation, conn_dir[ d ][ 3 ] ) ) {
                connections |= conn_dir[ d ][ 2 ];
            }
        }
        BlockBox boxes[ FENCE_MAX_BOXES ];
        const int num_boxes = fence_collision_boxes( connections, boxes );
        for ( int i = 0; i < num_boxes; i++ ) {
            mins[ i ] = glm::dvec3( block_pos ) + glm::dvec3( PIXEL_TO_FLOAT( boxes[ i ].offset[ 0 ] ), PIXEL_TO_FLOAT( boxes[ i ].offset[ 1 ] ), PIXEL_TO_FLOAT( boxes[ i ].offset[ 2 ] ) );
            maxs[ i ] = mins[ i ] + glm::dvec3( PIXEL_TO_FLOAT( boxes[ i ].scale[ 0 ] ), PIXEL_TO_FLOAT( boxes[ i ].scale[ 1 ] ), PIXEL_TO_FLOAT( boxes[ i ].scale[ 2 ] ) );
        }
        return num_boxes;
    }
    glm::dvec3 &out_min = mins[ 0 ];
    glm::dvec3 &out_max = maxs[ 0 ];
    const unsigned char rot = block_yaw( block, blockState.rotation );
    const bool piston_extended = block->is_piston && blockState.current_redstone_power > 0;
    if ( !block->non_full_size && !piston_extended ) {
        out_min = glm::dvec3( block_pos );
        out_max = glm::dvec3( block_pos ) + glm::dvec3( 1.0 );
        return 1;
    }
    // Non-full-size block: apply the same horizontal rotation logic used by
    // ray_traversal's contains_pixel so partial blocks (slabs, fences, etc.)
    // collide with the correct footprint.
    short scale_x = block->scale.x;
    short scale_y = block->scale.y;
    short scale_z = block->scale.z;
    short offset_x = block->offset.x;
    short offset_y = block->offset.y;
    short offset_z = block->offset.z;
    if ( block->is_piston_head ) {
        // The yaw cases below only remap x/z; piston heads also face up/down,
        // so take the whole plate box from the shared helper.
        short head_scale[ 3 ], head_offset[ 3 ];
        piston_head_shape( rot, head_scale, head_offset );
        scale_x = head_scale[ 0 ];
        scale_y = head_scale[ 1 ];
        scale_z = head_scale[ 2 ];
        offset_x = head_offset[ 0 ];
        offset_y = head_offset[ 1 ];
        offset_z = head_offset[ 2 ];
    } else if ( piston_extended ) {
        // Same deal: the base recesses 4px at its head end, including up/down.
        short base_scale[ 3 ], base_offset[ 3 ];
        piston_base_shape( rot, base_scale, base_offset );
        scale_x = base_scale[ 0 ];
        scale_y = base_scale[ 1 ];
        scale_z = base_scale[ 2 ];
        offset_x = base_offset[ 0 ];
        offset_y = base_offset[ 1 ];
        offset_z = base_offset[ 2 ];
    } else if ( rot == BLOCK_ROTATE_90 ) {
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
    out_min.x = static_cast<double>( block_pos.x ) + PIXEL_TO_FLOAT( offset_x );
    out_max.x = out_min.x + PIXEL_TO_FLOAT( scale_x );
    out_min.z = static_cast<double>( block_pos.z ) + PIXEL_TO_FLOAT( offset_z );
    out_max.z = out_min.z + PIXEL_TO_FLOAT( scale_z );
    out_min.y = static_cast<double>( block_pos.y ) + PIXEL_TO_FLOAT( offset_y );
    out_max.y = out_min.y + PIXEL_TO_FLOAT( scale_y );
    if ( block->is_fence_gate ) {
        // Closed gates collide like fences: 1.5 blocks tall from the cell
        // floor, so nothing hops them or crawls under the 5px bottom gap.
        out_min.y = static_cast<double>( block_pos.y );
        out_max.y = out_min.y + 1.5;
    }
    return 1;
}

// Returns true if the player AABB overlaps any solid block.
static bool aabb_collides_world( World &world, const glm::dvec3 &amin, const glm::dvec3 &amax ) {
    const double eps = 1e-4;
    const int x0 = static_cast<int>( floor( amin.x ) );
    const int x1 = static_cast<int>( floor( amax.x - eps ) );
    // Fence/gate collision reaches 0.5 into the cell above, so also scan the
    // layer below the AABB; ordinary boxes there never overlap.
    const int y0 = static_cast<int>( floor( amin.y ) ) - 1;
    const int y1 = static_cast<int>( floor( amax.y - eps ) );
    const int z0 = static_cast<int>( floor( amin.z ) );
    const int z1 = static_cast<int>( floor( amax.z - eps ) );
    for ( int x = x0; x <= x1; x++ ) {
        for ( int y = y0; y <= y1; y++ ) {
            for ( int z = z0; z <= z1; z++ ) {
                glm::dvec3 mins[ FENCE_MAX_BOXES ], maxs[ FENCE_MAX_BOXES ];
                const int num_boxes = block_collision_boxes( world, glm::ivec3( x, y, z ), mins, maxs );
                for ( int i = 0; i < num_boxes; i++ ) {
                    if ( aabb_overlap( amin, amax, mins[ i ], maxs[ i ] ) ) {
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
static void resolve_axis( World &world, glm::dvec3 &amin, glm::dvec3 &amax, int axis, double delta, bool &hit, bool &standing ) {
    hit = false;
    if ( delta == 0.0 ) {
        return;
    }
    // Record the pre-move extent on this axis so we can tell whether a block
    // face was actually crossed by this movement (vs. already overlapping the
    // player's body from before — e.g. a wall block at body height that the
    // player is standing next to).
    const double pre_min = amin[ axis ];
    const double pre_max = amax[ axis ];

    amin[ axis ] += delta;
    amax[ axis ] += delta;

    const double eps = 1e-4;
    const int x0 = static_cast<int>( floor( amin.x ) );
    const int x1 = static_cast<int>( floor( amax.x - eps ) );
    // One extra layer below: 1.5-block-tall collision boxes (fences, closed
    // gates) reach above their own cell.
    const int y0 = static_cast<int>( floor( amin.y ) ) - 1;
    const int y1 = static_cast<int>( floor( amax.y - eps ) );
    const int z0 = static_cast<int>( floor( amin.z ) );
    const int z1 = static_cast<int>( floor( amax.z - eps ) );

    double best = delta > 0.0 ? NO_CLAMP_POS : NO_CLAMP_NEG;
    bool found = false;
    for ( int x = x0; x <= x1; x++ ) {
        for ( int y = y0; y <= y1; y++ ) {
            for ( int z = z0; z <= z1; z++ ) {
                glm::dvec3 mins[ FENCE_MAX_BOXES ], maxs[ FENCE_MAX_BOXES ];
                const int num_boxes = block_collision_boxes( world, glm::ivec3( x, y, z ), mins, maxs );
                for ( int i = 0; i < num_boxes; i++ ) {
                    const glm::dvec3 &bmin = mins[ i ];
                    const glm::dvec3 &bmax = maxs[ i ];
                    if ( !aabb_overlap( amin, amax, bmin, bmax ) ) {
                        continue;
                    }
                    if ( delta > 0.0 ) {
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
    }

    if ( !found ) {
        return;
    }
    hit = true;
    const double size = amax[ axis ] - amin[ axis ];
    if ( delta > 0.0 ) {
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
static void try_step_up( World &world, glm::dvec3 &amin, glm::dvec3 &amax, int axis, double requested_delta ) {
    const glm::dvec3 clamped_min = amin;
    const glm::dvec3 clamped_max = amax;

    // Raise the player by STEP_HEIGHT and check there is headroom.
    glm::dvec3 up_min = amin;
    glm::dvec3 up_max = amax;
    up_min.y += STEP_HEIGHT;
    up_max.y += STEP_HEIGHT;
    if ( aabb_collides_world( world, up_min, up_max ) ) {
        amin = clamped_min;
        amax = clamped_max;
        return;
    }

    // Try to move forward by the full requested delta at the raised height.
    glm::dvec3 test_min = up_min;
    glm::dvec3 test_max = up_max;
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

// True if something solid exists within STEP_HEIGHT below the player AABB —
// i.e. the move leaves the player over a surface they could still step back
// onto. STEP_HEIGHT matches step-up range, so sneaking can descend slabs and
// stairs (0.5 drops) but is stopped at drops of a full block or more.
static bool has_support_below( World &world, const glm::dvec3 &amin, const glm::dvec3 &amax ) {
    const glm::dvec3 probe_min( amin.x, amin.y - STEP_HEIGHT, amin.z );
    return aabb_collides_world( world, probe_min, amax );
}

// Moves the player AABB by `delta` on a horizontal axis (0=x, 2=z), resolving
// collisions and attempting a step-up over small ledges. When edge_guard is
// set and the player is standing, the move is clamped so the AABB never ends
// fully past the edge of its support (sneak edge protection).
static void move_horizontal_axis( World &world, glm::dvec3 &amin, glm::dvec3 &amax, int axis, double delta, bool standing, bool edge_guard ) {
    if ( delta == 0.0 ) {
        return;
    }
    const glm::dvec3 pre_min = amin;
    const glm::dvec3 pre_max = amax;
    bool hit = false;
    resolve_axis( world, amin, amax, axis, delta, hit, standing );
    // The clamped (non-stepped) position, kept so a stepped position that
    // turned out to be over a void can be rolled back to it.
    const glm::dvec3 clamped_min = amin;
    const glm::dvec3 clamped_max = amax;
    if ( standing && hit ) {
        // Step forward by only the unapplied remainder of this substep's
        // move so the total displacement never exceeds what was requested.
        try_step_up( world, amin, amax, axis, delta - ( amin[ axis ] - pre_min[ axis ] ) );
    }
    if ( !edge_guard || !standing || has_support_below( world, amin, amax ) ) {
        return;
    }
    // The move (possibly via step-up) left the player with no support below.
    // Go back to the clamped position and binary-search the largest fraction
    // of that axis move that still has ground underneath.
    amin = clamped_min;
    amax = clamped_max;
    double lo = 0.0, hi = 1.0;
    for ( int i = 0; i < 10; i++ ) {
        const double mid = ( lo + hi ) * 0.5;
        glm::dvec3 tmin = pre_min;
        glm::dvec3 tmax = pre_max;
        tmin[ axis ] += mid * ( clamped_min[ axis ] - pre_min[ axis ] );
        tmax[ axis ] += mid * ( clamped_max[ axis ] - pre_max[ axis ] );
        if ( has_support_below( world, tmin, tmax ) ) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    amin[ axis ] = pre_min[ axis ] + lo * ( clamped_min[ axis ] - pre_min[ axis ] );
    amax[ axis ] = pre_max[ axis ] + lo * ( clamped_max[ axis ] - pre_max[ axis ] );
}

int Collision::check_collides_with_block( World &world, const glm::dvec3 &player, const glm::ivec3 &block, double player_height, double eye_height, double half_width ) {
    // Used by block placement validation: reject if the player overlaps the
    // target block cell. Treat the target as a full 1x1x1 block (the caller
    // already gated on collides_with_player), matching the original behavior.
    const glm::dvec3 block_min = glm::dvec3( block );
    const glm::dvec3 block_max = block_min + glm::dvec3( 1.0 );

    glm::dvec3 pmin, pmax;
    player_aabb( player, player_height, eye_height, half_width, pmin, pmax );
    return aabb_overlap( pmin, pmax, block_min, block_max ) ? 1 : 0;
}

bool Collision::collides_at( World &world, const glm::dvec3 &eye, double player_height, double eye_height, double half_width ) {
    glm::dvec3 amin, amax;
    player_aabb( eye, player_height, eye_height, half_width, amin, amax );
    return aabb_collides_world( world, amin, amax );
}

void Collision::check_move( World &world, glm::dvec3 &movement_vec, glm::dvec3 &position, int *out_standing,
                            double player_height, double eye_height, bool edge_guard, double half_width ) {
    bool standing = false;

    glm::dvec3 amin, amax;
    player_aabb( position, player_height, eye_height, half_width, amin, amax );
    const glm::dvec3 initial_center = ( amin + amax ) * 0.5;

    // Substep so no single resolve moves more than COLLISION_MAX_SUBSTEP on any
    // axis. This guarantees a substep can't tunnel through a 1-block-thick wall.
    const double max_delta = std::fmax( std::fabs( movement_vec.x ), std::fmax( std::fabs( movement_vec.y ), std::fabs( movement_vec.z ) ) );
    int steps = static_cast<int>( ceil( max_delta / COLLISION_MAX_SUBSTEP ) );
    if ( steps < 1 ) {
        steps = 1;
    }
    const glm::dvec3 step = movement_vec / static_cast<double>( steps );

    for ( int i = 0; i < steps; i++ ) {
        bool hit = false;
        // Resolve Y first so gravity/landing is settled before horizontal
        // moves and so step-up has a stable base to work from.
        resolve_axis( world, amin, amax, 1, step.y, hit, standing );

        // Step-up only applies when the player is standing on the ground this
        // tick — otherwise jumping/falling next to a wall would launch the
        // player up and over it. Edge guard (sneaking) likewise only clamps
        // while standing: airborne players can drift past edges and land
        // beyond them.
        move_horizontal_axis( world, amin, amax, 0, step.x, standing, edge_guard );
        move_horizontal_axis( world, amin, amax, 2, step.z, standing, edge_guard );
    }

    // Write back the actual applied displacement (eye-space). The caller adds
    // this to position, so we must not modify position here.
    const glm::dvec3 final_center = ( amin + amax ) * 0.5;
    movement_vec = final_center - initial_center;
    *out_standing = standing ? 1 : 0;
}
