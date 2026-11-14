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

#include "common/chat.hpp"

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
    // Requested display name, sent as PLAYER_NAME once WELCOME lands and
    // re-sent on /name; the server-accepted spelling lives in
    // multiplayer.player_names. Persisted in player.dat.
    std::string player_name;
    // Session chat log (net frames + local lines feed it; the chat UI
    // only reads it). Session-only — never persisted.
    ChatLog chat;
    long tick_number;
    // Day/night clock in ticks (see DAY_LENGTH_TICKS in light.hpp).
    // Persisted in player.dat via PlayerData::world_time.
    long world_time;
    // Fresh spawns (no player.dat) start on the terrain-height estimate but
    // can't see structure blocks until the spawn column is generated. While
    // true, RepGame::tick waits for the column to load and snaps the player
    // onto the topmost colliding block.
    bool spawn_pending;
    // Respawn point set by right-clicking a bed (repgame_try_sleep —
    // docs/player.md). While spawn_point_set, spawn_pending/resolve_pending_spawn
    // target this column instead of the (0,0) world spawn; a missing or
    // obstructed bed clears it back to the world spawn. Persisted in
    // player.dat.
    glm::ivec3 spawn_point;
    bool spawn_point_set;
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
    // Fluid cells with a FluidUpdateEvent queued: added when a face neighbor
    // changed (or a random tick rescues a flow frozen by chunk unload),
    // erased by the event when it fires. At most one update per fluid cell.
    std::unordered_set<glm::ivec3> pending_fluid_updates;
    // Repeater cells with a RepeaterEvent queued: added when a neighbor
    // change left the input disagreeing with the output (or the event
    // re-arms itself for pulse extension), erased when the event fires.
    // At most one pending tick per repeater.
    std::unordered_set<glm::ivec3> pending_repeater_ticks;

    // Local player: input, camera, block interaction, and the player's own
    // inventories (persisted in player.dat; the panels in `ui` render them).
    Input input;
    CameraState camera;
    PlayerVitals vitals;
    Hotbar hotbar;
    SurvivalInventory survival_inventory;
    BlockSelection block_selection;
    BlockMining block_mining;
    HeldSwing held_swing;

    // Display state and the block-atlas texture.
    ScreenState screen;
    Texture blocksTexture;

    // Panels, overlays, and the dragged-slot cursor.
    UiState ui;

    // OpenXR session state.
    VrState vr;
};
