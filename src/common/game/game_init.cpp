// RepGame member functions: world/game init, shutdown, and player save.

#include <cmath>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/map_storage.hpp"
#include "common/renderer/texture.hpp"
#include "common/world.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/harness/results.hpp"
#include "common/utils/env_config.hpp"
#include "common/chat.hpp"
#include "common/light.hpp"
#include "common/map_gen.hpp"
#include "common/game/game_internal.hpp"
#include "common/block_update_events/PistonEvent.hpp"

// Whether either cell a standing player would occupy at (x, y, z) is solid
// terrain — used to keep the spawn point out of overhang stone.

static inline bool spawn_cell_blocked( const int x, const int y, const int z ) {
    const BlockID feet = MapGen::gen_block_id( x, y, z );
    const BlockID head = MapGen::gen_block_id( x, y + 1, z );
    const Block *feet_def = block_definition_get_definition( feet );
    const Block *head_def = block_definition_get_definition( head );
    return feet_def->collides_with_player || head_def->collides_with_player;
}

void RepGame::initializeGameState( const char *world_name ) {
    globalGameState.input.exitGame = false;
    globalGameState.input.player_flying = false;
    globalGameState.input.inventory_open = false;
    globalGameState.input.menu_open = false;
    globalGameState.ui.chest.open = false;
    globalGameState.ui.furnace.open = false;
    globalGameState.pending_container_loot.clear( );
    globalGameState.input.no_clip = false;
    globalGameState.input.mouse.smoothed_dx = 0.0f;
    globalGameState.input.mouse.smoothed_dy = 0.0f;
    globalGameState.input.shift_held = false;
    globalGameState.input.screen_tap_pending = false;
    // Start a fresh world at noon (fully inside the daylight plateau); a
    // save may overwrite this below.
    globalGameState.world_time = DAY_LENGTH_TICKS / 4;
    globalGameState.block_mining.pos = glm::ivec3( 0 );
    globalGameState.block_mining.id = AIR;
    globalGameState.block_mining.progress_ticks = 0.0f;
    globalGameState.watched_pressure_plates.clear( );
    globalGameState.pending_repeater_ticks.clear( );
    globalGameState.camera.angle_H = 0.0f;
    globalGameState.camera.angle_V = 0.0f;
    globalGameState.vr.active = false;
    globalGameState.vr.body_yaw = 0.0f;
    globalGameState.vr.head = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
    globalGameState.vr.head_pos = glm::vec3( 0.0f );
    globalGameState.vr.ui_world_locked = false;
    globalGameState.vr.ui_orientation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
    globalGameState.vr.ui_position = glm::vec3( 0.0f );
    globalGameState.vr.prev_inventory_open = false;
    globalGameState.vr.pointer_on_ui = false;
    globalGameState.vr.saved_quality = WorldDrawQuality::MEDIUM;
    globalGameState.vr.input = VrControllerInput{};
    globalGameState.vr.prev_input = VrControllerInput{};
    globalGameState.vr.move_active = false;
    globalGameState.vr.turn_armed = true;
    globalGameState.vr.haptic_pending = false;
    globalGameState.camera.pos.x = 0.5f;
    // Spawn on the topmost solid block of the spawn column: overhangs and
    // floating islands can put real terrain far above the heightmap, so
    // scan down from the terrain bound for the highest colliding cell and
    // stand on it. (Terrain only — structures may still decorate the cell.)
    double spawn_y;
    {
        int top = (int)ceil( MapGen::maxTerrainHeight( ) );
        while ( top > BEDROCK_LEVEL &&
                !block_definition_get_definition( MapGen::gen_block_id( 0, top, 0 ) )->collides_with_player ) {
            top--;
        }
        spawn_y = top + 1.0;
        while ( spawn_cell_blocked( 0, (int)spawn_y, 0 ) ) {
            spawn_y += 1.0;
        }
    }
    globalGameState.camera.pos.y = spawn_y + PLAYER_EYE_HEIGHT + 0.5f;
    globalGameState.camera.pos.z = 0.5f;
    globalGameState.camera.y_speed = 0.0f;
    globalGameState.camera.horizontal_vel = glm::dvec2( 0.0, 0.0 );
    globalGameState.camera.sneaking = 0;
    globalGameState.camera.player_height = PLAYER_HEIGHT;
    globalGameState.camera.eye_height = PLAYER_EYE_HEIGHT;
    globalGameState.camera.eye_height_render = PLAYER_EYE_HEIGHT;
    globalGameState.input.worldDrawQuality = WorldDrawQuality::LOW;
    // Starting horizontal render distance (chunks); the torus is allocated
    // for RENDER_RADIUS_MAX regardless, so this only picks the initial
    // active window. Live-adjusted with =/-.
    globalGameState.input.render_distance = RENDER_RADIUS_DEFAULT;
    if ( env_is_set( "REPGAME_RENDER_DISTANCE" ) ) {
        int r = env_int( "REPGAME_RENDER_DISTANCE", RENDER_RADIUS_DEFAULT );
        if ( r < 1 ) {
            r = 1;
        } else if ( r > RENDER_RADIUS_MAX ) {
            r = RENDER_RADIUS_MAX;
        }
        globalGameState.input.render_distance = r;
    }
    // The hotbar's rendered height as a fraction of the UI height basis.
    // The creative palette reserves the same amount at the bottom of the
    // screen (ui_layout_input reads bottom_reserved_percent for its floor).
    constexpr float hotbar_height_percent = 0.1f;
    globalGameState.ui.creative.inventory_renderer.options.active_height_percent = 0.75f;
    globalGameState.ui.creative.inventory_renderer.options.max_height_percent = 0.75f;
    globalGameState.ui.creative.inventory_renderer.options.max_width_percent = 0.75f;
    globalGameState.ui.creative.inventory_renderer.options.bottom_reserved_percent = hotbar_height_percent;
    globalGameState.ui.creative.inventory_renderer.options.shows_selection_slot = false;
    globalGameState.ui.creative.inventory_renderer.options.fixed_cell_stride = 0.0f;

    globalGameState.hotbar.inventory_renderer.options.active_height_percent = 1.0f;
    globalGameState.hotbar.inventory_renderer.options.max_height_percent = hotbar_height_percent;
    globalGameState.hotbar.inventory_renderer.options.max_width_percent = 0.75f;
    globalGameState.hotbar.inventory_renderer.options.bottom_reserved_percent = 0.0f;
    globalGameState.hotbar.inventory_renderer.options.shows_selection_slot = true;
    globalGameState.hotbar.inventory_renderer.options.fixed_cell_stride = 0.0f;

    // Container panels (chest, crafting grids/output, the furnace trio)
    // are fixed-stride: repgame_layout_containers sets the stride from the
    // survival grid and the layout pass positions them, so only the
    // selection-slot flag matters here.
    globalGameState.ui.chest.inventory_renderer.options.shows_selection_slot = false;
    globalGameState.ui.chest.inventory_renderer.options.fixed_cell_stride = 0.0f;

    for ( InventoryRenderer *r : { &globalGameState.ui.crafting.grid2, &globalGameState.ui.crafting.grid3, &globalGameState.ui.crafting.output,
                                   &globalGameState.ui.furnace.input, &globalGameState.ui.furnace.fuel, &globalGameState.ui.furnace.output } ) {
        r->options.shows_selection_slot = false;
        r->options.fixed_cell_stride = 0.0f;
    }
    globalGameState.ui.crafting.table_open = false;
    globalGameState.ui.crafting.drag.active = false;
    globalGameState.ui.crafting.recipe = -1;
    globalGameState.ui.crafting.output_slot.item_id = LAST_BLOCK_ID;
    globalGameState.ui.crafting.output_slot.quantity = 0;
    for ( int i = 0; i < 9; i++ ) {
        globalGameState.ui.crafting.slots[ i ].item_id = LAST_BLOCK_ID;
        globalGameState.ui.crafting.slots[ i ].quantity = 0;
    }

    globalGameState.map_storage.init( world_name );
    PlayerData saved_data;
    globalGameState.spawn_pending = !globalGameState.map_storage.read_player_data( saved_data );
    if ( repgame_block_world( ) ) {
        // Block world: spawn on the sandstone floor at the grid start,
        // looking into +x/+z, creative, walking (not flying).
        globalGameState.spawn_pending = true;
        globalGameState.camera.pos = glm::dvec3( 0.5, 2.0, 0.5 );
        globalGameState.camera.angle_H = 135.0f;
        globalGameState.camera.angle_V = 15.0f;
        globalGameState.input.player_flying = false;
        globalGameState.input.no_clip = false;
        globalGameState.game_mode = GameMode_Creative;
    }
    if ( repgame_flat_world( ) && globalGameState.spawn_pending ) {
        // Flat world with no player.dat: creative + flying so the visitor
        // can immediately orbit the voxelized model. A save still wins.
        globalGameState.input.player_flying = true;
        globalGameState.game_mode = GameMode_Creative;
    }
    if ( !globalGameState.spawn_pending ) {
        globalGameState.camera.pos.x = saved_data.world_x;
        globalGameState.camera.pos.y = saved_data.world_y;
        globalGameState.camera.pos.z = saved_data.world_z;
        globalGameState.camera.angle_H = saved_data.angle_H;
        globalGameState.camera.angle_V = saved_data.angle_V;
        globalGameState.input.player_flying = saved_data.flying;
        globalGameState.input.no_clip = saved_data.no_clip;
        globalGameState.input.worldDrawQuality = world_draw_quality_step( WorldDrawQuality::LOW, saved_data.worldDrawQuality );
        globalGameState.input.shadow_quality = static_cast<ShadowQuality>( saved_data.shadow_quality );
        light_shadow_smooth_set( globalGameState.input.shadow_quality == SHADOW_SMOOTHER );
        globalGameState.hotbar.applySavedInventory( saved_data.hotbar_inventory );
        globalGameState.hotbar.setSelectedSlot( saved_data.selected_hotbar_slot );
        globalGameState.game_mode = saved_data.game_mode;
        globalGameState.peaceful = saved_data.peaceful;
        globalGameState.survival_inventory.applySavedInventory( saved_data.survival_inventory );
        globalGameState.world_time = saved_data.world_time;
        globalGameState.spawn_point_set = saved_data.has_spawn;
        globalGameState.spawn_point = glm::ivec3( saved_data.spawn_x, saved_data.spawn_y, saved_data.spawn_z );
        // Saved render distance wins over the default but not over an
        // explicit REPGAME_RENDER_DISTANCE. Old saves lack the field (0)
        // and corrupt values below 1 fall back to the default; oversized
        // values clamp to the torus max.
        if ( !env_is_set( "REPGAME_RENDER_DISTANCE" ) && saved_data.render_distance >= 1 ) {
            int r = saved_data.render_distance;
            if ( r > RENDER_RADIUS_MAX ) {
                r = RENDER_RADIUS_MAX;
            }
            globalGameState.input.render_distance = r;
        }
        // Same idea for the cave-brightness floor: the saved value wins
        // over the default but not over REPGAME_MIN_AMBIENT; old saves
        // lack the field (-1) and corrupt values clamp to [0, max].
        if ( !env_is_set( "REPGAME_MIN_AMBIENT" ) && saved_data.min_ambient >= 0.0f ) {
            imgui_overlay_get_imgui_debug_vars( ).min_ambient = glm::clamp( saved_data.min_ambient, 0.0f, MIN_AMBIENT_MAX );
        }
        // Saved health: absent/negative in old saves -> full. A saved 0
        // means the player quit on the death screen — respawn them fresh
        // at the world spawn (position snap via spawn_pending), like the
        // death screen's Respawn button.
        if ( saved_data.health >= 0.0f ) {
            globalGameState.vitals.health = glm::clamp( saved_data.health, 0.0f, PLAYER_MAX_HEALTH );
            if ( globalGameState.vitals.health <= 0.0f ) {
                globalGameState.vitals.health = PLAYER_MAX_HEALTH;
                globalGameState.spawn_pending = true;
            }
        }
    }
    // Display name precedence: saved player.dat name > "Player" (the
    // server dedupes collisions into Player_2, ...). Candidates are
    // sanitized (chat name charset); a save value that sanitizes to empty
    // falls through. The persisted value stays the *requested* name — a
    // server-deduped spelling is never written back.
    {
        std::string name = saved_data.name;
        chat_sanitize_name( name );
        if ( name.empty( ) ) {
            name = "Player";
        }
        globalGameState.player_name = name;
    }
    // Fly mode pins the camera every tick, but the pin runs after the network
    // tick: start at the pin so the first VIEW/location is already correct.
    if ( RepGame::fly_mode( ) ) {
        globalGameState.camera.pos = run_policy().fly.start;
        globalGameState.spawn_pending = false;
    }
    // Seed the interpolation snapshot so the first rendered frame doesn't blend from zeroes.
    globalGameState.camera.snapshot_prev( );
    ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
    globalGameState.ui.overlay.set_holding_item( selectedBlock );
}

