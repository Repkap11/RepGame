#pragma once

// Shared survival drop logic for every "block removed without being mined by
// the player" path — support-loss kills (BlockNextToChangeEvent) and leaf
// decay (LeafDecayEvent) — plus the player's own destroy path, so all of
// them roll the same drops.

#include <glm/glm.hpp>

#include "common/block_definitions.hpp"
#include "common/item.hpp"

struct RepGameState;

// Rolls the broken block's drop table (or its self-drop) and spawns each
// stack as an item entity at `pos` with a small random hop. Callers gate on
// game mode — support-loss breaks drop in survival and nothing in creative.
//
// Paired halves (doors, tall plants, beds) drop only while the partner cell
// still holds the partner: whichever kill lands first produces the one
// item — block_item_id maps either half to the lower — and the second sees
// an empty partner and drops nothing. The bed pair is horizontal, so the
// full broken_state comes in for the yaw. force_item skips the roll and
// drops that single item (shears harvesting leaves); LAST_BLOCK_ID means
// "roll".
void break_drops( RepGameState &state, const glm::ivec3 &pos, const BlockState &broken_state, ItemID force_item = LAST_BLOCK_ID );
