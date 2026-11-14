#include "common/tree_grow.hpp"

#include <vector>

#include "common/RepGame.hpp"
#include "common/game/game_state.hpp"
#include "common/multiplayer.hpp"
#include "common/block.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

bool tree_spec_for_sapling( const BlockID sapling, const glm::ivec3 &pos, TreeSpec *out ) {
    // Same roll layout as the generator's sg_tree_for_cell: species is fixed
    // by the sapling, so seeds 4 (shape/radius), 5 (height) and 6 (lean)
    // carry the per-position variation.
    const float sr = sg_roll( pos.x, pos.z, 4 );
    const float hr = sg_roll( pos.x, pos.z, 5 );
    const float lr = sg_roll( pos.x, pos.z, 6 );

    TreeSpec spec;
    spec.lean_x = 0;
    spec.lean_z = 0;
    spec.giant = 0;
    spec.hanger = AIR;
    spec.hanger_len = 0;
    spec.hanger_mask = 0;

    switch ( sapling ) {
        case OAK_SAPLING:
        case SAPLING:
            spec.log = sapling == SAPLING ? LOG : OAK_LOG;
            spec.leaf = sapling == SAPLING ? LEAVES : OAK_LEAVES;
            spec.shape = SHAPE_BLOB;
            spec.trunk_h = 4 + (int)( hr * 3.0f );
            spec.canopy_r = 3;
            break;
        case FAT_OAK_SAPLING:
            spec.log = OAK_LOG;
            spec.leaf = OAK_LEAVES;
            spec.shape = SHAPE_BLOB;
            spec.trunk_h = 6 + (int)( hr * 3.0f );
            spec.canopy_r = 4;
            break;
        case BIRCH_SAPLING:
            spec.log = BIRCH_LOG;
            spec.leaf = BIRCH_LEAVES;
            spec.shape = SHAPE_BLOB;
            spec.trunk_h = 5 + (int)( hr * 3.0f );
            spec.canopy_r = 3;
            break;
        case SPRUCE_SAPLING:
            spec.log = SPRUCE_LOG;
            spec.leaf = SPRUCE_LEAVES;
            spec.shape = SHAPE_PINE;
            spec.trunk_h = 7 + (int)( hr * 4.0f );
            spec.canopy_r = 2;
            break;
        case JUNGLE_SAPLING:
            // Like the generator's non-giant jungle trees, vines included.
            spec.log = JUNGLE_LOG;
            spec.leaf = JUNGLE_LEAVES;
            spec.shape = SHAPE_JUNGLE;
            spec.trunk_h = 9 + (int)( hr * 6.0f );
            spec.canopy_r = 3 + ( sr > 0.6f ? 1 : 0 );
            spec.hanger = VINE;
            spec.hanger_len = 4;
            spec.hanger_mask = 3u;
            break;
        case ACACIA_SAPLING:
            spec.log = ACACIA_LOG;
            spec.leaf = ACACIA_LEAVES;
            spec.shape = SHAPE_ACACIA;
            spec.trunk_h = 4 + (int)( hr * 3.0f );
            spec.canopy_r = 2;
            spec.lean_x = lr < 0.33f ? 1 : lr < 0.66f ? -1 : 0;
            spec.lean_z = spec.lean_x == 0 ? ( lr < 0.5f ? 1 : -1 ) : 0;
            break;
        case DARK_OAK_SAPLING:
            spec.log = DARK_OAK_LOG;
            spec.leaf = DARK_OAK_LEAVES;
            spec.shape = SHAPE_BLOB;
            spec.trunk_h = 6 + (int)( hr * 3.0f );
            spec.canopy_r = 4;
            break;
        case CHERRY_SAPLING:
            spec.log = CHERRY_LOG;
            spec.leaf = CHERRY_LEAVES;
            spec.shape = SHAPE_BLOB;
            spec.trunk_h = 5 + (int)( hr * 3.0f );
            spec.canopy_r = 3;
            break;
        case PALE_OAK_SAPLING:
            spec.log = PALE_OAK_LOG;
            spec.leaf = PALE_OAK_LEAVES;
            spec.shape = SHAPE_BLOB;
            spec.trunk_h = 7 + (int)( hr * 4.0f );
            spec.canopy_r = 4;
            spec.hanger = PALE_HANGING_MOSS;
            spec.hanger_len = 3;
            spec.hanger_mask = 7u;
            break;
        case POPLAR_SAPLING:
            spec.log = POPLAR_LOG;
            spec.leaf = lr < 0.33f ? YELLOW_POPLAR_LEAVES : lr < 0.66f ? ORANGE_POPLAR_LEAVES : RED_POPLAR_LEAVES;
            spec.shape = SHAPE_BLOB;
            spec.trunk_h = 7 + (int)( hr * 3.0f );
            spec.canopy_r = 2;
            break;
        default:
            // Bamboo, dead, garbage and potted saplings, the mangrove
            // propagule: none of these grow into trees.
            return false;
    }
    *out = spec;
    return true;
}

