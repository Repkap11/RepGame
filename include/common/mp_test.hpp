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
//   feed x y z [item]              feed the mob nearest the point (default:
//                                  its first breed food)
//   shear x y z                    shear the shearable mob nearest the point
//   fall <block> x y z             spawn a falling block body
//   tnt x y z                      spawn a primed TNT body (normal ignite path)
//   shoot fx fy fz dx dy dz        release an arrow from (f) along (d)
//   setblock x y z <block>         normal player edit (BLOCK_UPDATE)
//   give <item> [count]            add to the hotbar like a real pickup
//   damage <hp>                    player_damage on the local player; emits
//                                  damage_result {applied,health,dead}
//   eat                            right-click eat of the held food; emits
//                                  eat_result {applied,health}
//   respawn                        death-screen Respawn; emits respawn_result
//   probe x y z                    emit a "probe" event with the block there
//   role x y z                     emit a "role" event with this client's
//                                  role for that chunk
//   copen kind x y z               subscribe to the container (CONTAINER_OPEN)
//   cclose kind x y z              unsubscribe (CONTAINER_CLOSE)
//   cset kind x y z slot item n    optimistic slot write + CONTAINER_SET_SLOT
//   cprobe kind x y z              emit a "cprobe" event with the world-owned
//                                  contents (kind, is-block flag, rev, slots,
//                                  aux) — the sync result the server left
//   chat <rest of line>            submit the raw remainder through the
//                                  chat box's real path — plain text sends
//                                  CHAT (PLAYER), "/cmd ..." runs the local
//                                  slash-command dispatcher
//   signedit x y z [l1|l2|...]     open the sign editor, inject the joined
//                                  lines ('|' splits, args join on spaces),
//                                  and commit — the real close path sends
//                                  SIGN_TEXT like an Enter-to-save
//   signprobe x y z                emit a "signprobe" event with the stored
//                                  text {pos,has,lines} — the sync result
//                                  the server left on this client
//   mode survival|creative         pin the game mode each tick (survival
//                                  also pins walking state so the
//                                  REPGAME_FLY pin can't undo it)
//   peaceful on|off                off makes the player hostile-mob
//                                  targetable (survival alone no longer does)
//   time <ticks>                   set world time (and broadcast it)
//   dump [tag]                     force a state dump right now
//   quit                           clean exit through the normal path
//
// Events on the dump stream: welcome, ready (connected + WELCOME + the
// camera's chunk is CHUNK_LIVE + at least one CHUNK_ROLE grant applied),
// cmd_error, explosion_received, pickup_result, player_hit_received,
// entity_transfer, probe, role, cprobe, signprobe, damage_result,
// eat_result, respawn_result, chat_received {kind,from,target,text}, quit.
//
// State lines additionally carry: health (half-hearts), dead, air_ticks,
// name (the requested display name), players (the PLAYER_NAME table —
// the server's effective, deduped spellings).
//
// The world-action implementations (setblock/drop/mob/hit/fall/tnt/mode/
// time/goto/probe + name lookups) live in common/harness/debug_commands
// (dbgcmd::) so the REPGAME_SCRIPT runner shares them; mp_test keeps its
// own line parsing, event stream and dump cadence.

struct RepGameState;
struct NetChatPayload;

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
// The chat_received event for one inbound CHAT frame; the strings are
// JSON-escaped inside (don't pre-escape).
void chat_received( RepGameState *state, const NetChatPayload &p );
// Force one state line (also used by the `dump` command).
void dump_state( RepGameState &state, const char *tag = nullptr );
} // namespace mp_test
