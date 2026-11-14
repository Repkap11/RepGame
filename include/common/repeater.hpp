#pragma once

// Repeater (REPEATER) state encoding and input/output logic. Pure
// functions over World::get_loaded_block so the unit tests can drive them
// — the same style as common/rail.hpp. Used by RepeaterEvent (the
// scheduled delay tick), the power reads in BlockNextToChangeEvent, the
// right-click delay cycle and the mesher (movable torch position).
//
// Encoding: the facing is rotation & 3 in the shared yaw order —
// 0=-z, 1=+x, 2=+z, 3=-x (getPlacedRotation's buckets) — and output goes
// toward it, so the repeater points away from the player as in MC. Input
// is read only from the opposite cell. The right-click delay (1-4
// redstone ticks) lives in display_id because rotation has no free bits:
// display_id == REPEATER is delay 1 and the hidden render-only
// REPEATER_DELAY_2/3/4 blocks cover the rest — the same display_id trick
// dust shapes and RAIL_CORNER use. current_redstone_power is the output:
// 0 or REDSTONE_SOURCE_POWER, owned exclusively by RepeaterEvent.

#include <glm/glm.hpp>

#include "common/block_types.hpp"

class World;

// The unit vector the repeater at rotation `rot` outputs toward.
glm::ivec3 repeater_output_dir( unsigned char rot );

// The right-click delay in redstone ticks (1-4), decoded from display_id.
int repeater_delay( const BlockState &state );
// The same cell state with the delay swapped in (display_id only);
// out-of-range delays clamp to 1-4.
BlockState repeater_with_delay( BlockState state, int delay );
// The right-click cycle: delay 1->2->3->4->1.
BlockState repeater_cycle_delay( BlockState state );
// The delay in game ticks: delay * REDSTONE_TICK_TICKS.
long repeater_delay_ticks( const BlockState &state );

// Whether `rep` is a powered repeater whose output cell is `target` —
// the only direction a repeater feeds.
bool repeater_points_into( const BlockState &rep, const glm::ivec3 &rep_pos, const glm::ivec3 &target );

// Whether the cell behind the repeater (pos - output dir) drives it:
// powered dust pointing in, any powered solid block (weak or hard), an
// initial_redstone_power block, a lit redstone torch, a pressed
// button/plate, or a powered repeater pointing into it.
bool repeater_input_powered( World &world, const glm::ivec3 &pos, const BlockState &state );
