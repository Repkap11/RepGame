#include "common/RepGame.hpp"
#include "common/block_debris.hpp"
#include "common/block.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk_loader.hpp"

#include <stdlib.h>
#include <math.h>

#include <chrono>

// Lifetime seconds.
#define DEBRIS_LIFE_MIN 0.45f
#define DEBRIS_LIFE_SPREAD 0.4f
// Particle-count floor: the budget scales with the drawn cuboid's volume,
// but even a torch (~1% of a block) gets a few particles.
#define DEBRIS_MIN_PARTICLES 4
// Velocity (blocks/sec): outward radial push, sideways jitter, upward pop.
#define DEBRIS_VEL_RADIAL 1.0f
#define DEBRIS_VEL_JITTER 0.4f
#define DEBRIS_VEL_UP_MIN 1.2f
#define DEBRIS_VEL_UP_SPREAD 1.6f
// Outward-push falloff toward the cuboid surface: 1.0 means a particle that
// spawns on a face gets no radial velocity — it pops up where it sat
// instead of streaking through whatever the block touched.
#define DEBRIS_VEL_BORDER_DAMP 1.0f
// Particle size as a fraction of a block.
#define DEBRIS_SIZE_MIN 0.03f
#define DEBRIS_SIZE_SPREAD 0.04f
// Range of the per-particle seed feeding the shader's tumble hash.
#define DEBRIS_SEED_RANGE 1000.0f
// u_Time is fmod(now, 3600) in RepGame::draw; sweep must match that clock.
#define TIME_WRAP 3600.0f

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

static float frand( ) {
    return static_cast<float>( rand( ) ) / static_cast<float>( RAND_MAX );
}

