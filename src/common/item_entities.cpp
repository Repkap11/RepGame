#include "common/item_entities.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <unordered_map>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block.hpp"
#include "common/chunk_loader.hpp"
#include "common/chunk.hpp"
#include "common/multiplayer.hpp"
#include "common/net_entities.hpp"

// Per-tick constants at UPS_RATE (144). Gravity and air drag are MC's item
// values (0.04/tick², 0.98 at 20 TPS) rescaled to 144 UPS: ~16 blocks/s²
// (0.00077/tick²) and 0.9972 drag — gentler than the player's
// GRAVITY_STRENGTH. Ground friction 0.6 on xz. In water gravity is replaced
// by buoyancy (+0.0015/tick) with a heavy 0.9 drag and a 0.03 upward speed
// cap, so items rise gently and bob at the surface.
#define ITEM_GRAVITY 0.00077
#define ITEM_WATER_BUOYANCY 0.0015
#define ITEM_WATER_RISE_CAP 0.03
#define ITEM_AIR_DRAG 0.9972
#define ITEM_WATER_DRAG 0.9
#define ITEM_GROUND_FRICTION 0.6
// Player pickup: items with pickup_delay 0 within this distance of the
// player's chest magnet toward it; an overlap with the feet AABB (expanded
// this much horizontally/vertically) is collected.
#define ITEM_MAGNET_RADIUS 2.0
#define ITEM_MAGNET_PULL 0.2
#define ITEM_PICKUP_EXPAND_XZ 1.0
#define ITEM_PICKUP_EXPAND_Y 0.5
// Stacks of the same item merge into the older entity within this distance,
// checked every MERGE_INTERVAL ticks, capped at item_max_stack.
#define ITEM_MERGE_RADIUS 0.5
#define ITEM_MERGE_INTERVAL 10

// The shared extruded-sprite mesh: a 16x16 tile in the unit square [0,1]²
// extruded 1/16 in z. Every quad — front/back texel cells and the side/cap
// slices — is pinned to exactly one texel: all four vertices carry that
// texel's center uv, so uv interpolation is constant across the quad and the
// fragment's per-texel alpha test is all-or-nothing per quad. Without that,
// MSAA shades a pixel straddling a texel boundary once at its center and a
// center landing in a transparent texel discards the whole pixel even though
// its covered samples lie in the opaque texel — see-through pinholes along
// the icon's texel grid. 1536 quads total.
#define ITEM_SPRITE_QUADS ( 16 * 16 * 6 )
#define ITEM_SPRITE_VERTS ( ITEM_SPRITE_QUADS * 4 )
#define ITEM_SPRITE_INDICES ( ITEM_SPRITE_QUADS * 6 )

static ParticleVertex item_sprite_vb[ ITEM_SPRITE_VERTS ];
static unsigned int item_sprite_ib[ ITEM_SPRITE_INDICES ];
static bool item_sprite_mesh_built = false;

static void sprite_quad( int q, float x0, float y0, float x1, float y1, float z0, float z1, unsigned int face, float u0, float v0, float u1, float v1, int flip ) {
    // Axis-aligned quad: varying axes picked by the plane (z0==z1: xy plane;
    // x0==x1: zy plane; y0==y1: xz plane). flip selects the winding so the
    // face stays outward — matches how a face is oriented on the cube mesh.
    ParticleVertex *v = &item_sprite_vb[ q * 4 ];
    if ( z0 == z1 ) { // xy quad at fixed z
        v[ 0 ] = { x0, y0, z0, u0, v0, face };
        v[ 1 ] = { x1, y0, z0, u1, v0, face };
        v[ 2 ] = { x1, y1, z0, u1, v1, face };
        v[ 3 ] = { x0, y1, z0, u0, v1, face };
    } else if ( x0 == x1 ) { // zy quad at fixed x
        v[ 0 ] = { x0, y0, z0, u0, v0, face };
        v[ 1 ] = { x0, y0, z1, u0, v0, face };
        v[ 2 ] = { x0, y1, z1, u0, v1, face };
        v[ 3 ] = { x0, y1, z0, u0, v1, face };
    } else { // xz quad at fixed y
        v[ 0 ] = { x0, y0, z0, u0, v0, face };
        v[ 1 ] = { x1, y0, z0, u1, v0, face };
        v[ 2 ] = { x1, y0, z1, u1, v0, face };
        v[ 3 ] = { x0, y0, z1, u0, v0, face };
    }
    unsigned int *ix = &item_sprite_ib[ q * 6 ];
    if ( flip ) {
        ix[ 0 ] = q * 4 + 0;
        ix[ 1 ] = q * 4 + 2;
        ix[ 2 ] = q * 4 + 1;
        ix[ 3 ] = q * 4 + 0;
        ix[ 4 ] = q * 4 + 3;
        ix[ 5 ] = q * 4 + 2;
    } else {
        ix[ 0 ] = q * 4 + 0;
        ix[ 1 ] = q * 4 + 1;
        ix[ 2 ] = q * 4 + 2;
        ix[ 3 ] = q * 4 + 0;
        ix[ 4 ] = q * 4 + 2;
        ix[ 5 ] = q * 4 + 3;
    }
}

