#pragma once

// The panel/window layer of RepGameState: chest and crafting container UIs,
// inventory panels, overlays, menus, and the mouse-dragged stack.

#include <glm/glm.hpp>

#include "common/player_data.hpp" // GameMode (used by ui_overlay.hpp)
#include "common/ui_overlay.hpp"
#include "common/ui_layout.hpp"
#include "common/imgui_overlay.hpp"
#include "common/font_renderer.hpp"
#include "common/creative_inventory.hpp"
#include "common/pause_menu.hpp"
#include "common/touch_controls.hpp"
#include "common/inventory_renderer.hpp"
#include "common/block_definitions.hpp" // InventorySlot
#include "common/chest.hpp"             // CHEST_SLOTS
#include "common/furnace.hpp"
#include "common/sign.hpp"

#include <deque>
#include <string>

// Chest container the player currently has open (at most one). Contents
// live in the owning chunk (World::get_chest_contents); this renderer
// draws them as a top-aligned panel.
struct ChestUi {
    bool open;
    glm::ivec3 pos;
    InventoryRenderer inventory_renderer;
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
    // Last slot values pushed to the renderers. Furnace contents also change
    // from ticking (fuel consumed, smelts completing), so the tick loop
    // repaints through repgame_furnace_sync_icons when the world contents
    // differ from this snapshot.
    InventorySlot drawn[ FURNACE_INVENTORY_SLOTS ];
};

// The modal sign editor: four editable lines shown over the world while
// input.text_input_active captures the keyboard. `text` is the live edit
// buffer — nothing is committed until close, when the chunk's stored text
// is written (World::set_sign_text) and, in multiplayer, sent as SIGN_TEXT.
struct SignEditUi {
    bool open = false;
    // World position of the sign being edited.
    glm::ivec3 pos = glm::ivec3( 0 );
    SignText text;
    // The line the cursor is on (0..SIGN_LINES-1).
    int line = 0;
    // Tick the editor opened; the "still a sign?" close check waits a few
    // ticks because placement queues the block write for the next tick.
    long opened_tick = 0;
};

// The chat input box: a one-line editor over the scrollback, opened by
// 't' (or '/', which pre-fills a command slash). While open it owns text
// capture like the sign editor. `sent` is the Up/Down history of lines
// this client submitted (deduped, capped at CHAT_HISTORY_MAX);
// `history_pos` indexes it (-1 = editing `draft` below it).
struct ChatUi {
    bool open = false;
    // The live edit buffer (capped at CHAT_TEXT_MAX).
    std::string line;
    std::deque<std::string> sent;
    int history_pos = -1;
    // The draft `line` stashed when history recall begins.
    std::string draft;
    long opened_tick = 0;
};

// The panel/window layer drawn into the UI pass: inventory grids,
// overlays, menus, and the mouse-dragged stack. Distinct from the player
// inventories in RepGameState, which are game data these panels render.
struct UiState {
    UIOverlay overlay;
    // The Clay-backed screen-space layout pass; rects() names every
    // region draw/input code reads.
    UiLayout layout;
    ImGuiOverlay imgui;
    FontRenderer font;
    CreativeInventory creative;
    PauseMenu pause;
    // On-screen touch controls (shared Android/WASM port of
    // UIOverlayView); active while input.touch_ui.
    TouchControls touch;
    // Item stack currently being dragged with the mouse (Minecraft-style
    // click-to-pick-up / click-to-place). When holding_slot is true,
    // held_slot contains the stack picked up from a slot.
    InventorySlot held_slot;
    bool holding_slot;
    ChestUi chest;
    CraftingUi crafting;
    FurnaceUi furnace;
    SignEditUi sign_edit;
    ChatUi chat;
};
