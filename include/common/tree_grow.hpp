#pragma once

// Sapling growth: rolls a TreeSpec from the sapling's world position and
// writes it through the shared tree builders (tree_shapes.hpp), so grown
// trees use the exact code the terrain generator does and the shape is a
// pure function of position — two clients growing the same sapling write
// identical cells.

#include <glm/glm.hpp>

#include "common/tree_shapes.hpp"

struct RepGameState;

// Fills `out` with the tree `sapling` at world position `pos` grows into.
// Trunk height, canopy radius, lean and poplar leaf colour all come from
// sg_roll/sg_hash on `pos`. Returns false for things that never grow:
// bamboo, dead/garbage/potted saplings and the mangrove propagule.
bool tree_spec_for_sapling( BlockID sapling, const glm::ivec3 &pos, TreeSpec *out );

// Grows the sapling at `pos` if the ground below is soil and every trunk
// cell is loaded and clear. Writes the tree through WorldTreeTarget (world
// writes + multiplayer sync), records the trunk cells, then queues neighbor
// updates for the trunk and ground cells. Returns true when the tree was
// grown. Sapling-free cells and unsupported grounds return false without
// touching the world. preserve_player_edited restores each written chunk's
// player_edited flag — random-tick growth isn't a player edit; bone-meal
// growth is, so callers there leave it off.
bool tree_grow_try( RepGameState &state, const glm::ivec3 &pos, bool preserve_player_edited = false );
