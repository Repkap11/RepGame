#pragma once

// The panel/window layer of RepGameState: chest and crafting container UIs,
// inventory panels, overlays, menus, and the mouse-dragged stack.

#include <glm/glm.hpp>

#include "common/player_data.hpp" // GameMode (used by ui_overlay.hpp)
#include "common/ui_overlay.hpp"
#include "common/imgui_overlay.hpp"
#include "common/font_renderer.hpp"
#include "common/creative_inventory.hpp"
#include "common/pause_menu.hpp"
#include "common/inventory_renderer.hpp"
#include "common/block_definitions.hpp" // InventorySlot
#include "common/chest.hpp"             // CHEST_SLOTS
#include "common/furnace.hpp"

// Window backdrop rect behind a group of panels (the furnace slot trio,
// the crafting grid + output) AND the survival inventory below them, in
// centered y-up UI pixels — one unified window. The layout helpers fill it
// in; the draw pass paints it before the panels and the click router
// treats its dead areas as inside-the-window (not "outside every panel").
struct UiPanelBackdrop {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    // Centered, y-up UI coords (the space InventoryRenderer::containsPoint
    // works in after its screen-to-center transform).
    bool contains( int cx, int cy ) const {
        return cx >= this->x && cx < this->x + this->w && cy >= this->y && cy < this->y + this->h;
    }
};

// Chest container the player currently has open (at most one). Contents
// live in the owning chunk (World::get_chest_contents); this renderer
// draws them as a top-aligned panel.
struct ChestUi {
    bool open;
    glm::ivec3 pos;
    InventoryRenderer inventory_renderer;
    // Window rect framing the chest slab and the survival inventory below,
    // set by repgame_chest_layout.
    UiPanelBackdrop backdrop;
    // Last slot values pushed to the renderer. The net layer only writes
    // World state (pull model), so the tick loop repaints through
    // repgame_chest_sync_icons when the world contents differ from this
    // snapshot — same pattern as FurnaceUi::drawn.
    InventorySlot drawn[ CHEST_SLOTS ];
};

// Crafting grid: 2x2 in the survival inventory, 3x3 while a crafting
// table is open (either game mode). Grid contents are temporary — they
// return to the player when the UI closes. slots are in renderer order
// (slot 0 is the BOTTOM-left cell); the matcher wants top-first.
struct CraftingUi {
    bool table_open;
    glm::ivec3 table_pos;
    InventorySlot slots[ 9 ];
    InventorySlot output_slot;
    int recipe;
    InventoryRenderer grid2, grid3, output;
    // Window rect framing the active grid + output and the survival
    // inventory below, set by repgame_crafting_layout.
    UiPanelBackdrop backdrop;
    // In-progress button-drag split: the applied state is live in slots /
    // held; the snapshots let each new target re-distribute from scratch.
    struct {
        bool active;
        bool right;
        InventorySlot snapshot_slots[ 9 ];
        InventorySlot snapshot_held;
        int targets[ 9 ];
        int count;
    } drag;
};

// Furnace container the player currently has open (at most one). Contents
// live in the owning chunk (World::get_furnace_contents); the three 1x1
// renderers draw the input/fuel/output slots in a triangle above the
// player inventory.
struct FurnaceUi {
    bool open;
    glm::ivec3 pos;
    InventoryRenderer input, fuel, output;
    // Window rect framing the three panels (and the progress text between
    // them) plus the survival inventory below, set by
    // repgame_furnace_layout.
    UiPanelBackdrop backdrop;
    // Last slot values pushed to the renderers. Furnace contents also change
    // from ticking (fuel consumed, smelts completing), so the tick loop
    // repaints through repgame_furnace_sync_icons when the world contents
    // differ from this snapshot.
    InventorySlot drawn[ FURNACE_INVENTORY_SLOTS ];
};

// The panel/window layer drawn into the UI pass: inventory grids,
// overlays, menus, and the mouse-dragged stack. Distinct from the player
// inventories in RepGameState, which are game data these panels render.
struct UiState {
    UIOverlay overlay;
    ImGuiOverlay imgui;
    FontRenderer font;
    CreativeInventory creative;
    PauseMenu pause;
    // Item stack currently being dragged with the mouse (Minecraft-style
    // click-to-pick-up / click-to-place). When holding_slot is true,
    // held_slot contains the stack picked up from a slot.
    InventorySlot held_slot;
    bool holding_slot;
    ChestUi chest;
    CraftingUi crafting;
    FurnaceUi furnace;
};
