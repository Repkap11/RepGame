#pragma once
#include <string>
#include <glm/glm.hpp>

#include "common/block.hpp" // BlockID, ItemID
#include "common/mobs.hpp" // MobType
#include "common/chunk_loader.hpp"
#include "common/container.hpp" // NetContainerKindDef, ContainerContents

struct RepGameState;

// World-action commands shared between the mp-test ctl parser
// (src/common/mp_test.cpp) and the script runner
// (src/common/harness/script.cpp, REPGAME_SCRIPT). Both call into
// `dbgcmd::*`; mp_test keeps its own line parsing/event stream and the
// script runner reports through results::event/assert_ instead. Each
// mutating function reports failures via `err` and returns false so the
// caller picks the error channel.
namespace dbgcmd {
// Block/item name lookup (every defined block name plus pure item names,
// case-insensitive; LAST_BLOCK_ID on miss). block_from_name additionally
// accepts a numeric id.
ItemID item_from_name( const char *name );
BlockID block_from_name( const char *name );
MobType mob_from_name( const char *name );
// True when the terrain loader and light queues have no work outstanding —
// the shared settle predicate behind wait_idle, fly settle and capture
// settle.
bool loader_idle( const ChunkLoader &loader );

void goto_( RepGameState &state, const glm::dvec3 &pos );
bool drop( RepGameState &state, const char *item, int count, const glm::dvec3 &pos, const glm::dvec3 &vel, std::string *err );
bool mob( RepGameState &state, const char *type, const glm::dvec3 &pos, std::string *err );
bool hit( RepGameState &state, unsigned long long net_id, std::string *err );
// Right-click feed on the mob nearest `pos` (empty `item` picks the type's
// first breed food); mirrors route through ENTITY_ACTION/NET_ACTION_FEED.
bool feed( RepGameState &state, const glm::dvec3 &pos, const char *item, std::string *err );
bool fall( RepGameState &state, const char *item, const glm::ivec3 &pos, std::string *err );
bool tnt( RepGameState &state, const glm::vec3 &center, std::string *err );
bool setblock( RepGameState &state, const glm::ivec3 &pos, const char *block_name, std::string *err );
// Inclusive box; refuses volumes > 65536 cells.
bool fill( RepGameState &state, const glm::ivec3 &a, const glm::ivec3 &b, const char *block_name, std::string *err );
// Container kind lookup by name ("chest"/"furnace"), nullptr on miss.
const NetContainerKindDef *container_from_name( const char *name );
// CONTAINER_* drivers: copen/cclose subscribe on the server; cset writes
// the slot optimistically into World then sends SET_SLOT (same shape as the
// UI drag paths — the server either accepts and relays it to other viewers
// or pushes a correcting CONTENTS snapshot back).
bool copen( RepGameState &state, const NetContainerKindDef *def, const glm::ivec3 &pos, std::string *err );
bool cclose( RepGameState &state, const NetContainerKindDef *def, const glm::ivec3 &pos, std::string *err );
bool cset( RepGameState &state, const NetContainerKindDef *def, const glm::ivec3 &pos, int slot, const char *item, int count,
           std::string *err );
// Fills `out` with the world-owned contents at `pos` (exact-arity empties
// when nothing is stored); *is_container reports the live block. False only
// when the chunk the container would live in isn't loaded.
bool cprobe( RepGameState &state, const NetContainerKindDef *def, const glm::ivec3 &pos, ContainerContents *out, bool *is_container,
             std::string *err );
// `mode survival|creative`; force_survival (when non-null) also reasserts
// the mode each tick (mp_test semantics).
bool mode( RepGameState &state, const char *name, bool *force_survival, std::string *err );
// `peaceful on|off` — off makes the player targetable by hostile mobs.
bool peaceful( RepGameState &state, const char *name, std::string *err );
void set_time( RepGameState &state, long ticks );
} // namespace dbgcmd
