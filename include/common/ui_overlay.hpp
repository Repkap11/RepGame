#pragma once

#include "renderer/index_buffer.hpp"
#include "renderer/vertex_buffer.hpp"
#include "renderer/vertex_array.hpp"
#include "renderer/renderer.hpp"
#include "common/RenderChain.hpp"
#include "common/item.hpp"        // ItemID / InventorySlot
#include "common/player_data.hpp" // GameMode (draw takes it by value)

class Input;

// The floating-panel window backdrop (furnace/crafting groups): below the
// panel layers, though each panel's draw clears depth anyway — what orders
// it is being drawn before the panels it groups.
#define ORDER_Z_PANEL_BACKDROP -0.1f
#define ORDER_Z_INV_ITEMS_BG 0.0f
#define ORDER_Z_INV_SELECTED_SLOT 0.1f
#define ORDER_Z_INV_SLOT_BG 0.2f
#define ORDER_Z_INV_BLOCKS 0.3f
// Vitals HUD row (hearts/air bubbles) drawn above the hotbar.
#define ORDER_Z_VITALS_BG 0.4f
#define ORDER_Z_VITALS 0.5f

typedef struct {
    float screen_x;
    float screen_y;
    struct {
        float x, y;
    } texture;
    unsigned int is_isometric;
    unsigned int face_type;
    // Unit-cube corner (0-1) this vertex represents. Only used by isometric
    // block icons, which project it after applying the block's scale/offset.
    struct {
        float x, y, z;
    } cube_position;
} UIOverlayVertex;

struct UIOverlayInstance {
    float screen_x;
    float screen_y;
    float screen_z;
    float width;
    float height;
    unsigned int is_block;
    unsigned int is_isometric;
    float id_isos[ 3 ];
    float tint[ 4 ];
    // Block shape in pixels (0-16), matching Block::scale/offset/tex_offset.
    // Used to project isometric icon verts and adjust their UVs like the
    // chunk mesh does.
    float block_scale[ 3 ];
    float block_offset[ 3 ];
    float block_tex_offset[ 3 ];
};

// By-reference draw() params only — kept as forward declarations so this
// header stays out of the inventory-panel include web.
class CreativeInventory;
class SurvivalInventory;
class Hotbar;
class InventoryRenderer;
class FontRenderer;
struct ChestContents;
struct UiRect;

class UIOverlay {
    Shader shader;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_crosshair;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_held_block;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_dragged_item;
    // Window backdrops for the floating panel groups (furnace, crafting
    // grid + output) + the survival inventory they sit over. One quad per
    // draw_panel_backdrop call (the call clears the chain first).
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_panel_backdrop;
    // Flat icon instances for the hearts/air-bubble row above the hotbar —
    // cleared and rebuilt each draw_vitals call.
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_vitals;
    ItemID heldItemID = LAST_BLOCK_ID;
    int screen_width;
    int screen_height;
    // VR quad-layer mode: skip the screen-space in-hand block and the
    // crosshair (its ONE_MINUS_DST_COLOR blend can't work on a
    // transparent compositor layer).
    bool vr_mode = false;

  public:
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance );
    void on_screen_size_change( int width, int height );
    void set_vr_mode( bool vr_mode );
    void set_holding_item( ItemID holding_item );
    // The item shown in hand — draw_ui_pass feeds it to the extruded-sprite
    // held-item draw (flat items) while this class draws isometric blocks.
    ItemID held_item( ) const { return this->heldItemID; }
    // chest_renderer/chest_contents are non-null while a chest is open: the
    // creative palette is suppressed and the chest panel draws on top of the
    // player inventory. held_block_light is the scene light sampled at the
    // camera (sky and torch light combined with the day/night factor), used
    // to shade the in-hand block like the world does.
    void draw( CreativeInventory &creative_inventory, SurvivalInventory &survival_inventory, Hotbar &hotbar, GameMode game_mode, const Renderer &renderer, const Texture &blocksTexture, Input &input, FontRenderer &font, const glm::mat4 &mvp_ui, InventoryRenderer *chest_renderer, const ChestContents *chest_contents, bool crafting_table_open, bool furnace_open, float held_block_light );
    // Draws a panel plus the stack-size labels for its first n slots.
    void draw_slot_panel( InventoryRenderer &panel, const InventorySlot *slots, int n, const Renderer &renderer, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp_ui );
    // Paints the window-backdrop quad that frames a floating panel group
    // plus the survival inventory (furnace/crafting screens). The rect is
    // in centered y-up UI pixels; call before draw() so the panels and the
    // inventory slab composite inside it.
    void draw_panel_backdrop( int left, int bottom, int width, int height, const Renderer &renderer, const glm::mat4 &mvp_ui );
    void draw_held_inventory_item( const InventorySlot &held, bool is_holding, int mouse_x, int mouse_y, int block_size, int cell_size, const Renderer &renderer, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp_ui );
    // Heart containers + full/half hearts left-aligned, air bubbles
    // right-aligned while air is below max (underwater). `row` is the
    // layout pass's vitals rect — hotbar-slab width, icon-height tall —
    // so the row tracks the strip it belongs to.
    void draw_vitals( float health, float max_health, int air_ticks, int max_air_ticks, const UiRect &row, const Renderer &renderer, const Texture &blocksTexture, const glm::mat4 &mvp_ui );
    // Exposes the shared ui_overlay shader so the pause menu (drawn from
    // RepGame::draw_ui_pass) uses the same program.
    Shader &get_shader( ) {
        return this->shader;
    }
    void cleanup( );
};