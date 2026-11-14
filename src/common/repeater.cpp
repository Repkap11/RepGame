#include "common/repeater.hpp"
#include "common/world.hpp"
#include "common/constants.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"

// The four facings in the shared yaw order — 0=-z, 1=+x, 2=+z, 3=-x.
static const glm::ivec3 REPEATER_OUT_DIRS[ 4 ] = {
    glm::ivec3( 0, 0, -1 ), //
    glm::ivec3( 1, 0, 0 ),  //
    glm::ivec3( 0, 0, 1 ),  //
    glm::ivec3( -1, 0, 0 ), //
};

glm::ivec3 repeater_output_dir( unsigned char rot ) {
    return REPEATER_OUT_DIRS[ rot & 3 ];
}

// display_id -> delay table (the delay variant ids aren't contiguous).
static const BlockID REPEATER_DELAY_IDS[ 4 ] = { REPEATER, REPEATER_DELAY_2, REPEATER_DELAY_3, REPEATER_DELAY_4 };

int repeater_delay( const BlockState &state ) {
    for ( int i = 1; i < 4; i++ ) {
        if ( state.display_id == REPEATER_DELAY_IDS[ i ] ) {
            return i + 1;
        }
    }
    return 1;
}

BlockState repeater_with_delay( BlockState state, int delay ) {
    if ( delay < 1 ) {
        delay = 1;
    } else if ( delay > 4 ) {
        delay = 4;
    }
    state.display_id = REPEATER_DELAY_IDS[ delay - 1 ];
    return state;
}

BlockState repeater_cycle_delay( BlockState state ) {
    return repeater_with_delay( state, repeater_delay( state ) % 4 + 1 );
}

long repeater_delay_gt( const BlockState &state ) {
    return repeater_delay( state ) * REDSTONE_REPEATER_STEP_GT;
}

bool repeater_points_into( const BlockState &rep, const glm::ivec3 &rep_pos, const glm::ivec3 &target ) {
    return block_definition_get_definition( rep.id )->is_repeater && rep.current_redstone_power > 0 &&
           rep_pos + repeater_output_dir( rep.rotation ) == target;
}

bool repeater_input_powered( World &world, const glm::ivec3 &pos, const BlockState &state ) {
    const glm::ivec3 out_dir = repeater_output_dir( state.rotation );
    const glm::ivec3 back_pos = pos - out_dir;
    const BlockState back = world.get_loaded_block( back_pos );
    if ( back.id == LAST_BLOCK_ID ) {
        return false;
    }
    const Block *back_def = block_definition_get_definition( back.id );
    if ( back_def->is_redstone_dust ) {
        return back.current_redstone_power > 0 && dust_points_toward( back.display_id, out_dir );
    }
    if ( back_def->is_repeater ) {
        return repeater_points_into( back, back_pos, pos );
    }
    if ( back_def->initial_redstone_power > 0 ) {
        return true;
    }
    if ( back.id == REDSTONE_TORCH && back.current_redstone_power > 0 ) {
        return true;
    }
    if ( ( back_def->is_button || back_def->is_pressure_plate ) && back.current_redstone_power > 0 ) {
        return true;
    }
    if ( back_def->is_solid && back.current_redstone_power > 0 ) {
        return true;
    }
    return false;
}
