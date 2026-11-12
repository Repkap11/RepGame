#pragma once

// Declarations of every rep_tests entry point (one per file under
// src/common/tests/). The runner table in rep_tests.cpp picks which of
// these run, in order. Test bodies must keep the exact `int test_*()` global
// signature — several classes friend them for private access.

int test_ecs( );
int test_vr_projection( );
int test_min_ambient_step( );
int test_angle_wrap( );
int test_teleport_parse( );
int test_redstone( );
int test_torch_pick( );
int test_ray_face_neighbor( );
int test_player_placement_valid( );
int test_sneak( );
int test_climb( );
int test_vitals( );
int test_held_swing( );
int test_items( );
int test_furnace( );
int test_item_entities( );
int test_save_migration( );
int test_player_data( );
int test_creative( );
int test_ui_layout( );
int test_fences( );
int test_doors( );
int test_block_world( );
int test_mapgen( );
int test_waterlog( );
int test_fluids( );
int test_lighting( );
int test_tnt( );
int test_falling_blocks( );
int test_leaf_decay( );
int test_support_drops( );
int test_sapling_grow( );
int test_leaf_placement( );
int test_shears_bonemeal( );
int test_farming( );
int test_stairs( );
int test_slabs( );
int test_cake( );
int test_grass( );
int test_grass_persist( );
int test_structure_border( );
int test_particles( );
int test_mobs( );
int test_net_codec_registry( );
int test_net_containers( );
int test_net_entities( );
int test_net_item_crossing( );
int test_net_item_grant( );
int test_net_item_mirror_visuals( );
int test_net_mobs( );
int test_net_bodies( );
int test_arrows( );
int test_net_multiplayer_mode( );
int test_signs( );
int test_crafting( );
int test_results( );
int test_script( );
int test_inspect( );
int test_profile( );
int test_chunk_edits( );
int test_chunk_sort( );
int test_chunk_occlusion( );
int test_light_stale( );