static void build_item_sprite_mesh( ) {
    if ( item_sprite_mesh_built ) {
        return;
    }
    item_sprite_mesh_built = true;
    int q = 0;
    const float d = 1.0f / 16.0f;
    for ( int i = 0; i < 16; i++ ) {
        for ( int j = 0; j < 16; j++ ) {
            const float u = ( i + 0.5f ) / 16.0f;
            const float v = ( j + 0.5f ) / 16.0f;
            // Front (+z) and back (-z): one quad per texel, u along +x and v
            // along +y as before so the icon isn't mirrored.
            sprite_quad( q++, i * d, j * d, ( i + 1 ) * d, ( j + 1 ) * d, d, d, FACE_FRONT, u, v, u, v, 0 );
            sprite_quad( q++, i * d, j * d, ( i + 1 ) * d, ( j + 1 ) * d, 0.0f, 0.0f, FACE_BACK, u, v, u, v, 1 );
            // Left edge of column i (normal -x) and right edge (normal +x),
            // row j of that column.
            sprite_quad( q++, i * d, j * d, i * d, ( j + 1 ) * d, 0.0f, d, FACE_LEFT, u, v, u, v, 0 );
            sprite_quad( q++, ( i + 1 ) * d, j * d, ( i + 1 ) * d, ( j + 1 ) * d, 0.0f, d, FACE_RIGHT, u, v, u, v, 1 );
            // Bottom edge of row j (normal -y) and top edge (normal +y),
            // column i of that row.
            sprite_quad( q++, i * d, j * d, ( i + 1 ) * d, j * d, 0.0f, d, FACE_BOTTOM, u, v, u, v, 0 );
            sprite_quad( q++, i * d, ( j + 1 ) * d, ( i + 1 ) * d, ( j + 1 ) * d, 0.0f, d, FACE_TOP, u, v, u, v, 1 );
        }
    }
}

// Case-insensitive name lookup over every defined block name plus the pure
// item names (REPGAME_ITEM_TEST). LAST_BLOCK_ID on no match.
static ItemID item_id_from_name( const char *name ) {
    for ( unsigned int id = 0; id < static_cast<unsigned int>( LAST_BLOCK_ID ); id++ ) {
        if ( !block_id_defined( id ) ) {
            continue;
        }
        const char *n = block_definition_get_name( static_cast<BlockID>( id ) );
        if ( n != nullptr && strcasecmp( n, name ) == 0 ) {
            return id;
        }
    }
    for ( ItemID id = ITEM_ID_BASE; id < ITEM_ID_END; id++ ) {
        const char *n = item_name( id );
        if ( n != nullptr && strcasecmp( n, name ) == 0 ) {
            return id;
        }
    }
    return LAST_BLOCK_ID;
}

void ItemEntities::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position, const VertexBufferLayout &vbl_item_instance ) {
    build_item_sprite_mesh( );
    this->render_cube.init( vbl_object_vertex, vbl_object_position, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    this->render_sprite.init( vbl_object_vertex, vbl_item_instance, item_sprite_vb, ITEM_SPRITE_VERTS, item_sprite_ib, ITEM_SPRITE_INDICES );
    this->render_inited = true;

    // REPGAME_ITEM_TEST="name,x,y,z[,count];..." — exact spawns for
    // deterministic captures, stationary with an effectively infinite
    // pickup delay so they stay put.
    const char *spec = getenv( "REPGAME_ITEM_TEST" );
    if ( spec != nullptr ) {
        char buf[ 1024 ];
        strncpy( buf, spec, sizeof( buf ) - 1 );
        buf[ sizeof( buf ) - 1 ] = 0;
        for ( char *entry = strtok( buf, ";" ); entry != nullptr; entry = strtok( nullptr, ";" ) ) {
            char item_name[ 40 ];
            double x, y, z;
            int count = 1;
            if ( sscanf( entry, "%39[^,],%lf,%lf,%lf,%d", item_name, &x, &y, &z, &count ) < 4 ) {
                pr_debug( "REPGAME_ITEM_TEST: bad entry '%s' (want name,x,y,z[,count])", entry );
                continue;
            }
            const ItemID item = item_id_from_name( item_name );
            if ( item == LAST_BLOCK_ID ) {
                pr_debug( "REPGAME_ITEM_TEST: unknown item '%s'", item_name );
                continue;
            }
            // Non-persistent (never written to an items file) and facing the
            // -z/+z axis (spin_seed 0): captures see the icon face-on.
            const uint32_t id = this->spawn( item, count > 0 ? count : 1, glm::dvec3( x, y, z ), glm::dvec3( 0.0 ), INT_MAX / 2, 0 );
            (void)id;
            this->items.back( ).spin_seed = 0.0f;
        }
    }
}

static void resolve_item_faces( const ItemID item, unsigned short face[ NUM_FACES_IN_CUBE ] ) {
    const BlockState state = { static_cast<BlockID>( item ), 0, 0, static_cast<BlockID>( item ), 0 };
    const Block *block = block_definition_get_definition( state.id );
    BlockCoords coords = {};
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        coords.face[ f ] = block->textures[ f ];
    }
    block_adjust_coord_based_on_state( block, &state, &coords );
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        face[ f ] = coords.face[ f ];
    }
}

static glm::dvec3 item_center( const ItemEntities::ItemEntity &it ) {
    return it.pos + glm::dvec3( 0.0, ItemEntities::ITEM_SIZE * 0.5, 0.0 );
}

