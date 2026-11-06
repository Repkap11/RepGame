#pragma once
#include <string>
#include <glm/glm.hpp>

#include "common/block.hpp" // BlockID, ItemID
#include "common/mobs.hpp" // MobType
#include "common/chunk_loader.hpp"

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
bool fall( RepGameState &state, const char *item, const glm::ivec3 &pos, std::string *err );
bool tnt( RepGameState &state, const glm::vec3 &center, std::string *err );
bool setblock( RepGameState &state, const glm::ivec3 &pos, const char *block_name, std::string *err );
// Inclusive box; refuses volumes > 65536 cells.
bool fill( RepGameState &state, const glm::ivec3 &a, const glm::ivec3 &b, const char *block_name, std::string *err );
// `mode survival|creative`; force_survival (when non-null) also reasserts
// the mode each tick (mp_test semantics).
bool mode( RepGameState &state, const char *name, bool *force_survival, std::string *err );
void set_time( RepGameState &state, long ticks );
} // namespace dbgcmd
