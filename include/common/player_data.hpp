#pragma once

// player.dat — the player's saved position/inventory/settings. On disk the
// file is a versioned human-readable text format (see player_data.cpp); it
// is written and read only once per run, so readability beats size. Older
// binary layouts are still accepted on load (frozen structs in
// player_data.cpp); new fields go in as new keys — never rename or reuse an
// existing key.

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "common/constants.hpp"
#include "common/block_definitions.hpp"

typedef enum {
    GameMode_Creative = 0,
    GameMode_Survival = 1,
} GameMode;

// In-memory shape of the player save. Not packed — the disk format is text.
struct PlayerData {
    PlayerData( );

    double world_x = 0.0;
    double world_y = 0.0;
    double world_z = 0.0;
    float angle_H = 0.0f;
    float angle_V = 0.0f;
    bool flying = false;
    bool no_clip = false;
    int worldDrawQuality = 0;
    // ShadowQuality (input.hpp) — 0 SHADOW_SMOOTH, 1 SHADOW_SMOOTHER (default;
    // absent in old saves).
    int shadow_quality = 1;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot = 0;
    GameMode game_mode = GameMode_Creative;
    // Absent in old saves -> peaceful (hostile mobs don't target the player).
    bool peaceful = true;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
    int64_t world_time = 0;
    // 0 = absent (old saves) -> the default render distance is used.
    int render_distance = 0;
    // Negative = absent (old saves) -> the default min_ambient is used.
    float min_ambient = -1.0f;
    // Negative = absent (old saves) -> full health. A saved 0 means the
    // player exited on the death screen -> respawned fresh at spawn.
    float health = -1.0f;
    // Requested multiplayer display name (the server-accepted spelling is
    // never persisted). Absent in old saves -> empty, init falls back to
    // $USER/"Player".
    std::string name;
    // Bed respawn point (docs/player.md). Absent in old saves -> spawn at
    // the world-spawn column.
    bool has_spawn = false;
    int spawn_x = 0;
    int spawn_y = 0;
    int spawn_z = 0;
};

// Serializes player data to the text format written to player.dat.
std::string player_data_serialize( const PlayerData &data );

// Parses a player.dat buffer: the text format first, then each frozen
// legacy binary layout. Returns false only when nothing was recognized —
// the caller treats that as "no save" and spawns fresh.
bool player_data_parse( const unsigned char *buf, size_t len, PlayerData &out );
