#pragma once

#include "common/BlockUpdateQueue.hpp"

class World;

// How far from a supporting log a leaf can sit, in face-adjacent steps
// through leaf cells of any species or placement kind.
#define LEAF_DECAY_RANGE 6
// A decay check lands 0.5-2.5 s after the neighbor change that queued it, so
// a falling canopy unravels gradually instead of popping all at once.
#define LEAF_DECAY_MIN_TICKS ( UPS_RATE / 2 )
#define LEAF_DECAY_MAX_TICKS ( UPS_RATE * 5 / 2 )

// BFS over face-adjacent leaf cells (any species, natural or persistent)
// starting at `pos`: true as soon as a visited cell touches an is_log face
// neighbor, or the search reaches an unloaded cell (LAST_BLOCK_ID counts as
// supported — the canopy may continue into an unloaded chunk). Bounded to
// LEAF_DECAY_RANGE steps from `pos`. Declared here so tests can call it.
bool leaf_has_log_support( World &world, const glm::ivec3 &pos );

// Queued (after a jittered delay) when a natural leaf's neighbor changed and
// the leaf may have lost log support — see BlockNextToChangeEvent. On firing
// it erases the leaf's pending_leaf_decay slot, re-checks the cell, runs
// leaf_has_log_support and queues an AIR write with BreakDrops::Always when
// unsupported. That write's own neighbor updates queue the next leaves'
// checks, so a severed canopy decays over a few seconds rather than
// recursively in one tick. Persistent (player-placed) leaves never decay.
class LeafDecayEvent : public BlockUpdateEvent {
  public:
    LeafDecayEvent( long tick_number, const glm::ivec3 &pos );
    void performAction( RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
};