// World-space Target for the shared tree builders: get/set go through the
// world (writes are synced to multiplayer), and the cells that receive a
// trunk log are recorded so the caller can queue neighbor updates only for
// the parts that can displace other blocks (a trunk can push through a tall
// plant's upper half; leaves only ever fill AIR).
class WorldTreeTarget {
  public:
    WorldTreeTarget( RepGameState &state, const bool preserve_player_edited ) : state( state ), preserve_player_edited( preserve_player_edited ) {
    }

    BlockState get_block( const glm::ivec3 &pos ) const {
        return state.world.get_loaded_block( pos );
    }

    void set_block( const glm::ivec3 &pos, const BlockState &blockState ) {
        if ( this->preserve_player_edited ) {
            // Natural growth isn't a player edit: keep the pen rule out.
            state.world.set_loaded_block_natural( pos, blockState );
        } else {
            state.world.set_loaded_block( pos, blockState );
        }
        state.multiplayer.set_block( pos, blockState, NET_BU_FLAG_NO_DEBRIS );
        if ( block_definition_get_definition( blockState.id )->is_log ) {
            trunk_cells.push_back( pos );
        }
    }

    RepGameState &state;
    bool preserve_player_edited;
    std::vector<glm::ivec3> trunk_cells;
};

bool tree_grow_try( RepGameState &state, const glm::ivec3 &pos, const bool preserve_player_edited ) {
    const BlockState sapling_state = state.world.get_loaded_block( pos );
    if ( sapling_state.id == LAST_BLOCK_ID ) {
        return false;
    }
    TreeSpec spec;
    if ( !tree_spec_for_sapling( sapling_state.id, pos, &spec ) ) {
        return false;
    }
    // Soil under the sapling.
    switch ( state.world.get_loaded_block( pos + glm::ivec3( 0, -1, 0 ) ).id ) {
        case GRASS:
        case DIRT:
        case PODZOL:
        case SNOWY_GRASS:
        case MYCELIUM:
        case PALE_MOSS_BLOCK:
        case COARSE_DIRT:
        case MOSS_BLOCK:
        case MUD:
            break;
        default:
            return false;
    }
    // Every trunk cell (the acacia lean included) must be loaded and either
    // open air, a non-colliding block the trunk can replace, or a leaf —
    // mirroring what sg_write_trunk will actually overwrite.
    for ( int i = 0; i < spec.trunk_h; i++ ) {
        const glm::ivec3 cell = pos + glm::ivec3( spec.lean_x != 0 && i >= spec.trunk_h - 2 ? spec.lean_x : 0, i,
                                                  spec.lean_z != 0 && i >= spec.trunk_h - 2 ? spec.lean_z : 0 );
        const BlockState cur = state.world.get_loaded_block( cell );
        if ( cur.id == LAST_BLOCK_ID ) {
            return false;
        }
        if ( cur.id != AIR ) {
            const Block *cur_block = block_definition_get_definition( cur.id );
            if ( cur_block->collides_with_player && !block_is_leaf( cur.id ) ) {
                return false;
            }
        }
    }
    WorldTreeTarget target( state, preserve_player_edited );
    // co = (0,0,0): pos is already world coords, so the hash inputs
    // co + local are the world cells, exactly like mapgen.
    sg_place_tree( target, spec, pos.x, pos.y, pos.z, glm::ivec3( 0, 0, 0 ) );
    queue_neighbor_block_updates( state, pos + glm::ivec3( 0, -1, 0 ) );
    for ( const glm::ivec3 &cell : target.trunk_cells ) {
        queue_neighbor_block_updates( state, cell );
    }
    return true;
}

