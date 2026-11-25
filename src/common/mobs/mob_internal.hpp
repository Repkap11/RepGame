#pragma once

#include "common/mobs.hpp"

class World;

// Bedrock modified_distance_moved / Java limbSwing advance about 4 units per
// block; the quadruped walk anim's 38.17°/unit is calibrated against that.
#define MOB_GAIT_UNITS_PER_BLOCK 4.0f

// Breeding timers (UPS ticks): love mode lasts until a mate reaches the
// mob; a mated pair can't re-breed for 2 min; a baby grows up in ~5 min.
#define MOB_BREED_COOLDOWN_TICKS ( 2 * 60 * UPS_RATE )
#define MOB_BABY_AGE_TICKS ( 5 * 60 * UPS_RATE )
// Animals notice a held breed item within this distance of the player.
#define MOB_TEMPT_RANGE 10.0
// Babies render and collide at half the adult hitbox scale.
#define MOB_BABY_SCALE 0.5f
// Breeding hearts: feeding opens a ~5 s window with one heart per ~0.5 s;
// a burst on a newborn, a smaller puff when a baby eats.
#define HEART_TICKS ( 5 * UPS_RATE )
#define HEART_PERIOD_MIN 0.35f
#define HEART_PERIOD_SPREAD 0.5f
#define HEART_BORN_COUNT 7
#define HEART_BABY_FEED_COUNT 2
// Grazing (sheep): only a shorn sheep eats — idling on grass it starts a
// ~2 s graze about every 2.5 s until the wool regrows, then stops for good
// (Java's woolly ambient graze is skipped on purpose: grass out of tick
// range can't spread back, so remote pastures would degrade to dirt).
// graze_ticks pins the AI while the head dips.
#define MOB_GRAZE_TICKS ( 2 * UPS_RATE )
#define MOB_GRAZE_PITCH -80.0f // head-down animator pitch while grazing, degrees
// Ranged hostiles (the skeleton): they hold a strafing stand-off band
// instead of closing to melee — chase beyond FAR, circle sideways inside
// the band, drift back under NEAR — and loose a mob-fired arrow about
// every 2 s, only on a clear pick-ray line to the target's chest.
#define MOB_RANGED_NEAR 5.0
#define MOB_RANGED_FAR 14.0
#define MOB_RANGED_SHOT_RANGE 16.0
#define MOB_GHAST_SHOT_RANGE 30.0
#define MOB_ARROW_SPEED 0.35 // slower than a full player draw (ARROW_SPEED)
// Soft crowding separation, per physics tick. The response is deliberately
// positional rather than momentum: overlap closes smoothly at ordinary walk
// speeds, while the caps bound dense-crowd jitter and deep-overlap pops.
#define MOB_CROWD_RESPONSE 0.20
#define MOB_CROWD_PAIR_MAX 0.02
#define MOB_CROWD_TOTAL_MAX 0.028
#define MOB_CROWD_EPS 1e-4
// HeartParticles::now wraps with u_Time (3600 s) — wrapped-diff for the
// per-mob emission cadence.
#define HEART_TIME_WRAP 3600.0f

namespace mob_terrain {
bool solid_at( World &world, const glm::ivec3 &pos );
bool spawnable_air_at( World &world, const glm::ivec3 &pos );
double ground_top_at( World &world, int bx, int bz, int from_y, int max_drop, bool *found );
bool hover_solid_at( World &world, int bx, int y, int bz );
double hover_ground_at( World &world, int bx, int bz, int from_y, int max_drop, bool *found );
double hover_ground_ahead_at( World &world, int bx, int bz, int from_y, int max_drop, double pass_h, bool *found );
} // namespace mob_terrain
