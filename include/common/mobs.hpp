#pragma once

#include <random>
#include <set>
#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/mob_types.hpp"
#include "common/net/packet.hpp"
#include "common/net/net_sync.hpp"
#include "common/utils/map_storage.hpp"

class World;
class Chunk;
class ChunkLoader;
class NetEntities;
struct RepGameState;

// Gameplay cap on how many mobs a client may *create* locally (spawns,
// spawner, natural roll products). It is not a storage/render limit: the
// mobs vector must hold anything beyond it — avatars and network mobs
// (mirrors, owned transfers) never count and are never refused.
#define MAX_MOBS 256

// A remote player's targeting data (built from MultiplayerAvatars): hostile
// owned mobs pick the nearest targetable player among local + remote.
struct MobRemoteTarget {
    int32_t player_id;
    glm::dvec3 eye_pos;
    float eye_height;
    bool targetable;
};

// Optional input to MobManager::tick: NULL means the player isn't targetable,
// world_time is noon, and the night spawner stays off.
struct MobTickInput {
    bool player_targetable;
    long world_time;
    float player_eye_height; // player_pos is the eye; feet = pos - eye_height
    // Tempting ignores a no-clipping player — a spectator-style camera
    // shouldn't pull livestock.
    bool player_no_clip = false;
    // The local player's held item — breedable animals walk toward the
    // player while it is one of their breed foods (MobTypeDef::breed_items).
    ItemID held_item = LAST_BLOCK_ID;
    // Remote players (multiplayer): hostile targeting and despawn distance
    // consider these alongside the local player.
    std::vector<MobRemoteTarget> remote_targets = {};
};

// A hostile mob's melee hit on the player (damage is informational — there is
// no player health yet; RepGame turns it into knockback + player_hurt sfx).
struct MobPlayerHit {
    glm::dvec3 from;
    float damage;
    int32_t target_player_id = -1; // -1 = local player; else PLAYER_HIT target
};

// Per-tick results for RepGame: melee hits to apply, creeper explosions to
// turn into TntExplodeEvents, and farmland cells a mob trampled (the caller
// owns the world write).
struct MobTickOutput {
    std::vector<MobPlayerHit> player_hits;
    std::vector<glm::dvec3> explosions;
    std::vector<glm::ivec3> tramples;
};

// Zombies burn while daylighted under open sky (and not in water). Pure
// helper for rep_tests.
inline bool mob_burns_in_daylight( float daylight, float sky ) {
    return daylight > 0.5f && sky >= 0.95f;
}

// Night-spawner light gate: max(sky*daylight, block) must be below level 7
// (fractions of 15) — pure helper for rep_tests.
inline bool hostile_spawn_light_ok( float sky, float block, float daylight ) {
    const float v = sky * daylight > block ? sky * daylight : block;
    return v < 7.0f / 15.0f;
}

