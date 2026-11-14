#pragma once

#include <stdlib.h>

// REPGAME_BLOCK_WORLD=1 runs the "block world" debug mode: a flat sandstone
// floor with every defined block laid out on a grid instead of generated
// terrain. No map gen, structures, mobs, disk I/O or server connection.
static inline bool repgame_block_world( ) {
    static const bool enabled = getenv( "REPGAME_BLOCK_WORLD" ) != nullptr &&
                                atoi( getenv( "REPGAME_BLOCK_WORLD" ) ) != 0;
    return enabled;
}

// Per-world "flat" mode: a save dir containing a `world_type` file whose
// first token is `flat` generates a plain sandstone floor at y=0 with air
// above instead of terrain. Set by MapStorage::detect_flat_world before any
// chunk generation starts; tools/model_to_world.py writes pre-voxelized
// chunk files into such worlds.
inline bool g_repgame_flat_world = false;
static inline bool repgame_flat_world( ) {
    return g_repgame_flat_world;
}

#define MOUSE_SENSITIVITY 0.18f
// Low-pass filter factor for look deltas. Each tick the applied delta is blended
// with the previous smoothed delta: smoothed = smoothed*(1-X) + raw*X.
// 1.0 = no smoothing (raw input), lower = more smoothing (less slow-pan jitter, more lag).
#define MOUSE_SMOOTHING 0.5f
#define MOVEMENT_SENSITIVITY_WALKING 0.0326f
#define MOVEMENT_SENSITIVITY_SPRINTING 0.125f
#define MOVEMENT_SENSITIVITY_FLYING 0.3f
#define MOVEMENT_SENSITIVITY_FLYING_SPRINTING 2.0f
// Vertical fly speed (blocks/tick) when ascending/descending with jump/sneak.
#define FLY_VERTICAL_SPEED 0.5f
#define FLY_SPRINT_VERTICAL_SPEED 2.0f

// CHUNK_SIZE_*/CHUNK_RADIUS_*/CHUNK_SIZE_INTERNAL_* live in
// chunk_dimensions.hpp, which shaders also #include (see SHADER_PP).
#include "common/chunk_dimensions.hpp"

#ifdef REPGAME_WASM
// Cap the worker->render result queue: each undrained chunk holds MBs of
// populated_blocks/blocks data, so an unbounded backlog blows the 2GB heap
// during initial load-in.
#define WASM_TERRAIN_RESULT_BACKLOG 16
#endif

static_assert( CHUNK_RADIUS_XZ > 0, "CHUNK_RADIUS_XZ too small" );
static_assert( CHUNK_RADIUS_Y > 0, "CHUNK_RADIUS_Y too small" );

// Runtime render distance is a single horizontal radius (y stays at
// CHUNK_RADIUS_Y): starts at RENDER_RADIUS_DEFAULT, clamped to the torus
// capacity.
#define RENDER_RADIUS_MAX CHUNK_RADIUS_XZ
#define RENDER_RADIUS_DEFAULT CHUNK_RADIUS_XZ_DEFAULT

// Frames to defer freeing a parked chunk's terrain data. Parking flips
// load_state so every light/draw path skips the slot, but a light worker
// already inside a job that passed its load_state gate can still be
// touching the chunk's pooled buffers — freeing them mid-job recycles the
// memory under the worker (UAF). Light jobs are time-budgeted (~ms), so a
// frames defer is margin rather than a queue.
#define PARK_RELEASE_DEFER_FRAMES 30


#define CHUNK_SIZE_I glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z )
#define CHUNK_SIZE_F glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z )


#ifdef REPGAME_BUILD_WITH_CUDA
#define LOAD_CHUNKS_SUPPORTS_CUDA 1
#else
#define LOAD_CHUNKS_SUPPORTS_CUDA 0
#endif

#ifdef REPGAME_BUILD_WITH_HIP
#define LOAD_CHUNKS_SUPPORTS_HIP 1
#else
#define LOAD_CHUNKS_SUPPORTS_HIP 0
#endif

// WebGPU mapgen exists only on WASM (emdawnwebgpu port, --use-port in
// wasm.mk). The host shim is src/wasm/webgpu_mapgen.cpp.
#ifdef REPGAME_WASM
#define LOAD_CHUNKS_SUPPORTS_WEBGPU 1
#else
#define LOAD_CHUNKS_SUPPORTS_WEBGPU 0
#endif

#define DEFAULT_WINDOW_WIDTH 1600
#define DEFAULT_WINDOW_HEIGHT 800

// These impact the save file size, so don't change them.
#define MAIN_INVENTORY_WIDTH 12
#define MAIN_INVENTORY_HEIGHT 6
#define HOTBAR_WIDTH 10
#define HOTBAR_HEIGHT 1
// Survival inventory grid dimensions (Minecraft-style 9x3 main grid).
#define SURVIVAL_INVENTORY_WIDTH 9
#define SURVIVAL_INVENTORY_HEIGHT 3
// MAX_STACK_SIZE lives in common/item.hpp.

// This can be useful when debugging and you want to stop at a breakpoint.
#define ALLOW_GRAB_MOUSE 1