MK_TEXTURE( blocks, 384, BLOCK_TEXTURE_ATLAS_ROWS * 16, 16, 16 );

static bool globalSupportsAnisotropic;

bool RepGame::supportsAnisotropic( ) {
    return globalSupportsAnisotropic;
}

RepGameState *RepGame::init( const char *world_name, const bool connect_multi, const char *host, const bool supportsAnisotropicFiltering ) {
    globalGameState.screen.width = DEFAULT_WINDOW_WIDTH;
    globalGameState.screen.height = DEFAULT_WINDOW_HEIGHT;
    globalSupportsAnisotropic = supportsAnisotropicFiltering;

    // Flat-world marker (`<world>/world_type` = "flat") must be read before
    // any chunk generation starts — the map storage init happens later.
    MapStorage::detect_flat_world( world_name );

    // Start broadcasting chunk updates
    if ( connect_multi && !repgame_block_world( ) && !repgame_flat_world( ) ) {
        int port = 25566;
        port = env_int( "REPGAME_PORT", port );
        globalGameState.multiplayer.init( host, port );
    } else if ( connect_multi && repgame_flat_world( ) ) {
        pr_debug( "Flat world: skipping multiplayer connect to %s", host );
    }

    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_BACK );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glBlendEquation( GL_FUNC_ADD );

    VertexBufferLayout vbl_ui_overlay_vertex;
    // These are from UIOverlayVertex
    vbl_ui_overlay_vertex.push_float( 2 );        // UIOverlayVertex screen_x, screen_y
    vbl_ui_overlay_vertex.push_float( 2 );        // UIOverlayVertex texture
    vbl_ui_overlay_vertex.push_unsigned_int( 1 ); // UIOverlayVertex is_isometric
    vbl_ui_overlay_vertex.push_unsigned_int( 1 ); // UIOverlayVertex face_type
    vbl_ui_overlay_vertex.push_float( 3 );        // UIOverlayVertex cube_position

    VertexBufferLayout vbl_ui_overlay_instance;
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance screen_x, screen_y, screen_z
    vbl_ui_overlay_instance.push_float( 2 );        // UIOverlayInstance width, height
    vbl_ui_overlay_instance.push_unsigned_int( 1 ); // UIOverlayInstance is_block
    vbl_ui_overlay_instance.push_unsigned_int( 1 ); // UIOverlayInstance is_isometric
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance id_isos
    vbl_ui_overlay_instance.push_float( 4 );        // UIOverlayInstance tint
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance block_scale
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance block_offset
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance block_tex_offset

    globalGameState.blocksTexture.init( texture_source_blocks, 0 );
    block_definitions_initilize_definitions( &globalGameState.blocksTexture );

    globalGameState.ui.creative.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, MAIN_INVENTORY_WIDTH, MAIN_INVENTORY_HEIGHT );
    globalGameState.survival_inventory.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, SURVIVAL_INVENTORY_WIDTH, SURVIVAL_INVENTORY_HEIGHT );
    globalGameState.hotbar.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, HOTBAR_WIDTH, HOTBAR_HEIGHT );
    globalGameState.ui.chest.inventory_renderer.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, CHEST_INVENTORY_WIDTH, CHEST_INVENTORY_HEIGHT );
    globalGameState.ui.furnace.input.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, 1, 1 );
    globalGameState.ui.furnace.fuel.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, 1, 1 );
    globalGameState.ui.furnace.output.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, 1, 1 );
    globalGameState.ui.crafting.grid2.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, 2, 2 );
    globalGameState.ui.crafting.grid3.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, 3, 3 );
    globalGameState.ui.crafting.output.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, 1, 1 );
    globalGameState.game_mode = GameMode_Creative;
    // Fresh worlds (and block/flat worlds) default to peaceful; a save may
    // overwrite this below.
    globalGameState.peaceful = true;
    globalGameState.ui.held_slot.item_id = LAST_BLOCK_ID;
    globalGameState.ui.held_slot.quantity = 0;
    globalGameState.ui.holding_slot = false;

    initializeGameState( world_name );

    // Multiplayer clients never touch local chunk/chest/furnace files —
    // set the gate before world.init enqueues the first chunk loads.
    globalGameState.map_storage.set_chunk_io_enabled( !globalGameState.multiplayer.multiplayer_mode( ) );
    globalGameState.world.init( globalGameState.camera.pos, globalGameState.screen.width, globalGameState.screen.height, globalGameState.map_storage, globalGameState.input.render_distance );
    globalGameState.world.multiplayer = &globalGameState.multiplayer;

    globalGameState.ui.overlay.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance );
    globalGameState.ui.pause.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance );
    imgui_overlay_init( &globalGameState.ui.imgui );
    globalGameState.ui.font.init( );
    globalGameState.world.init_sign_text( globalGameState.ui.font );
    globalGameState.ui.layout.init( globalGameState.ui.font );

    ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
    globalGameState.ui.overlay.set_holding_item( selectedBlock );

    int iMultiSample = 0;
    int iNumSamples = 0;
    glGetIntegerv( GL_SAMPLE_BUFFERS, &iMultiSample );
    glGetIntegerv( GL_SAMPLES, &iNumSamples );
    pr_debug( "GL_SAMPLE_BUFFERS = %d, GL_SAMPLES = %d", iMultiSample, iNumSamples );
    audio_init( );
    // Opens REPGAME_RESULTS and writes the header line (no-op when unset).
    results::init( world_name );
    return &globalGameState;
}