// Client-local passive mobs. Simulation (physics, wander AI) runs on the game
// tick; rendering rebuilds one instanced RenderChain<ParticleVertex,
// MobPartInstance> per frame with CPU-evaluated bone transforms from the
// generated mob_models[] table. REPGAME_MOBS=0 disables everything;
// REPGAME_MOB_TEST="type,x,y,z,yaw;..." spawns exact mobs for captures;
// capture mode freezes AI and physics.
class MobManager {
    friend int test_mobs( );
    friend int test_net_mobs( );
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
        // Hostile AI state.
        int despawnable;    // night-spawner mobs: removed on distance/unload, never persisted
        int targeting;      // currently chasing the player
        int attack_ticks;   // melee cooldown (zombie: 1 s)
        int fuse_ticks;     // creeper fuse; >0 while lit, counts down to defuse
        int burn_acc;       // daylight-burn damage accumulator ticks
        float wing_phase;   // wing-flap oscillator
        // Breeding (types with MobTypeDef::breed_items): love_ticks > 0 is
        // love mode — the mob seeks a same-type partner also in love and a
        // close pair produces a baby; breed_cooldown > 0 blocks re-entry
        // (2 min after mating). age_ticks < 0 is a baby counting up toward
        // 0 = adult, persisted through MobFileRecord::grow_ticks and
        // NetEntityRecord::age_ticks; babies render/collide at BABY_SCALE
        // and drop nothing. fall_dist accumulates downward travel for the
        // farmland-trample landing check.
        int love_ticks;
        int breed_cooldown;
        int age_ticks;
        float fall_dist;
        // Multiplayer sync (net.id 0 / net.mirror false in single player):
        // mirrors run no sim — tick_mirror eases pos/yaw toward the net
        // targets and derives gait from the applied motion.
        NetSync net;
        float net_yaw;        // mirror look target, degrees
        uint32_t net_flags;   // last seen/sent NET_MOB_FLAG_* bits
        bool net_natural;     // our own natural-spawn roll product
        // Natural-mob provenance: the deterministic roll assigns every
        // product a net_natural_mob_id (home chunk + roll index); SP stores
        // it in the file record's net_id, MP keeps it as net.id. 0 for
        // non-natural mobs. `touched` is the sticky player-interacted bit —
        // pristine naturals (natural && !touched) are never persisted.
        NetEntityId natural_id = 0;
        bool touched = false;
        // Outbound tracking for fields beyond the shared pos/vel throttle.
        float net_sent_yaw;
        float net_sent_health;
        uint32_t net_sent_flags;
        std::vector<entt::entity> parts; // one render entity per model cube
    };
    // A chunk this manager took ownership of via adopt_chunk: its mobs file
    // was loaded (or the natural-spawn roll ran) and its mobs persist back to
    // the same file when the chunk leaves the active window. `removed` holds
    // the tombstoned natural-roll indices (touched/killed naturals) written
    // alongside the records.
    struct OwnedChunk {
        glm::ivec3 pos;
        int file_present;
        std::set<uint8_t> removed;
    };
    bool enabled;
    bool spawner_enabled; // REPGAME_MOB_SPAWNER=0 disables the night spawner
    bool test_mode;       // REPGAME_MOB_TEST set: no natural spawn/load/persist
    bool render_inited;
    std::vector<Mob> mobs;
    std::vector<OwnedChunk> owned_chunks;
    std::mt19937 rng;
    uint32_t next_uid;
    MapStorage *storage;
    long poll_count;
    long spawn_ticks; // 2 s countdown for the night spawner
    RenderChain<ParticleVertex, MobPartInstance> render_chain;
    // Multiplayer sync hub; bound while multiplayer_mode (hostname given).
    NetEntities *net = nullptr;
    bool mp_mode = false;

    void tick_mob( World &world, Mob &mob, const MobTypeDef &def, const glm::dvec3 &player_pos,
                   const MobTickInput *in, MobTickOutput *out );
    void tick_remote( Mob &mob );
    void tick_mirror( Mob &mob, const glm::dvec3 &player_pos );
    void poll_owned( World &world );
    void tick_spawner( World &world, const glm::dvec3 &player_feet, const MobTickInput &in );
    std::vector<Mob>::iterator remove_mob( std::vector<Mob>::iterator it );
    bool spawn_any( MobType type, const glm::dvec3 &pos, float yaw, int persistent );
    // True while locally owned world mobs (uid == 0 && !net.mirror — not
    // avatars, not mirrors) are under MAX_MOBS. The capped spawn paths gate
    // on this; storage itself is uncapped.
    bool under_mob_cap( ) const;
    // Streamed NET_MOB_FLAG_* bits for a mob (dynamic + provenance bits).
    static uint32_t mob_net_flags( const Mob &mob );
    // Multiplayer: mint a net id for a just-spawned local mob and announce it
    // with ENTITY_SPAWN (no-op unbound / net_id already set).
    void announce_spawn( Mob &mob );
    void send_net_remove( Mob &mob, uint8_t reason );
    Mob *find_by_net_id( NetEntityId net_id );
    // First player interaction with an owned mob: sets `touched` and, for a
    // natural mob, tombstones its roll index — into the home chunk's
    // OwnedChunk when owned, else straight into its sidecar (SP only; the
    // server does this bookkeeping in multiplayer).
    void mark_touched( Mob &mob );
    // Spawn one entity from a wire record (mirror or freshly owned).
    bool spawn_record( const NetEntityRecord &rec, bool mirror );
    // Render/collision scale for a mob (0.5 for babies, 1.0 otherwise).
    static float scale_of( const Mob &mob );

  public:
    MobManager( );
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance );
    void init_storage( MapStorage *storage ) { this->storage = storage; }
    // capped=false: bypass MAX_MOBS — persisted-mob restores (adopted
    // touched mobs must never be dropped) and explicit player/debug spawns
    // (spawn eggs, `n`, dbgcmd) always go through; the cap only bounds
    // emergent population sources (natural roll, spawner, roll restores).
    bool spawn( MobType type, const glm::dvec3 &pos, float yaw, int persistent = 1, bool capped = true );
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
    // Feeding (breeding): whether the mob accepts `item` right now — a valid
    // breed food for the type, and it can take effect (a baby always eats
    // for the growth boost; an adult only while neither in love nor on the
    // post-mating cooldown). feed() applies the effect and marks the mob
    // touched; both return false on a mirror/remote/dead mob so the click
    // can fall through to block use.
    bool feedable( int index, ItemID item ) const;
    bool feed( int index, ItemID item );
    // Baby while age_ticks < 0 (also true for mirrors via NET_MOB_FLAG_BABY).
    bool is_baby_at( int index ) const;
    const MobTypeDef &def_at( int index ) const { return mob_types[ this->mobs[ index ].type ]; }
    // The mob's feet position (for dropping items where it died).
    glm::dvec3 pos_at( int index ) const { return this->mobs[ index ].pos; }
    // Drain a just-finalized chunk's pending_mobs into live mobs and record
    // ownership. Skips chunks already owned, parked/loading leftovers, and
    // test mode.
    void adopt_chunk( World &world, Chunk &chunk );
    void tick( World &world, const glm::dvec3 &player_pos, const MobTickInput *in = nullptr, MobTickOutput *out = nullptr );
    // Per-bone world matrices in model space (pixels) at render alpha: rest
    // pose + walk/head-look animator rotations, then the sneak pose as a
    // model-space pre-multiply. Shared by update_render and rep_tests.
    static void bone_worlds( const Mob &mob, const MobTypeDef &def, float alpha, glm::mat4 *out );
    void update_render( float alpha, const ChunkLoader &chunk_loader );
    void draw( const Renderer &renderer, const Shader &shader );
    // Persist every owned chunk's mobs (World::cleanup).
    void persist_all( );

    // ---- Multiplayer sync (NetEntities drives these; all no-ops in single
    // player). Local mobs_* file reads/writes are off in multiplayer mode.
    void set_multiplayer_mode( bool mp_mode, NetEntities *net );
    bool multiplayer_mode( ) const { return this->mp_mode; }
    // Whether a raycast-resolved mob is a remote-owned mirror (hitting it
    // sends ENTITY_ACTION instead of applying damage locally).
    bool net_mirror_at( int index ) const { return index >= 0 && this->mobs[ index ].net.mirror; }
    NetEntityId net_id_at( int index ) const { return index >= 0 ? this->mobs[ index ].net.id : 0; }
    // Local hurt feedback for a mirror hit (flash + sfx, no damage).
    void mirror_hit_feedback( int index, const glm::dvec3 &attacker_pos );
    // Incoming record handlers (called by NetEntities).
    void net_apply_spawn( const NetEntityRecord &rec, bool mirror );
    void net_apply_state( const NetEntityStateEntry &e );
    void net_apply_remove( NetEntityId net_id );
    // ENTITY_ACTION forwarded to us as the chunk's owner (HIT).
    void net_apply_action( RepGameState &state, const NetEntityActionPayload &p );
    // CHUNK_ROLE: drop the chunk (NONE) or re-sync its mob records
    // (VIEWER -> mirrors, OWNER -> locally simulated; populated==0 re-runs
    // the deterministic natural roll on the live chunk — skipping
    // `removed_natural` indices and ids already live locally — and reports
    // CHUNK_POPULATED).
    void net_apply_chunk_role( RepGameState &state, const glm::ivec3 &chunk, uint8_t role, uint8_t populated, const std::vector<NetEntityRecord> &records,
                               const std::vector<uint8_t> &removed_natural, const std::set<NetEntityId> &keep_ids );
    // ENTITY_TRANSFER adopt=0: become a mirror if still viewed, else drop.
    void net_release_entity( const NetEntityRecord &rec, bool still_viewed );
    // Owner-side outbound batch (same cadence/keyframes as items).
    void collect_net_state( NetEntities &net, std::vector<NetEntityStateEntry> &out, long tick_number );

    size_t count( ) const { return this->mobs.size( ); }
    // mp_test dump seam: snapshot of every live mob (remote players
    // included).
    std::vector<Mob> debug_mobs( ) const { return this->mobs; }
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