void BlockDebris::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_debris_instance ) {
    this->render_chain.init( vbl_object_vertex, vbl_debris_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
}

void BlockDebris::spawn_block_break( const glm::ivec3 &block_pos, const BlockState &blockState, const glm::vec2 &light, const int num_particles ) {
    const float time_s = static_cast<float>( fmod( now_us( ) / 1.0e6, TIME_WRAP ) );
    const Block *block = block_definition_get_definition( blockState.id );
    // Resolve the displayed textures the same way chunk meshing does
    // (chunk.cpp): per-face lookup, then state-based adjustment.
    BlockCoords coords = {};
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        coords.face[ f ] = block->textures[ f ];
    }
    block_adjust_coord_based_on_state( block, &blockState, &coords );

    // Keep only faces that actually render: AIR marks faces that never draw
    // (dust sides, door tops/bottoms, cross-rendered flower tops/bottoms).
    int candidates[ NUM_FACES_IN_CUBE ];
    int num_candidates = 0;
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        if ( coords.face[ f ] == AIR ) {
            continue;
        }
        candidates[ num_candidates++ ] = f;
    }
    if ( num_candidates == 0 ) {
        return;
    }

    // The drawn cuboid in 16ths of a block, with the same rotation transform
    // the chunk shader applies (chunk_vertex.glsl), so particles spawn inside
    // the geometry that was actually visible.
    glm::ivec3 scale16( block->scale.x, block->scale.y, block->scale.z );
    glm::ivec3 offset16( block->offset.x, block->offset.y, block->offset.z );
    if ( blockState.rotation == BLOCK_ROTATE_90 ) {
        const int sx = scale16.x;
        scale16.x = scale16.z;
        scale16.z = sx;
        const int ox = offset16.x;
        offset16.x = 16 - block->scale.z - block->offset.z;
        offset16.z = ox;
    } else if ( blockState.rotation == BLOCK_ROTATE_180 ) {
        offset16.x = 16 - block->scale.x - block->offset.x;
        offset16.z = 16 - block->scale.z - block->offset.z;
    } else if ( blockState.rotation == BLOCK_ROTATE_270 ) {
        const int sx = scale16.x;
        scale16.x = scale16.z;
        scale16.z = sx;
        const int ox = offset16.x;
        offset16.x = block->offset.z;
        offset16.z = 16 - block->scale.x - ox;
    }
    // Wall-mounted torches (rotation 4-7) hug their wall — the same offset
    // push chunk.cpp applies.
    if ( block->is_torch ) {
        if ( blockState.rotation == 4 ) {
            offset16.x = 0;
        } else if ( blockState.rotation == 5 ) {
            offset16.z = 14;
        } else if ( blockState.rotation == 6 ) {
            offset16.x = 14;
        } else if ( blockState.rotation == 7 ) {
            offset16.z = 0;
        }
    }

    const glm::vec3 box_min = glm::vec3( block_pos ) + glm::vec3( offset16 ) / 16.0f;
    const glm::vec3 box_size = glm::vec3( scale16 ) / 16.0f;
    const glm::vec3 box_center = box_min + box_size * 0.5f;
    const glm::vec3 spawn_half = glm::max( box_size * 0.5f, glm::vec3( 1e-4f ) );

    // The particle budget scales with the drawn volume — a torch is ~1% of a
    // block — with a floor so thin blocks still visibly pop.
    const float volume = static_cast<float>( scale16.x * scale16.y * scale16.z ) / 4096.0f;
    const int count = glm::max( DEBRIS_MIN_PARTICLES, static_cast<int>( lroundf( num_particles * volume ) ) );

    const glm::ivec3 tex_off( block->tex_offset.x, block->tex_offset.y, block->tex_offset.z );
    const glm::ivec3 blk_scale( block->scale.x, block->scale.y, block->scale.z );

    for ( int i = 0; i < count; i++ ) {
        const std::pair<entt::entity, DebrisInstance &> data = this->render_chain.create_instance( );
        DebrisInstance &d = data.second;
        // Pick the face here: every slot carries its texture so the shader can
        // read any of them (it uses slot 0).
        const int face = candidates[ rand( ) % num_candidates ];
        const unsigned short texture = coords.face[ face ];
        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
            // Same -1 as chunk meshing (chunk.cpp): the shader offsets by 1.
            d.face[ f ] = texture - 1;
        }
        // Sampled texel window per axis: chunk_vertex's v_TexCoordBlock maps a
        // face to tile range [-tex_offset, scale - tex_offset). Staying inside
        // it keeps particles off the transparent margins of cropped sprites.
        int u_axis, v_axis;
        if ( face == FACE_TOP || face == FACE_BOTTOM ) {
            u_axis = 0;
            v_axis = 2;
        } else if ( face == FACE_LEFT || face == FACE_RIGHT ) {
            u_axis = 2;
            v_axis = 1;
        } else { // FACE_FRONT / FACE_BACK
            u_axis = 0;
            v_axis = 1;
        }
        const auto texel = [ &tex_off, &blk_scale ]( const int axis ) {
            const int lo = glm::clamp( -tex_off[ axis ], 0, 15 );
            const int hi = glm::clamp( -tex_off[ axis ] + blk_scale[ axis ], lo + 1, 16 );
            return lo + static_cast<int>( frand( ) * ( hi - lo ) );
        };
        d.texel = glm::vec2( ( texel( u_axis ) + 0.5f ) / 16.0f, ( texel( v_axis ) + 0.5f ) / 16.0f );
        // Spawn inside the drawn cuboid; drift radially out from its
        // center, damped toward the surface.
        d.spawn = box_min + glm::vec3( frand( ), frand( ), frand( ) ) * box_size;
        const glm::vec3 radial = d.spawn - box_center;
        const glm::vec3 edge = glm::abs( radial ) / spawn_half;
        const float border_t = glm::clamp( glm::max( edge.x, glm::max( edge.y, edge.z ) ), 0.0f, 1.0f );
        const float push = DEBRIS_VEL_RADIAL * ( 1.0f - DEBRIS_VEL_BORDER_DAMP * border_t );
        d.velocity = glm::vec3( radial.x * push + ( frand( ) - 0.5f ) * DEBRIS_VEL_JITTER, //
                                DEBRIS_VEL_UP_MIN + frand( ) * DEBRIS_VEL_UP_SPREAD,
                                radial.z * push + ( frand( ) - 0.5f ) * DEBRIS_VEL_JITTER );
        const float life = DEBRIS_LIFE_MIN + frand( ) * DEBRIS_LIFE_SPREAD;
        d.anim = glm::vec4( time_s, life, DEBRIS_SIZE_MIN + frand( ) * DEBRIS_SIZE_SPREAD, frand( ) * DEBRIS_SEED_RANGE );
        d.light = light;
        this->live.push_back( { data.first, time_s, life } );
    }
}

void BlockDebris::update_lighting( const ChunkLoader &chunk_loader ) {
    for ( const LiveParticle &p : this->live ) {
        DebrisInstance &d = this->render_chain.get_instance( p.entity );
        // The spawn point sits inside the broken cell — the trilinear sample
        // blends in its already-lit neighbors, then converges to the cell's
        // true value once the recheck writes it.
        const glm::vec2 light = chunk_loader.light_sample( d.spawn );
        if ( d.light != light ) {
            d.light = light;
            this->render_chain.invalidate( p.entity );
        }
    }
}

void BlockDebris::draw( const Renderer &renderer, const Shader &shader, const float time_s ) {
    for ( size_t i = 0; i < this->live.size( ); ) {
        float age = time_s - this->live[ i ].spawn_time;
        if ( age < 0.0f ) {
            age += TIME_WRAP;
        }
        if ( age >= this->live[ i ].life ) {
            this->render_chain.remove( this->live[ i ].entity );
            this->live[ i ] = this->live.back( );
            this->live.pop_back( );
        } else {
            i++;
        }
    }
    this->render_chain.draw( renderer, shader );
}

void BlockDebris::cleanup( ) {
    this->render_chain.clear( );
    this->live.clear( );
}
