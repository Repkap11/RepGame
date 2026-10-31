#pragma once

#include <random>
#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/mob_types.hpp"
#include "common/utils/map_storage.hpp"

class World;
class Chunk;
class ChunkLoader;

#define MAX_MOBS 256

// Client-local passive mobs. Simulation (physics, wander AI) runs on the game
// tick; rendering rebuilds one instanced RenderChain<ParticleVertex,
// MobPartInstance> per frame with CPU-evaluated bone transforms from the
// generated mob_models[] table. REPGAME_MOBS=0 disables everything;
// REPGAME_MOB_TEST="type,x,y,z,yaw;..." spawns exact mobs for captures;
// capture mode freezes AI and physics.
class MobManager {
    friend int test_mobs( );
    struct Mob {
        MobType type;
        int variant;
        glm::dvec3 pos;      // feet
        glm::dvec3 prev_pos; // previous tick's feet, for render interpolation
        float yaw;           // facing, degrees; forward = (sin(yaw), 0, -cos(yaw))
        float prev_yaw;
        glm::dvec3 vel;      // blocks/tick; y is gravity-owned like the player's y_speed
        int standing;
        float health;
        int ai_state;   // 0 = idle, 1 = wandering
        int ai_ticks;   // idle countdown or wander timeout
        int panic_ticks;
        glm::dvec3 target; // wander destination
        int hurt_ticks;
        int death_ticks; // >0 while the 0.5s tip-over plays; removed at the end
        float walk_dist; // horizontal distance walked, drives the gait phase
        float leg_amp;   // eased 0..1 gait amplitude
        float head_yaw;  // eased look-at offsets on the head bone, degrees
        float head_pitch;
        int persistent;   // REPGAME_MOB_TEST spawns are non-persistent
        int say_ticks;    // ambient "say" countdown
        float step_acc;   // horizontal distance since the last step sound
        glm::dvec3 kb;    // knockback impulse, decays ~x0.85/tick
        uint32_t uid;     // != 0 for network-driven mobs (MOB_AI_REMOTE)
        float look_yaw;   // remote look target, degrees (angle_H convention)
        float look_pitch; // remote look target, degrees (angle_V convention)
        float sneak;        // eased 0..1 crouch amount (remote pose anim)
        float prev_sneak;
        float sneak_target;
        std::vector<entt::entity> parts; // one render entity per model cube
    };
    // A chunk this manager took ownership of via adopt_chunk: its mobs file
    // was loaded (or the natural-spawn roll ran) and its mobs persist back to
    // the same file when the chunk leaves the active window.
    struct OwnedChunk {
        glm::ivec3 pos;
        int file_present;
        int spawned;
    };
    bool enabled;
    bool test_mode; // REPGAME_MOB_TEST set: no natural spawn/load/persist
    bool render_inited;
    std::vector<Mob> mobs;
    std::vector<OwnedChunk> owned_chunks;
    std::mt19937 rng;
    uint32_t next_uid;
    MapStorage *storage;
    long poll_count;
    RenderChain<ParticleVertex, MobPartInstance> render_chain;

    void tick_mob( World &world, Mob &mob, const MobTypeDef &def, const glm::dvec3 &player_pos );
    void tick_remote( Mob &mob );
    void poll_owned( World &world );
    std::vector<Mob>::iterator remove_mob( std::vector<Mob>::iterator it );
    bool spawn_any( MobType type, const glm::dvec3 &pos, float yaw, int persistent );