#define REMEMBER_BLOCKS 1
#define CULL_NON_VISIBLE 1
#define FOREST_DEBUG 0
#define DEBUG_CYCLE_AUTO_ROTATING_BLOCKS 0
#define TERRAIN_GEN_PROFILING 0
#define MESH_VERIFY 0 // When 1, runs both CPU and GPU meshing and compares results
#define MAPGEN_VERIFY 0 // When 1, runs CPU mapgen alongside WebGPU and diffs chunk cells

// REPGAME_MAP_GEN_LEGACY (terrain generator selection) and MAP_SEED moved to
// map_gen_consts.hpp so the WGSL generation pass can see them without pulling
// in C++ declarations — included here to keep this header's old contract.
#include "common/map_gen_consts.hpp"

#define SHOW_IMGUI 1

#define BLOCK_SCALE_OFFSET 0.2f

#define DISABLE_GROUPING_BLOCKS 0
#define REACH_DISTANCE 7

#define CAMERA_SIZE 0.01f // Defines how much crop is in front (low for minecraft)
#define PERSON_HEIGHT 1.62f
#define DRAW_DISTANCE 1000

#define TERMINAL_VELOCITY 0.5f
#define JUMP_STRENGTH 0.07f
#define GRAVITY_STRENGTH 0.002f

// Swimming: active while the player's body (not just feet) is in a liquid.
// Movement is walking-like — WASD moves on the yaw plane at reduced speed,
// jump swims up and sneak sinks; gravity is weak and vertical speed drags
// to rest when neither is held.
#define MOVEMENT_SENSITIVITY_SWIMMING 0.016f
#define MOVEMENT_SENSITIVITY_SWIMMING_SPRINTING 0.05f
// Feet in water but body above the surface: slowed walk, normal jump.
#define MOVEMENT_SENSITIVITY_WADING 0.022f
#define MOVEMENT_SENSITIVITY_WADING_SPRINTING 0.06f
#define SWIM_VERTICAL_SPEED 0.045f
#define SWIM_SINK_SPEED 0.06f
// Gravity is much weaker in water and vertical velocity decays each tick, so
// idle players sink gently and fall momentum from above dies quickly.
#define WATER_GRAVITY_STRENGTH 0.0004f
#define WATER_VERTICAL_DRAG 0.9f
#define SWIM_TERMINAL_VELOCITY 0.15f
#define PLAYER_WATER_ACCEL 0.05f
#define PLAYER_WATER_FRICTION 0.04f

// Climbing (blocks flagged "climbable" — ladders): while the body AABB
// overlaps a climbable cell, pressing forward into a blocked wall or
// jumping ascends at CLIMB_VERTICAL_SPEED, sneak hangs in place, and with
// no input gravity keeps pulling but the descent is capped at
// CLIMB_SLIDE_SPEED.
#define CLIMB_VERTICAL_SPEED 0.02f
#define CLIMB_SLIDE_SPEED 0.025f

// Collision: max distance the player may move on a single axis per collision
// substep. Must stay below the smallest player AABB half-dimension (PLAYER_WIDTH/2
// = 0.3) so a single substep can never tunnel through a 1-block-thick wall.
#define COLLISION_MAX_SUBSTEP 0.27f
// Collision: how high the player can step up without jumping. Covers slabs
// (0.5) and snow layers; full blocks (1.0) still require a jump. Set slightly
// above 0.5 so floating-point error doesn't prevent stepping a half slab.
#define STEP_HEIGHT 0.6f

// Movement feel: horizontal velocity is accelerated toward a target velocity
// derived from input and decelerated by friction when no input is held. All
// values are in blocks/tick. Ground accel is high so walking still feels
// responsive; air accel is lower for some in-air control; friction brings the
// player to rest quickly on the ground. Flying uses a much higher accel so it
// stays snappy.
#define PLAYER_GROUND_ACCEL 0.4f
#define PLAYER_AIR_ACCEL 0.08f
#define PLAYER_GROUND_FRICTION 0.25f
#define PLAYER_AIR_FRICTION 0.02f
#define PLAYER_FLY_ACCEL 1.0f

#define UPS_RATE 144

// MSAA sample count for the offscreen FBO. Capped at 4x: above that the
// per-sample cost grows steeply while geometric edge quality barely improves.
// Alpha-to-coverage for foliage also gets 4 levels of edge smoothing at 4x.
#define MSAA_SAMPLES 4

// Opaque chunk quads are pushed outward this many pixels in screen
// space (chunk_vertex.glsl) so greedy-meshed neighbours overlap on screen
// instead of meeting at T-junctions, where rounding leaves pinhole cracks
// that leak the sky — even on faces seen nearly edge-on.
#define CHUNK_FACE_GROW_PX 0.125f

// Almost the number of clients, but some fd's are not clients so it's slightly less.
#define MAX_CLIENT_FDS 100

#define CAMERA_FOV 60.0f
#if defined( REPGAME_WINDOWS )
#define NUM_RENDER_THREADS 1
#else
#define NUM_RENDER_THREADS 8
#endif