// A shoot converts on the same soils trees use, plus the sands MC also
// accepts, plus the bamboo family itself — worldgen writes stacked shoots
// and they convert bottom-up.
static bool bamboo_soil( const BlockID id ) {
    switch ( id ) {
        case GRASS:
        case DIRT:
        case PODZOL:
        case SNOWY_GRASS:
        case MYCELIUM:
        case PALE_MOSS_BLOCK:
        case COARSE_DIRT:
        case MOSS_BLOCK:
        case MUD:
        case SAND:
        case RED_SAND:
        case BAMBOO:
        case BAMBOO_SAPLING:
            return true;
        default:
            return false;
    }
}

static void bamboo_write( RepGameState &state, const glm::ivec3 &pos, const BlockState &blockState, const bool preserve_player_edited ) {
    if ( preserve_player_edited ) {
        // Random-tick growth isn't a player edit: keep the pen rule out.
        state.world.set_loaded_block_natural( pos, blockState );
    } else {
        state.world.set_loaded_block( pos, blockState );
    }
    state.multiplayer.set_block( pos, blockState, NET_BU_FLAG_NO_DEBRIS );
    queue_neighbor_block_updates( state, pos );
}

// Leaf tiers are positional from the column top (MC's leaves property):
// top large (2), second small (1), deeper none (0). Writes only on a
// mismatch so a settled column costs no writes.
static void bamboo_set_tier( RepGameState &state, const glm::ivec3 &pos, const int tier, const bool preserve_player_edited ) {
    const BlockState cur = state.world.get_loaded_block( pos );
    if ( cur.id != BAMBOO || cur.rotation == tier ) {
        return;
    }
    BlockState next = cur;
    next.rotation = static_cast<unsigned char>( tier );
    bamboo_write( state, pos, next, preserve_player_edited );
}

bool bamboo_grow_try( RepGameState &state, const glm::ivec3 &pos, const int segments, const bool preserve_player_edited ) {
    const BlockState cur = state.world.get_loaded_block( pos );
    if ( cur.id == BAMBOO_SAPLING ) {
        if ( !bamboo_soil( state.world.get_loaded_block( pos + glm::ivec3( 0, -1, 0 ) ).id ) ) {
            return false;
        }
        const BlockState shoot = { BAMBOO, 0, 0, BAMBOO, 0 };
        bamboo_write( state, pos, shoot, preserve_player_edited );
    } else if ( cur.id != BAMBOO ) {
        return false;
    }
    // Column bounds — growth only ever happens at the top.
    glm::ivec3 base = pos;
    while ( state.world.get_loaded_block( base + glm::ivec3( 0, -1, 0 ) ).id == BAMBOO ) {
        base += glm::ivec3( 0, -1, 0 );
    }
    glm::ivec3 top = pos;
    while ( state.world.get_loaded_block( top + glm::ivec3( 0, 1, 0 ) ).id == BAMBOO ) {
        top += glm::ivec3( 0, 1, 0 );
    }
    // MC caps a natural column at 11-16 segments, hashed per column base.
    const int max_h = 11 + static_cast<int>( sg_roll( base.x, base.z, 7 ) * 5.9999f );
    int grew = 0;
    for ( int i = 0; i < segments; i++ ) {
        const glm::ivec3 above = top + glm::ivec3( 0, 1, 0 );
        if ( top.y - base.y + 1 >= max_h || state.world.get_loaded_block( above ).id != AIR ) {
            break;
        }
        const BlockState next = { BAMBOO, 2, 0, BAMBOO, 0 };
        bamboo_write( state, above, next, preserve_player_edited );
        top = above;
        grew++;
    }
    if ( grew > 0 ) {
        // Refresh the leaf tiers around the new top.
        bamboo_set_tier( state, top + glm::ivec3( 0, -1, 0 ), 1, preserve_player_edited );
        bamboo_set_tier( state, top + glm::ivec3( 0, -2, 0 ), 0, preserve_player_edited );
    }
    return grew > 0 || cur.id == BAMBOO_SAPLING;
}
