#include "common/heart_particles.hpp"
#include "common/block.hpp" // ib_data_face / IB_FACE_SIZE
#include "common/texture_ids_gen.hpp"

#include <stdlib.h>
#include <math.h>

#include <chrono>

// Lifetime seconds — Minecraft hearts last about a second.
#define HEART_LIFE_MIN 0.8f
#define HEART_LIFE_SPREAD 0.6f
// Drift (blocks/sec): a slow rise with a little horizontal scatter.
#define HEART_VEL_UP_MIN 0.4f
#define HEART_VEL_UP_SPREAD 0.3f
#define HEART_VEL_JITTER 0.25f
// Spawn scatter around the mob's head (blocks).
#define HEART_SPAWN_JITTER 0.3f
// Particle size as a fraction of a block.
#define HEART_SIZE_MIN 0.15f
#define HEART_SIZE_SPREAD 0.08f
// Range of the per-particle seed feeding the shader's sway hash.
#define HEART_SEED_RANGE 1000.0f
// u_Time is fmod(now, 3600) in RepGame::draw; sweep must match that clock.
#define TIME_WRAP 3600.0f

float HeartParticles::now( ) {
    const long long us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
    return static_cast<float>( fmod( us / 1.0e6, TIME_WRAP ) );
}

static float frand( ) {
    return static_cast<float>( rand( ) ) / static_cast<float>( RAND_MAX );
}

void HeartParticles::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_debris_instance ) {
    this->render_chain.init( vbl_object_vertex, vbl_debris_instance, vd_data_face_object, VB_DATA_SIZE_FACE, ib_data_face, IB_FACE_SIZE );
    this->inited = true;
}

bool HeartParticles::expired( const float time_s, const float spawn_time, const float life ) {
    float age = time_s - spawn_time;
    // Only a clock rollover wraps; a small negative age just means the
    // particle was stamped later than the frame's time_s was captured.
    if ( age < -TIME_WRAP * 0.5f ) {
        age += TIME_WRAP;
    } else if ( age < 0.0f ) {
        age = 0.0f;
    }
    return age >= life;
}

void HeartParticles::spawn( const glm::dvec3 &pos, const glm::vec2 &light, const int count, const float time_s ) {
    if ( !this->inited ) {
        return;
    }
    for ( int i = 0; i < count; i++ ) {
        const std::pair<entt::entity, DebrisInstance &> data = this->render_chain.create_instance( );
        DebrisInstance &d = data.second;
        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
            d.face[ f ] = TEX_HEART;
        }
        d.spawn = glm::vec3( pos ) + glm::vec3( ( frand( ) - 0.5f ) * HEART_SPAWN_JITTER, frand( ) * 0.1f, ( frand( ) - 0.5f ) * HEART_SPAWN_JITTER );
        d.velocity = glm::vec3( ( frand( ) - 0.5f ) * HEART_VEL_JITTER, //
                                HEART_VEL_UP_MIN + frand( ) * HEART_VEL_UP_SPREAD,
                                ( frand( ) - 0.5f ) * HEART_VEL_JITTER );
        const float life = HEART_LIFE_MIN + frand( ) * HEART_LIFE_SPREAD;
        d.anim = glm::vec4( time_s, life, HEART_SIZE_MIN + frand( ) * HEART_SIZE_SPREAD, frand( ) * HEART_SEED_RANGE );
        d.light = light;
        d.texel = glm::vec2( 0.0f );
        this->live.push_back( { data.first, time_s, life } );
    }
}

void HeartParticles::draw( const Renderer &renderer, const Shader &shader, const float time_s ) {
    for ( size_t i = 0; i < this->live.size( ); ) {
        if ( HeartParticles::expired( time_s, this->live[ i ].spawn_time, this->live[ i ].life ) ) {
            this->render_chain.remove( this->live[ i ].entity );
            this->live[ i ] = this->live.back( );
            this->live.pop_back( );
        } else {
            i++;
        }
    }
    this->render_chain.draw( renderer, shader );
}

void HeartParticles::cleanup( ) {
    this->render_chain.clear( );
    this->live.clear( );
}
