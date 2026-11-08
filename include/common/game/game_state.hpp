#pragma once

// The whole client game state RepGame owns, and the named pieces it's
// composed of (player_state.hpp, ui_state.hpp, vr_state.hpp).
// RepGame.hpp is the API; this header is the data.

#include <unordered_set>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

// GameMode must exist before any UI header is pulled (ui_overlay.hpp's
// draw() signature takes it) — same role the old RepGame.hpp's early
// typedef played.
#include "common/player_data.hpp"

#include <entt/entity/registry.hpp>
#include "common/RenderChain.hpp"
#include "common/RenderLink.hpp"

#include "common/utils/map_storage.hpp"
#include "common/chunk_loader.hpp"
#include "common/world.hpp"
#include "common/constants.hpp"
#include "common/multiplayer.hpp"
#include "common/BlockUpdateQueue.hpp"
#include "common/crafting.hpp"
#include "common/input.hpp"
// ui_state.hpp must precede hotbar.hpp/survival_inventory.hpp: those two
// lead with inventory_renderer.hpp, which pulls ui_overlay.hpp mid-include —
// the cluster only resolves when ui_overlay.hpp is entered first.
#include "common/game/ui_state.hpp"
#include "common/hotbar.hpp"
#include "common/survival_inventory.hpp"
#include "common/renderer/texture.hpp"
#include "common/utils/ecs.hpp"
#include "common/game/player_state.hpp"
#include "common/game/vr_state.hpp"

struct ScreenState {
    float width;
    float height;
    glm::mat4 proj;
    glm::mat4 ortho;
    glm::mat4 ortho_center;
};

struct RepGameState {
    // World and session simulation.
    MapStorage map_storage;
    World world;
    Multiplayer multiplayer;
    BlockUpdateQueue blockUpdateQueue;
    GameMode game_mode;
    // Peaceful (default) = hostile mobs never target the local player;
    // hostile = they do. Game mode no longer factors in (see
    // RepGame::player_targetable). Persisted in player.dat.
    bool peaceful;
    long tick_number;
    // Day/night clock in ticks (see DAY_LENGTH_TICKS in light.hpp).
    // Persisted in player.dat via PlayerData::world_time.
    long world_time;
    // Fresh spawns (no player.dat) start on the terrain-height estimate but
    // can't see structure blocks until the spawn column is generated. While
    // true, RepGame::tick waits for the column to load and snaps the player
    // onto the topmost colliding block.
    bool spawn_pending;
    double frame_rate;
    // Pressure plates with a live PressurePlateEvent check chain. Positions
    // are added when the player steps on a plate and removed by the event
    // when nobody is on it (or it was broken/unloaded), so exactly one check
    // chain exists per plate.
    std::unordered_set<glm::ivec3> watched_pressure_plates;
    // Containers this player broke while connected to a server: the server
    // answers the block removal with a CONTAINER_CONTENTS snapshot used for
    // looting, so contents are never lost to a client that never opened it.
    std::unordered_set<glm::ivec3> pending_container_loot;
    // Natural leaves with a decay check queued: added when a neighbor change
    // may have removed their supporting log, erased by the LeafDecayEvent
    // when it fires. At most one check is queued per leaf.
    std::unordered_set<glm::ivec3> pending_leaf_decay;

    // Local player: input, camera, block interaction, and the player's own
    // inventories (persisted in player.dat; the panels in `ui` render them).
    Input input;
    CameraState camera;
    Hotbar hotbar;
    SurvivalInventory survival_inventory;
    BlockSelection block_selection;
    BlockMining block_mining;

    // Display state and the block-atlas texture.
    ScreenState screen;
    Texture blocksTexture;

    // Panels, overlays, and the dragged-slot cursor.
    UiState ui;

    // OpenXR session state.
    VrState vr;
};
