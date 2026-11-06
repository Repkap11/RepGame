#pragma once

// Declarations of every rep_tests entry point (one per file under
// src/common/tests/). The runner table in rep_tests.cpp picks which of
// these run, in order. Test bodies must keep the exact `int test_*()` global
// signature — several classes friend them for private access.

int test_ecs( );
int test_vr_projection( );
int test_min_ambient_step( );
int test_redstone( );
int test_torch_pick( );
int test_player_placement_valid( );
int test_sneak( );
int test_items( );
int test_item_entities( );
int test_save_migration( );
int test_player_data( );
int test_creative( );
int test_fences( );
int test_doors( );
int test_block_world( );
int test_mapgen( );
int test_waterlog( );
int test_lighting( );
int test_tnt( );
int test_falling_blocks( );
int test_structure_border( );
int test_mobs( );
int test_net_entities( );
int test_net_item_crossing( );
int test_net_item_mirror_visuals( );
int test_net_mobs( );
int test_net_bodies( );
int test_crafting( );
int test_results( );
int test_script( );