static glm::dvec3 item_min( const ItemEntities::ItemEntity &it ) {
    return it.pos - glm::dvec3( ItemEntities::ITEM_SIZE * 0.5, 0.0, ItemEntities::ITEM_SIZE * 0.5 );
}

// Spin phase seed: derived from the entity id in single player but from
// the wire net_id in multiplayer so every client's mirror spins in phase.
static float item_spin_seed( const uint64_t id ) {
    return static_cast<float>( ( id * 2654435761ull ) % 1024 ) / 1024.0f * 6.2831853f;
}

void ItemEntities::render_pose( const ItemEntity &it, const float alpha, float *yaw, float *bob ) {
    const double t = static_cast<double>( it.age_ticks + static_cast<double>( alpha ) ) / UPS_RATE;
    *yaw = it.spin_seed + static_cast<float>( t ) * 2.0f;
    *bob = ( it.grounded || it.in_water ) ? 0.05f + 0.05f * sinf( it.spin_seed + static_cast<float>( t ) * 2.0f ) : 0.0f;
}

bool ItemEntities::item_chunk_live( World &world, const ItemEntity &it ) const {
    const Chunk *chunk = world.get_loaded_chunk( glm::ivec3( glm::floor( item_center( it ) ) ) );
    return chunk != nullptr && chunk->load_state == CHUNK_LIVE;
}

void ItemEntities::drop_render_instance( ItemEntity &it ) {
    if ( it.entity == entt::null ) {
        return;
    }
    if ( item_is_block( it.item ) && item_icon_is_isometric( it.item ) ) {
        this->render_cube.remove( it.entity );
    } else {
        this->render_sprite.remove( it.entity );
    }
    it.entity = entt::null;
}

void ItemEntities::remove_item( const size_t i ) {
    ItemEntity &it = this->items[ i ];
    drop_render_instance( it );
    this->items.erase( this->items.begin( ) + i );
}

