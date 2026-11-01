#pragma once

// Multiplayer end-to-end test harness (client side). Driven entirely by env
// vars; zero cost when unset.
//
//   REPGAME_MP_CTL=<path>    — append-only command file; each game tick
//                            executes any newly appended complete lines.
//   REPGAME_MP_DUMP=<path>   — JSONL output: periodic "state" lines plus
//                            immediate "event" lines.
//   REPGAME_MP_DUMP_EVERY=n  — state dump cadence in ticks (default 5).
//   REPGAME_PORT=n           — server port override (read in RepGame::init).
//
// Commands (one per line, whitespace separated):
//   goto x y z                     pin the camera (re-aims REPGAME_FLY speed 0)
//   drop <item> <count> x y z [vx vy vz]
//                                  spawn a dropped-item entity via the normal
//                                  item spawn path (net_id + ENTITY_SPAWN)
//   mob <type> x y z               spawn a mob via MobManager::spawn
//   hit <net_id>                   player melee hit on that mob (local damage,
//                                  or feedback + ENTITY_ACTION on a mirror)
//   fall <block> x y z             spawn a falling block body
//   tnt x y z                      spawn a primed TNT body (normal ignite path)
//   setblock x y z <block>         normal player edit (BLOCK_UPDATE)
//   probe x y z                    emit a "probe" event with the block there
//   time <ticks>                   set world time (and broadcast it)
//   dump [tag]                     force a state dump right now
//   quit                           clean exit through the normal path
//
// Events on the dump stream: welcome, ready (connected + WELCOME + the
// camera's chunk is CHUNK_LIVE + at least one CHUNK_ROLE grant applied),
// cmd_error, explosion_received, pickup_result, player_hit_received,
// entity_transfer, probe, quit.

struct RepGameState;

namespace mp_test {
bool active( );
// Once at game init: reads env, opens the dump/ctl files.
void init( );
// Every game tick: drains new ctl lines, emits `ready` once, writes the
// periodic state dump.
void tick( RepGameState &state );
// Emit an immediate {"type":"event","name":...} line; `fmt` is a printf
// fragment appended as the "detail" JSON object's contents (may be empty).
void event( RepGameState *state, const char *name, const char *fmt = nullptr, ... );
// Force one state line (also used by the `dump` command).
void dump_state( RepGameState &state, const char *tag = nullptr );
} // namespace mp_test