void RepGame::save_player_data( ) {
    PlayerData saved_data;
    saved_data.world_x = globalGameState.camera.pos.x;
    saved_data.world_y = globalGameState.camera.pos.y;
    saved_data.world_z = globalGameState.camera.pos.z;
    saved_data.angle_H = globalGameState.camera.angle_H;
    saved_data.angle_V = globalGameState.camera.angle_V;
    saved_data.flying = globalGameState.input.player_flying;
    saved_data.no_clip = globalGameState.input.no_clip;
    // In VR the live quality is the session's REPGAME_VR_QUALITY — persist
    // the flat preference instead so a VR run never overwrites it.
    saved_data.worldDrawQuality = static_cast<int>( globalGameState.vr.active ? globalGameState.vr.saved_quality : globalGameState.input.worldDrawQuality );
    saved_data.shadow_quality = static_cast<int>( globalGameState.input.shadow_quality );
    globalGameState.hotbar.saveInventory( saved_data.hotbar_inventory );
    saved_data.selected_hotbar_slot = globalGameState.hotbar.getSelectedSlot( );
    saved_data.game_mode = globalGameState.game_mode;
    saved_data.peaceful = globalGameState.peaceful;
    globalGameState.survival_inventory.saveInventory( saved_data.survival_inventory );
    saved_data.world_time = globalGameState.world_time;
    saved_data.render_distance = globalGameState.input.render_distance;
    saved_data.min_ambient = imgui_overlay_get_imgui_debug_vars( ).min_ambient;
    saved_data.health = globalGameState.vitals.health;
    saved_data.name = globalGameState.player_name;
    saved_data.has_spawn = globalGameState.spawn_point_set;
    saved_data.spawn_x = globalGameState.spawn_point.x;
    saved_data.spawn_y = globalGameState.spawn_point.y;
    saved_data.spawn_z = globalGameState.spawn_point.z;

    globalGameState.map_storage.write_player_data( saved_data );
}

