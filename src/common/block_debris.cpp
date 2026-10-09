#include "common/RepGame.hpp"
#include "common/block_debris.hpp"
#include "common/block.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk_loader.hpp"

#include <stdlib.h>
#include <math.h>

#include <chrono>

#define DEBRIS_PER_BLOCK 32
// Lifetime seconds.
#define DEBRIS_LIFE_MIN 0.45f
#define DEBRIS_LIFE_SPREAD 0.4f
// Spawn offsets: particles start within +/- half this of the block center.
#define DEBRIS_SPAWN_SPREAD 0.7f
// Velocity (blocks/sec): outward radial push, sideways jitter, upward pop.
#define DEBRIS_VEL_RADIAL 1.0f
#define DEBRIS_VEL_JITTER 0.4f
#define DEBRIS_VEL_UP_MIN 1.2f
#define DEBRIS_VEL_UP_SPREAD 1.6f
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

void BlockDebris::spawn_block_break( const glm::ivec3 &block_pos, const BlockState &blockState, const glm::vec2 &light ) {
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
    unsigned short candidates[ NUM_FACES_IN_CUBE ];
    int num_candidates = 0;
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        if ( coords.face[ f ] == AIR ) {
            continue;
        }
        candidates[ num_candidates++ ] = coords.face[ f ];
    }
    if ( num_candidates == 0 ) {
        return;
    }
    const glm::vec3 center = glm::vec3( block_pos ) + 0.5f;
    for ( int i = 0; i < DEBRIS_PER_BLOCK; i++ ) {
        const std::pair<entt::entity, DebrisInstance &> data = this->render_chain.create_instance( );
        DebrisInstance &d = data.second;
        // Cycle candidates across the 6 slots so the shader's random face pick
        // only ever lands on a real texture, weighted by real-face count.
        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
            // Same -1 as chunk meshing (chunk.cpp): the shader offsets by 1.
            d.face[ f ] = candidates[ f % num_candidates ] - 1;
        }
        // Spawn jittered inside the block; drift radially out from its center.
        d.spawn = center + glm::vec3( frand( ) - 0.5f, frand( ) - 0.5f, frand( ) - 0.5f ) * DEBRIS_SPAWN_SPREAD;
        const glm::vec3 radial = d.spawn - center;
        d.velocity = glm::vec3( radial.x * DEBRIS_VEL_RADIAL + ( frand( ) - 0.5f ) * DEBRIS_VEL_JITTER, //
                                DEBRIS_VEL_UP_MIN + frand( ) * DEBRIS_VEL_UP_SPREAD,
                                radial.z * DEBRIS_VEL_RADIAL + ( frand( ) - 0.5f ) * DEBRIS_VEL_JITTER );
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