uint32_t ItemEntities::spawn( const ItemID item, const int count, const glm::dvec3 &pos, const glm::dvec3 &vel, const int pickup_delay, const int persistent, const bool from_net ) {
    if ( this->items.size( ) >= MAX_ITEM_ENTITIES ) {
        // Evict the oldest entity (lowest id).
        size_t oldest = 0;
        for ( size_t i = 1; i < this->items.size( ); i++ ) {
            if ( this->items[ i ].id < this->items[ oldest ].id ) {
                oldest = i;
            }
        }
        remove_item( oldest );
    }
    ItemEntity it;
    it.id = this->next_id++;
    if ( this->next_id == 0 ) {
        this->next_id = 1;
    }
    it.item = item;
    it.count = count;
    it.pos = pos;
    it.prev_pos = pos;
    it.vel = vel;
    it.pickup_delay = pickup_delay;
    it.persistent = persistent;
    it.spin_seed = item_spin_seed( it.id );
    if ( item_is_block( item ) ) {
        resolve_item_faces( item, it.face );
    }
    this->items.push_back( it );
    ItemEntity &back = this->items.back( );
    back.net_chunk = glm::ivec3( glm::floor( pos / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
    // Multiplayer: announce the spawn to the server. mint_id returns 0
    // before WELCOME — those entities stay local-only forever.
    if ( this->net != nullptr && !from_net ) {
        const NetEntityId nid = this->net->mint_id( );
        if ( nid != 0 ) {
            back.net_id = nid;
            back.spin_seed = item_spin_seed( nid );
            NetEntityRecord rec;
            rec.id = nid;
            rec.kind = NetEntityKind::ITEM;
            rec.x = back.pos.x;
            rec.y = back.pos.y;
            rec.z = back.pos.z;
            rec.vx = static_cast<float>( back.vel.x );
            rec.vy = static_cast<float>( back.vel.y );
            rec.vz = static_cast<float>( back.vel.z );
            rec.type = back.item;
            rec.count = back.count;
            rec.age_ticks = back.age_ticks;
            rec.pickup_delay = back.pickup_delay;
            this->net->send_spawn( rec );
        }
    }
    return it.id;
}

void ItemEntities::impulse( const glm::dvec3 &center, const double radius, const double strength ) {
    for ( ItemEntity &it : this->items ) {
        if ( it.remote ) {
            continue; // mirrors are shoved by their own owner's blast
        }
        const glm::dvec3 offset = item_center( it ) - center;
        const double d = glm::length( offset );
        if ( d >= radius ) {
            continue;
        }
        const glm::dvec3 dir = d > 1e-6 ? offset / d : glm::dvec3( 0.0, 1.0, 0.0 );
        // Items are light: take 3x the body shove.
        it.vel += dir * strength * ( 1.0 - d / radius ) * 3.0;
        it.grounded = 0;
    }
}

// Cell-keyed spatial hash for the merge broadphase (same scheme as the
// BlockBodies one).
static int64_t item_cell_key( const glm::ivec3 &c ) {
    return ( ( static_cast<int64_t>( c.x ) & 0x1FFFFF ) << 42 ) | ( ( static_cast<int64_t>( c.y ) & 0x1FFFFF ) << 21 ) | ( static_cast<int64_t>( c.z ) & 0x1FFFFF );
}

void ItemEntities::tick( RepGameState &state ) {
    World &world = state.world;
    this->now_tick = state.tick_number;
    const bool freeze = RepGame::capture_mode( );
    // Bind the sync hub while multiplayer is on; entities with net_id 0
    // (spawned before WELCOME) behave exactly like single player.
    this->net = this->mp_mode ? &state.multiplayer.entities : nullptr;

    // Player feet AABB (camera pos is the eye position).
    const double feet_y = state.camera.pos.y - state.camera.eye_height;
    const double half_w = PLAYER_WIDTH * 0.5;
    const glm::dvec3 chest = state.camera.pos - glm::dvec3( 0.0, state.camera.eye_height * 0.5, 0.0 );

    const bool do_merge = state.tick_number % ITEM_MERGE_INTERVAL == 0;
    std::unordered_map<int64_t, std::vector<int>> hash;
    if ( do_merge ) {
        hash.reserve( this->items.size( ) );
        for ( size_t i = 0; i < this->items.size( ); i++ ) {
            hash[ item_cell_key( glm::ivec3( glm::floor( item_center( this->items[ i ] ) ) ) ) ].push_back( static_cast<int>( i ) );
        }
    }

    std::vector<size_t> dead;
    for ( size_t i = 0; i < this->items.size( ); i++ ) {
        ItemEntity &it = this->items[ i ];
        it.prev_pos = it.pos;
        if ( freeze || !item_chunk_live( world, it ) ) {
            continue;
        }

        if ( it.remote ) {
            // Mirror of a remote-owned entity: no physics or merging —
            // ease toward the last streamed target. age_ticks still ticks:
            // it's the spin/bob render clock (despawn below is owner-only).
            it.age_ticks++;
            if ( it.pickup_delay > 0 ) {
                it.pickup_delay--;
            }
            const glm::dvec3 delta = it.net_pos - it.pos;
            if ( glm::length( delta ) > 4.0 ) {
                it.pos = it.net_pos;
            } else {
                it.pos += delta * 0.35;
            }
            it.vel = it.net_vel;
            // Magnet is visual only for mirrors: drift toward the chest.
            const glm::dvec3 to_chest = chest - item_center( it );
            const double cd = glm::length( to_chest );
            if ( it.pickup_delay == 0 && cd < ITEM_MAGNET_RADIUS && cd > 1e-6 ) {
                it.pos += to_chest / cd * ITEM_MAGNET_PULL * 0.02;
            }
            // Same medium test as the owner so bob floats identically, and
            // grounded iff ~resting over solid footing.
            const BlockState medium = world.get_loaded_block( glm::ivec3( glm::floor( item_center( it ) ) ) );
            it.in_water = medium.id == WATER || medium.waterlogged;
            const BlockState under = world.get_loaded_block(
                glm::ivec3( glm::floor( item_center( it ) - glm::dvec3( 0.0, ITEM_SIZE * 0.5 + 1e-3, 0.0 ) ) ) );
            it.grounded = std::fabs( it.net_vel.y ) < 1e-3 &&
                          ( under.id == LAST_BLOCK_ID || block_definition_get_definition( under.id )->collides_with_player )
                              ? 1
                              : 0;
        } else {
        it.age_ticks++;
        if ( it.pickup_delay > 0 ) {
            it.pickup_delay--;
        }

        // Medium: the cell at the item center is water or waterlogged.
        const BlockState medium = world.get_loaded_block( glm::ivec3( glm::floor( item_center( it ) ) ) );
        it.in_water = medium.id == WATER || medium.waterlogged;
        if ( it.in_water ) {
            it.vel.y += ITEM_WATER_BUOYANCY;
            it.vel *= ITEM_WATER_DRAG;
            if ( it.vel.y > ITEM_WATER_RISE_CAP ) {
                it.vel.y = ITEM_WATER_RISE_CAP;
            }
        } else {
            it.vel.y -= ITEM_GRAVITY;
            it.vel *= ITEM_AIR_DRAG;
            if ( it.vel.y > TERMINAL_VELOCITY ) {
                it.vel.y = TERMINAL_VELOCITY;
            } else if ( it.vel.y < -TERMINAL_VELOCITY ) {
                it.vel.y = -TERMINAL_VELOCITY;
            }
        }

        // Magnet: a collectable item near the player drifts toward the chest.
        const glm::dvec3 to_chest = chest - item_center( it );
        const double chest_dist = glm::length( to_chest );
        if ( it.pickup_delay == 0 && chest_dist < ITEM_MAGNET_RADIUS && chest_dist > 1e-6 ) {
            it.vel += to_chest / chest_dist * ITEM_MAGNET_PULL * 0.02;
        }

        glm::dvec3 min = item_min( it );
        glm::dvec3 max = min + glm::dvec3( ITEM_SIZE );
        bool hit_axis[ 3 ];
        int hit_extra_axis[ 3 ];
        bool standing = false;
        static const std::vector<BodyBox> no_extra;
        Collision::move_box( world, min, max, it.vel, no_extra, hit_axis, hit_extra_axis, &standing );
        it.pos = min + glm::dvec3( ITEM_SIZE * 0.5, 0.0, ITEM_SIZE * 0.5 );
        for ( int axis = 0; axis < 3; axis++ ) {
            if ( hit_axis[ axis ] ) {
                it.vel[ axis ] = 0.0; // no bounce
            }
        }
        it.grounded = standing ? 1 : 0;
        if ( it.grounded ) {
            it.vel.x *= ITEM_GROUND_FRICTION;
            it.vel.z *= ITEM_GROUND_FRICTION;
        }
        } // end remote/owned branch

        // Pickup: the item box overlaps the feet AABB expanded a little.
        {
        glm::dvec3 min = item_min( it );
        glm::dvec3 max = min + glm::dvec3( ITEM_SIZE );
        if ( it.pickup_delay == 0 && //
             max.x > state.camera.pos.x - half_w - ITEM_PICKUP_EXPAND_XZ && min.x < state.camera.pos.x + half_w + ITEM_PICKUP_EXPAND_XZ && //
             max.z > state.camera.pos.z - half_w - ITEM_PICKUP_EXPAND_XZ && min.z < state.camera.pos.z + half_w + ITEM_PICKUP_EXPAND_XZ && //
             max.y > feet_y - ITEM_PICKUP_EXPAND_Y && min.y < feet_y + state.camera.player_height + ITEM_PICKUP_EXPAND_Y ) {
            if ( this->mp_mode && it.net_id != 0 ) {
                // Synced entity (owned or mirror): the server arbitrates the
                // pickup. One request per overlap; hidden while it flies.
                if ( !it.pickup_pending && this->net != nullptr && this->net->connected( ) ) {
                    const int room = RepGame::inventory_space_for_item( state, it.item );
                    this->net->send_pickup_request( it.net_id, room );
                    it.pickup_pending = true;
                }
                continue;
            }
            const int leftover = RepGame::give_item_to_player( state, it.item, it.count );
            if ( leftover < it.count ) {
                audio_play_sfx_gain( "pop", 0.5f );
                // The pickup may have filled the selected hotbar slot —
                // refresh the held-item overlay.
                state.ui_overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
            }
            it.count = leftover;
            if ( it.count <= 0 ) {
                dead.push_back( i );
                continue;
            }
        }

        if ( !it.remote && it.age_ticks >= ITEM_DESPAWN_TICKS ) {
            this->send_net_remove( it, NET_REMOVE_DESPAWN );
            dead.push_back( i );
        }
        }
    }

    // Merge pass: same item, close together, into the older entity. Runs
    // before removals so indices stay valid. Mirrors and hidden
    // pickup-pending items don't take part.
    if ( do_merge ) {
        for ( size_t i = 0; i < this->items.size( ); i++ ) {
            ItemEntity &a = this->items[ i ];
            if ( a.count >= item_max_stack( a.item ) || a.remote || a.pickup_pending ) {
                continue;
            }
            const glm::ivec3 cc( glm::floor( item_center( a ) ) );
            for ( int dx = -1; dx <= 1; dx++ ) {
                for ( int dy = -1; dy <= 1; dy++ ) {
                    for ( int dz = -1; dz <= 1; dz++ ) {
                        const auto found = hash.find( item_cell_key( cc + glm::ivec3( dx, dy, dz ) ) );
                        if ( found == hash.end( ) ) {
                            continue;
                        }
                        for ( const int j : found->second ) {
                            if ( static_cast<size_t>( j ) <= i ) {
                                continue;
                            }
                            ItemEntity &b = this->items[ j ];
                            if ( b.item != a.item || a.count >= item_max_stack( a.item ) || b.remote || b.pickup_pending ) {
                                continue;
                            }
                            if ( glm::length( item_center( a ) - item_center( b ) ) >= ITEM_MERGE_RADIUS ) {
                                continue;
                            }
                            const int space = item_max_stack( a.item ) - a.count;
                            const int move = b.count < space ? b.count : space;
                            a.count += move;
                            b.count -= move;
                        }
                    }
                }
            }
        }
        // Zero-count merged-away entities die with the rest.
        for ( size_t i = 0; i < this->items.size( ); i++ ) {
            if ( this->items[ i ].count <= 0 ) {
                this->send_net_remove( this->items[ i ], NET_REMOVE_MERGED );
                dead.push_back( i );
            }
        }
    }

    std::sort( dead.rbegin( ), dead.rend( ) );
    dead.erase( std::unique( dead.begin( ), dead.end( ) ), dead.end( ) );
    for ( const size_t i : dead ) {
        remove_item( i );
    }

    // Owned-chunk leave-poll (~every 16 ticks), same cadence as mobs.
    if ( ++this->poll_count >= 16 ) {
        this->poll_count = 0;
        this->poll_owned( world );
    }
}

// Called from World::render for every chunk that finished load+finalize this
// frame. Claims ownership (so the leave path knows whether to persist) and
// spawns the worker-side pending_items as live item entities.
// Same test-mode gate as the chunk's mobs load: no item file reads or
// writes under REPGAME_MOBS=0 / REPGAME_MOB_TEST.
static bool item_persistence_enabled( ) {
    static const bool enabled = !( getenv( "REPGAME_MOBS" ) != nullptr && atoi( getenv( "REPGAME_MOBS" ) ) == 0 ) && getenv( "REPGAME_MOB_TEST" ) == nullptr;
    return enabled;
}

void ItemEntities::adopt_chunk( Chunk &chunk ) {
    // Multiplayer mode: entity files live server-side; nothing local to load.
    if ( this->storage == nullptr || !item_persistence_enabled( ) || this->mp_mode ) {
        chunk.pending_items.clear( );
        return;
    }
    for ( const OwnedItemChunk &owned : this->owned_chunks ) {
        if ( owned.pos == chunk.chunk_pos ) {
            chunk.pending_items.clear( );
            return;
        }
    }
    for ( const ItemFileRecord &record : chunk.pending_items ) {
        const uint32_t id = this->spawn( record.item, record.count, glm::dvec3( record.x, record.y, record.z ),
                                         glm::dvec3( record.vx, record.vy, record.vz ), 0 );
        (void)id;
        this->items.back( ).age_ticks = record.age_ticks;
    }
    this->owned_chunks.push_back( { chunk.chunk_pos, chunk.items_file_present } );
    chunk.pending_items.clear( );
}

// Roughly every 16 ticks: persist+remove items whose owning chunk left the
// active window (slot empty, different pos, or PARKED) — same rules as
// MobManager::poll_owned.
void ItemEntities::poll_owned( World &world ) {
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( size_t i = 0; i < this->owned_chunks.size( ); ) {
        const OwnedItemChunk &owned = this->owned_chunks[ i ];
        const Chunk *chunk = world.get_loaded_chunk( owned.pos * glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) );
        const bool left = chunk == nullptr || chunk->chunk_pos != owned.pos || chunk->load_state == CHUNK_PARKED;
        if ( !left ) {
            i++;
            continue;
        }
        std::vector<ItemFileRecord> records;
        for ( size_t j = 0; j < this->items.size( ); ) {
            ItemEntity &it = this->items[ j ];
            const glm::ivec3 owner = glm::ivec3( glm::floor( it.pos / csize ) );
            if ( it.persistent && owner == owned.pos ) {
                ItemFileRecord record = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, 0 };
                record.item = it.item;
                record.count = it.count;
                record.x = it.pos.x;
                record.y = it.pos.y;
                record.z = it.pos.z;
                record.vx = static_cast<float>( it.vel.x );
                record.vy = static_cast<float>( it.vel.y );
                record.vz = static_cast<float>( it.vel.z );
                record.age_ticks = it.age_ticks;
                records.push_back( record );
                remove_item( j );
            } else {
                j++;
            }
        }
        // Rewrite (even empty) when there was a file so stale records don't
        // resurrect, or when there is something to record.
        if ( this->storage != nullptr && item_persistence_enabled( ) && !RepGame::capture_mode( ) && !RepGame::fly_mode( ) && ( !records.empty( ) || owned.file_present ) ) {
            this->storage->persist_items( owned.pos, records );
        }
        this->owned_chunks.erase( this->owned_chunks.begin( ) + static_cast<long>( i ) );
    }
}