  public:
    MobManager( );
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance );
    void init_storage( MapStorage *storage ) { this->storage = storage; }
    bool spawn( MobType type, const glm::dvec3 &pos, float yaw, int persistent = 1 );
    // Network-driven mobs (remote players): spawned/posed/removed by the
    // multiplayer layer via a stable uid. persistent=0, invisible to the
    // raycast and the mob count, immune to the chunk-live freeze. Works even
    // with REPGAME_MOBS=0.
    uint32_t spawn_remote( MobType type );
    void set_remote_pose( uint32_t uid, const glm::dvec3 &eye_pos, float look_yaw_deg, float look_pitch_deg,
                          float eye_height = PLAYER_EYE_HEIGHT );
    void remove_remote( uint32_t uid );
    // Spawn `type` three blocks in front of `camera_pos` along `look`, dropped
    // onto the ground (persistent). Debug spawn key.
    bool spawn_in_front( MobType type, const glm::dvec3 &camera_pos, const glm::dvec3 &look, World &world );
    // AABB slab test against every live (non-dying) mob's hitbox. Returns the
    // mob index or -1; *out_dist gets the entry distance when hit.
    int raycast( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double *out_dist ) const;
    // The crosshair's mob target: the ray's mob hit when its entry distance
    // is nearer than the selected block cell's (or no block is selected),
    // else -1. -1 as well when mobs are disabled.
    int resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist,
                        int selectionInBounds, const glm::ivec3 &pos_destroy, double *out_dist ) const;
    // Apply damage + knockback away from attacker_pos; returns true if this
    // hit started the death animation.
    bool hurt( int index, float damage, const glm::dvec3 &attacker_pos );
    const MobTypeDef &def_at( int index ) const { return mob_types[ this->mobs[ index ].type ]; }
    // Drain a just-finalized chunk's pending_mobs into live mobs and record
    // ownership. Skips chunks already owned, parked/loading leftovers, and
    // test mode.
    void adopt_chunk( Chunk &chunk );
    void tick( World &world, const glm::dvec3 &player_pos );
    // Per-bone world matrices in model space (pixels) at render alpha: rest
    // pose + walk/head-look animator rotations, then the sneak pose as a
    // model-space pre-multiply. Shared by update_render and rep_tests.
    static void bone_worlds( const Mob &mob, const MobTypeDef &def, float alpha, glm::mat4 *out );
    void update_render( float alpha, const ChunkLoader &chunk_loader );
    void draw( const Renderer &renderer, const Shader &shader );
    // Persist every owned chunk's mobs (World::cleanup).
    void persist_all( );
    size_t count( ) const { return this->mobs.size( ); }
    size_t remote_count( ) const {
        size_t n = 0;
        for ( const Mob &mob : this->mobs ) {
            n += mob.uid != 0;
        }
        return n;
    }
    bool is_enabled( ) const { return this->enabled; }
    void cleanup( );
};

// Recovers angle_H/angle_V (degrees) from the rotation matrix the network
// layer ships in player-location packets: RepGame::build_camera_view makes
// rot = RY(180 - H) * RX(V). Pure math, kept here for rep_tests.
inline void mob_look_from_camera_rotation( const glm::mat4 &rot, float *yaw_deg, float *pitch_deg ) {
    // M = RY(a)*RX(b): M[1][2] = -sin b, M[2][0] = -sin a, M[0][0] = cos a
    // (glm is column-major: m[col][row]).
    *pitch_deg = glm::degrees( asinf( glm::clamp( -rot[ 2 ][ 1 ], -1.0f, 1.0f ) ) );
    const float a = atan2f( -rot[ 0 ][ 2 ], rot[ 0 ][ 0 ] );
    float yaw = 180.0f - glm::degrees( a );
    yaw = fmodf( yaw + 360.0f, 360.0f );
    *yaw_deg = yaw;
}

// Head-look animator angles for update_render's bone_anim (added to the
// bone's rest rotation in bone_local_matrix). head_yaw > 0 turns the head
// in the +yaw sense of the forward convention (sin yaw, 0, -cos yaw);
// head_pitch > 0 looks up. In the bone's pivot frame a +X rotation pitches
// the face up and a -Y rotation turns it toward +yaw.
inline glm::vec3 mob_head_anim( float head_yaw, float head_pitch ) {
    return glm::vec3( head_pitch, -head_yaw, 0.0f );
}