void RepGame::cleanup( ) {
    static bool clean_up_done = false;
    if ( clean_up_done ) {
        return;
    }

    // Return any crafting-grid contents before the save so quitting with
    // items in the grid doesn't lose them.
    repgame_crafting_return_grid( globalGameState );
    // Dismount before the player.dat write so the saved pos is a free
    // spot, not the cart seat.
    if ( globalGameState.camera.riding_cart != 0 ) {
        repgame_cart_dismount( globalGameState );
    }
    // Capture/fly/script mode never writes player.dat — it would store the
    // teleported view over the user's save.
    if ( !run_policy().capture.enabled && !run_policy().fly.enabled && !run_policy().script.enabled ) {
        save_player_data( );
    }

    // In-flight piston moves land before teardown — MOVING_PISTON must
    // never persist in a save. Relay goes out before multiplayer cleanup.
    while ( !globalGameState.piston_moves.empty( ) ) {
        piston_land_cell( globalGameState, globalGameState.piston_moves.begin( )->first );
    }
    globalGameState.multiplayer.cleanup( );
    // Write live block bodies back into the world before the save tears down.
    globalGameState.world.bodies.land_all( globalGameState );
    globalGameState.world.cleanup( globalGameState.map_storage );
    globalGameState.blocksTexture.destroy( );
    globalGameState.ui.overlay.cleanup( );
    globalGameState.ui.layout.destroy( );
    globalGameState.ui.font.cleanup( );
    globalGameState.ui.creative.cleanup( );
    globalGameState.survival_inventory.cleanup( );
    globalGameState.hotbar.cleanup( );
    globalGameState.ui.chest.inventory_renderer.cleanup( );
    globalGameState.ui.crafting.grid2.cleanup( );
    globalGameState.ui.crafting.grid3.cleanup( );
    globalGameState.ui.crafting.output.cleanup( );
    globalGameState.ui.pause.cleanup( );
    imgui_overlay_cleanup( &globalGameState.ui.imgui );
    block_definitions_free_definitions( );
    audio_cleanup( );

    // Last results record — a missing summary line marks an abnormal exit.
    results::summary( globalGameState );
    clean_up_done = true;
    // pr_debug( "RepGame cleanup done" );
}