void ItemEntities::persist_all( ) {
    if ( this->storage == nullptr || !item_persistence_enabled( ) || RepGame::capture_mode( ) || RepGame::fly_mode( ) || this->mp_mode ) {
        return;
    }
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( const OwnedItemChunk &owned : this->owned_chunks ) {
        std::vector<ItemFileRecord> records;
        for ( const ItemEntity &it : this->items ) {
            if ( it.persistent && glm::ivec3( glm::floor( it.pos / csize ) ) == owned.pos ) {
                ItemFileRecord record = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, 0 };
                record.item = it.item;
                record.count = it.count;
                record.x = it.pos.x;
                record.y = it.pos.y;
                record.z = it.pos.z;
                record.vx = static_cast<float>( it.vel.x );
                record.vy = static_cast<float>( it.vel.y );
                record.vz = static_cast<float>( it.vel.z );
                record.age_ticks = it.age_ticks;
                records.push_back( record );
            }
        }
        if ( !records.empty( ) || owned.file_present ) {
            this->storage->persist_items( owned.pos, records );
        }
    }
}

// ---------------------------------------------------------------------------
// Multiplayer sync (server arbitrates, owner client simulates). mp_mode is
// set from World::render once a hostname exists; while it's set, local
// items_* file reads/writes are off and every networked entity obeys the
// records streamed through NetEntities.
// ---------------------------------------------------------------------------

