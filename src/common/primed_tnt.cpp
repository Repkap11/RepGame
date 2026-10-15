#include "common/RepGame.hpp"
#include "common/primed_tnt.hpp"
#include "common/block.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk_loader.hpp"

#include <stdlib.h>
#include <math.h>

#include <chrono>

// u_Time is fmod(now, 3600) in RepGame::draw; sweep must match that clock.
#define TIME_WRAP 3600.0f
// Seed range feeding the shader's per-instance hash (kept for future jitter).
#define PRIMED_SEED_RANGE 1000.0f

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

static float frand( ) {
    return static_cast<float>( rand( ) ) / static_cast<float>( RAND_MAX );
}

void PrimedTnt::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_primed_tnt_instance ) {
    this->render_chain.init( vbl_object_vertex, vbl_primed_tnt_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
}

entt::entity PrimedTnt::spawn( const glm::vec3 &center, const glm::vec3 &velocity, const float life_s, const float ground_t, const glm::vec2 &light ) {
    const float time_s = static_cast<float>( fmod( now_us( ) / 1.0e6, TIME_WRAP ) );
    const std::pair<entt::entity, PrimedTntInstance &> data = this->render_chain.create_instance( );
    PrimedTntInstance &d = data.second;

    // Resolve the displayed textures the same way chunk meshing does
    // (chunk.cpp): per-face lookup, then state-based adjustment.
    const BlockState block_state = { TNT, 0, 0, TNT, 0 };
    const Block *block = block_definition_get_definition( TNT );
    BlockCoords coords = {};
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        coords.face[ f ] = block->textures[ f ];
    }
    block_adjust_coord_based_on_state( block, &block_state, &coords );
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        // Same -1 as chunk meshing (chunk.cpp): the shader offsets by 1.
        d.face[ f ] = coords.face[ f ] - 1;
    }

    d.spawn = center;
    d.velocity = velocity;
    d.anim = glm::vec4( time_s, life_s, ground_t, frand( ) * PRIMED_SEED_RANGE );
    d.light = light;
    this->live.push_back( { data.first, time_s, life_s } );
    return data.first;
}

void PrimedTnt::despawn( const entt::entity entity ) {
    for ( size_t i = 0; i < this->live.size( ); i++ ) {
        if ( this->live[ i ].entity == entity ) {
            this->render_chain.remove( entity );
            this->live[ i ] = this->live.back( );
            this->live.pop_back( );
            return;
        }
    }
}

void PrimedTnt::update_lighting( const ChunkLoader &chunk_loader ) {
    for ( const LivePrimed &p : this->live ) {
        PrimedTntInstance &d = this->render_chain.get_instance( p.entity );
        // The ignited cell's light settles a few ticks after the block
        // leaves; resample so the entity doesn't keep stale light.
        const glm::vec2 light = chunk_loader.light_sample( d.spawn );
        if ( d.light != light ) {
            d.light = light;
            this->render_chain.invalidate( p.entity );
        }
    }
}

void PrimedTnt::draw( const Renderer &renderer, const Shader &shader, const float time_s ) {
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

void PrimedTnt::cleanup( ) {
    this->render_chain.clear( );
    this->live.clear( );
}