#define PLAYER_HEIGHT 1.85f
#define PLAYER_WIDTH 0.6f
#define PLAYER_EYE_HEIGHT 1.62f

// Sneaking pose (Minecraft-like): shorter body, lower eye. The 0.23 gap
// between eye and head stays the same as standing, so only the feet-to-eye
// distance changes.
#define PLAYER_SNEAK_HEIGHT 1.5f
#define PLAYER_SNEAK_EYE_HEIGHT 1.27f
#define PLAYER_SNEAK_EYE_DROP ( PLAYER_EYE_HEIGHT - PLAYER_SNEAK_EYE_HEIGHT )
// Sneaking walks at half normal speed; sprint-sneaking is faster but still
// well below a full sprint.
#define MOVEMENT_SENSITIVITY_SNEAKING ( MOVEMENT_SENSITIVITY_WALKING * 0.5f )
#define MOVEMENT_SENSITIVITY_SNEAKING_SPRINTING ( MOVEMENT_SENSITIVITY_WALKING * 1.5f )

// Player health: 20 half-hearts (10 hearts on the HUD). Damage below
// applies in survival only (creative is invulnerable). HURT_COOLDOWN is the
// post-hit invulnerability window that also paces periodic sources
// (lava, fire, drowning) — all of them strike at most once per cooldown.
#define PLAYER_MAX_HEALTH 20.0f
#define PLAYER_HURT_COOLDOWN_TICKS ( UPS_RATE / 2 )
// Breath meter: ~15 s of air shown as 10 bubbles (1.5 s each), then
// drowning damage per hurt cooldown.
#define PLAYER_AIR_MAX_TICKS ( 15 * UPS_RATE )
#define PLAYER_DROWN_DAMAGE 2.0f
// Per-tick fall distance (blocks) above which a landing damages; MC's
// rule: floor(distance) - 3 hp.
#define PLAYER_FALL_SAFE_DISTANCE 3.0
// Standing in lava burns fast; standing in fire burns slowly — both paced
// by the hurt cooldown (~0.5 s), so these are ~2x per second.
#define PLAYER_LAVA_DAMAGE 4.0f
#define PLAYER_FIRE_DAMAGE 1.0f
// Explosion damage falls off linearly from EXPLOSION_MAX_DAMAGE at the
// center to 0 at this radius (same reach as the entity impulse).
#define PLAYER_EXPLOSION_MAX_DAMAGE 16.0f
// Peaceful regeneration: +1 hp per interval while the "Mobs" toggle is
// peaceful, survival, alive and hurt.
#define PLAYER_PEACEFUL_REGEN_TICKS ( 4 * UPS_RATE )
// Bed sleeping ("monsters nearby"): a live hostile mob within this
// distance of the player blocks the sleep (MC's 8-block check).
#define SLEEP_MONSTER_RADIUS 8.0
// Sleep lands this long past the dawn boundary: the daylight factor is
// still sunrise-dim at exact dawn (~0.24) and ~0.87 here — reads as
// morning instead of twilight (light.cpp light_daylight_factor).
#define SLEEP_WAKE_OFFSET ( 30 * UPS_RATE )
// Right-click eat also sets the standard click cadence (30 ticks) — same
// as the bow, so holding the button eats at ~5/s.

// #define EYE_POSITION_OFFSET 0.1f
#define EYE_POSITION_OFFSET ( PLAYER_EYE_HEIGHT - ( PLAYER_HEIGHT / 2.0f ) )

// Cube-topology defines (FACE_*, ISO_FACE_*, BLOCK_ROTATE_*,
// NUM_FACES_IN_CUBE, CORNER_OFFSET_*) and the NO_LIGHT_* packed-lighting
// sentinels moved to common/block_types.hpp.

// Redstone power emitted by a pressed button/pressure plate (same as a torch).
#define REDSTONE_SOURCE_POWER 15

// The redstone clock rate: MC game ticks ("gt"), 20 per second.
#define REDSTONE_UPS 20
// MC redstone-torch reaction delay: 2 gt.
#define REDSTONE_TORCH_DELAY_GT 2
// MC redstone ticks are 2 game ticks each: a repeater waits 2 * delay gt.
#define REDSTONE_REPEATER_STEP_GT 2
// MC piston move duration: the moving_piston slide takes 2 gt (the block
// event itself starts on the edge's game tick, so there is no extra delay).
#define PISTON_MOVE_GT 2
// MC door open/close on a power edge: 1 gt.
#define DOOR_DELAY_GT 1
// MC button press durations: stone 1 s, wood 1.5 s.
#define STONE_BUTTON_PRESS_GT 20
#define WOOD_BUTTON_PRESS_GT 30
// MC pressure-plate release: ~1 s after stepping off (the recheck cadence).
#define PRESSURE_PLATE_RECHECK_GT 20

// Bounded timeout for the background TCP connect attempt to a multiplayer
// server. Prevents the OS default (~75s+) from stalling if SYNs are silently
// dropped by a down/unreachable host.
#define MULTIPLAYER_CONNECT_TIMEOUT_MS 5000