void ItemEntities::set_multiplayer_mode( const bool mp_mode ) {
    this->mp_mode = mp_mode;
    if ( !mp_mode ) {
        this->net = nullptr;
    }
}

void ItemEntities::send_net_remove( ItemEntity &it, const uint8_t reason ) {
    if ( this->net == nullptr || it.net_id == 0 ) {
        return;
    }
    // Never announce removals for chunks we don't own (server-belief chunk).
    if ( this->net->owns_chunk( it.net_chunk ) ) {
        this->net->send_remove( it.net_id, reason );
    }
}

ItemEntities::ItemEntity *ItemEntities::find_by_net_id( const NetEntityId net_id ) {
    if ( net_id == 0 ) {
        return nullptr;
    }
    for ( ItemEntity &it : this->items ) {
        if ( it.net_id == net_id ) {
            return &it;
        }
    }
    return nullptr;
}

// Spawn one entity from a wire record (CHUNK_ROLE / ENTITY_SPAWN /
// ENTITY_TRANSFER). mirror=true means another client simulates it.
uint32_t ItemEntities::spawn_record( const NetEntityRecord &rec, const bool mirror ) {
    const uint32_t id = this->spawn( static_cast<ItemID>( rec.type ), rec.count,
                                     glm::dvec3( rec.x, rec.y, rec.z ), glm::dvec3( rec.vx, rec.vy, rec.vz ),
                                     rec.pickup_delay, 1, true );
    ItemEntity &it = this->items.back( );
    it.net_id = rec.id;
    it.spin_seed = item_spin_seed( rec.id );
    it.remote = mirror;
    it.net_pos = glm::dvec3( rec.x, rec.y, rec.z );
    it.net_vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
    it.net_chunk = glm::ivec3( glm::floor( glm::dvec3( rec.x, rec.y, rec.z ) / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
    it.age_ticks = rec.age_ticks;
    return id;
}

void ItemEntities::net_apply_spawn( const NetEntityRecord &rec, const bool mirror ) {
    ItemEntity *existing = this->find_by_net_id( rec.id );
    if ( existing != nullptr ) {
        // Already local (e.g. our own predicted spawn): just set ownership.
        existing->remote = mirror;
        existing->net_pos = glm::dvec3( rec.x, rec.y, rec.z );
        existing->net_vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
        existing->net_chunk = glm::ivec3( glm::floor( existing->net_pos / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
        return;
    }
    this->spawn_record( rec, mirror );
}

void ItemEntities::net_apply_state( const NetEntityStateEntry &e ) {
    ItemEntity *it = this->find_by_net_id( e.id );
    if ( it == nullptr ) {
        return;
    }
    // The incoming state reflects the server's belief: track the chunk it
    // places the entity in (same boundary hysteresis the server applies),
    // and only treat it as ours when we own that chunk.
    it->net_chunk = net_entity_next_chunk( it->net_chunk, glm::dvec3( e.x, e.y, e.z ) );
    const bool owned_here = this->net != nullptr && this->net->owns_chunk( it->net_chunk );
    if ( it->remote || !owned_here ) {
        // Mirror (or a predicted spawn we don't own): the streamed state is
        // authoritative — ease toward it.
        it->remote = true;
        it->net_pos = glm::dvec3( e.x, e.y, e.z );
        it->net_vel = glm::dvec3( e.vx, e.vy, e.vz );
        it->count = e.count;
    } else {
        // Owned: only the count is server-driven (partial pickups).
        it->count = e.count;
    }
    // pickup_pending is deliberately NOT cleared here — a routine state must
    // not unhide an item whose PICKUP_RESULT hasn't landed (re-requests).
}

void ItemEntities::net_apply_remove( const NetEntityId net_id ) {
    for ( size_t i = 0; i < this->items.size( ); i++ ) {
        if ( this->items[ i ].net_id == net_id && net_id != 0 ) {
            remove_item( i );
            return;
        }
    }
}

void ItemEntities::net_release_entity( const NetEntityRecord &rec, const bool still_viewed ) {
    ItemEntity *it = this->find_by_net_id( rec.id );
    if ( it == nullptr ) {
        return;
    }
    if ( still_viewed ) {
        it->remote = true;
        it->net_pos = glm::dvec3( rec.x, rec.y, rec.z );
        it->net_vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
        it->net_chunk = glm::ivec3( glm::floor( it->net_pos / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
    } else {
        for ( size_t i = 0; i < this->items.size( ); i++ ) {
            if ( &this->items[ i ] == it ) {
                remove_item( i );
                return;
            }
        }
    }
}

void ItemEntities::net_apply_chunk_role( const glm::ivec3 &chunk, const uint8_t role, const std::vector<NetEntityRecord> &records, const std::set<NetEntityId> &keep_ids ) {
    if ( role == NET_ROLE_NONE ) {
        // Chunk left our view entirely: drop its synced entities.
        for ( size_t i = 0; i < this->items.size( ); ) {
            const ItemEntity &it = this->items[ i ];
            if ( it.net_id != 0 && it.net_chunk == chunk ) {
                remove_item( i );
            } else {
                i++;
            }
        }
        return;
    }
    const bool owned = role == NET_ROLE_OWNER;
    // The record list is authoritative: synced items in this chunk that the
    // server doesn't know about are dropped.
    std::unordered_map<NetEntityId, const NetEntityRecord *> by_id;
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind == NetEntityKind::ITEM ) {
            by_id[ rec.id ] = &rec;
        }
    }
    for ( size_t i = 0; i < this->items.size( ); ) {
        const ItemEntity &it = this->items[ i ];
        if ( it.net_id != 0 && it.net_chunk == chunk && by_id.find( it.net_id ) == by_id.end( ) && !keep_ids.count( it.net_id ) ) {
            remove_item( i );
        } else {
            i++;
        }
    }
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind != NetEntityKind::ITEM ) {
            continue;
        }
        ItemEntity *existing = this->find_by_net_id( rec.id );
        if ( existing != nullptr ) {
            existing->remote = !owned;
            existing->net_pos = glm::dvec3( rec.x, rec.y, rec.z );
            existing->net_vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
            // The grant's chunk is authoritative — a record resting in the
            // boundary margin has floor(pos) != the server's chunk.
            existing->net_chunk = chunk;
        } else {
            this->spawn_record( rec, !owned );
            ItemEntity *sp = this->find_by_net_id( rec.id );
            if ( sp != nullptr ) {
                sp->net_chunk = chunk;
            }
        }
    }
}

void ItemEntities::collect_net_state( NetEntities &net, std::vector<NetEntityStateEntry> &out, const long tick_number ) {
    for ( ItemEntity &it : this->items ) {
        if ( it.net_id == 0 || it.remote || it.pickup_pending ) {
            continue;
        }
        // Ownership keys off the chunk the server believes the entity is in:
        // when it crosses into a foreign chunk we keep streaming so the
        // server sees the crossing and issues ENTITY_TRANSFER.
        if ( !net.owns_chunk( it.net_chunk ) ) {
            continue;
        }
        const bool keyframe = tick_number - it.net_keyframe_tick >= UPS_RATE;
        const bool changed = !it.net_sent ||                                  //
                             glm::length( it.pos - it.net_sent_pos ) > 1e-4 || //
                             glm::length( it.vel - it.net_sent_vel ) > 1e-4 || //
                             it.count != it.net_sent_count;
        if ( !changed && !keyframe ) {
            continue;
        }
        NetEntityStateEntry e;
        e.id = it.net_id;
        e.kind = static_cast<uint8_t>( NetEntityKind::ITEM );
        e.x = it.pos.x;
        e.y = it.pos.y;
        e.z = it.pos.z;
        e.vx = static_cast<float>( it.vel.x );
        e.vy = static_cast<float>( it.vel.y );
        e.vz = static_cast<float>( it.vel.z );
        e.count = it.count;
        out.push_back( e );
        it.net_sent = true;
        it.net_sent_pos = it.pos;
        it.net_sent_vel = it.vel;
        it.net_sent_count = it.count;
        // The server now applies its boundary hysteresis to this pos.
        it.net_chunk = net_entity_next_chunk( it.net_chunk, it.pos );
        if ( keyframe ) {
            it.net_keyframe_tick = tick_number;
        }
    }
}

void ItemEntities::update_render( const float alpha, const ChunkLoader &chunk_loader ) {
    if ( !this->render_inited ) {
        return;
    }
    for ( ItemEntity &it : this->items ) {
        if ( it.pickup_pending ) {
            // PICKUP_REQUEST in flight: hidden until the server rules.
            drop_render_instance( it );
            continue;
        }
        const bool cube = item_is_block( it.item ) && item_icon_is_isometric( it.item );
        const glm::dvec3 rpos = glm::mix( it.prev_pos, it.pos, static_cast<double>( alpha ) );
        const glm::vec2 light = chunk_loader.light_sample( glm::vec3( item_center( it ) ) );
        // Spin about the vertical axis; lift bobs in [0, 0.1] (never below
        // the rest position) while the item rests on the ground or floats.
        float yaw, bob;
        render_pose( it, alpha, &yaw, &bob );
        if ( cube ) {
            if ( it.entity == entt::null ) {
                const std::pair<entt::entity, ParticlePosition &> data = this->render_cube.create_instance( );
                it.entity = data.first;
                for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                    data.second.face[ f ] = it.face[ f ];
                }
            }
            ParticlePosition &inst = this->render_cube.get_instance( it.entity );
            const float s = static_cast<float>( ITEM_SIZE );
            glm::mat4 m = glm::translate( glm::mat4( 1.0f ), glm::vec3( rpos ) + glm::vec3( 0.0f, bob + s * 0.5f, 0.0f ) );
            m = glm::rotate( m, yaw, glm::vec3( 0.0f, 1.0f, 0.0f ) );
            m = glm::scale( m, glm::vec3( s ) );
            m = glm::translate( m, glm::vec3( -0.5f ) );
            inst.transform = m;
            inst.light = light;
        } else {
            if ( it.entity == entt::null ) {
                const std::pair<entt::entity, ItemSpriteInstance &> data = this->render_sprite.create_instance( );
                it.entity = data.first;
                data.second.tex_layer = item_icon_texture( it.item );
            }
            ItemSpriteInstance &inst = this->render_sprite.get_instance( it.entity );
            // Upright sprite centered on the entity, spinning and bobbing
            // while it rests on the ground or floats.
            glm::mat4 m = glm::translate( glm::mat4( 1.0f ), glm::vec3( rpos ) + glm::vec3( 0.0f, bob, 0.0f ) );
            m = glm::rotate( m, yaw, glm::vec3( 0.0f, 1.0f, 0.0f ) );
            m = glm::scale( m, glm::vec3( 0.35f ) );
            m = glm::translate( m, glm::vec3( -0.5f, 0.0f, -0.5f * ( 1.0f / 16.0f ) ) );
            inst.transform = m;
            inst.light = light;
        }
    }
    this->render_cube.invalidate_all( );
    this->render_sprite.invalidate_all( );
}

void ItemEntities::draw( const Renderer &renderer, const Shader &object_shader, const Shader &item_sprite_shader ) {
    if ( !this->render_inited ) {
        return;
    }
    this->render_cube.draw( renderer, object_shader );
    this->render_sprite.draw( renderer, item_sprite_shader );
}

void ItemEntities::cleanup( ) {
    this->render_cube.clear( );
    this->render_sprite.clear( );
    this->items.clear( );
}
