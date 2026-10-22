#pragma once

#include "renderer/index_buffer.hpp"
#include "renderer/vertex_buffer.hpp"
#include "renderer/vertex_array.hpp"
#include "renderer/renderer.hpp"
#include "common/RenderChain.hpp"
#include "RepGame.hpp"
#include "input.hpp"

#define ORDER_Z_INV_ITEMS_BG 0.0f
#define ORDER_Z_INV_SELECTED_SLOT 0.1f
#define ORDER_Z_INV_SLOT_BG 0.2f
#define ORDER_Z_INV_BLOCKS 0.3f

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

#include "common/creative_inventory.hpp"
#include "common/chest.hpp"
#include "common/hotbar.hpp"
#include "common/survival_inventory.hpp"
#include "common/font_renderer.hpp"

class InventoryRenderer;

class UIOverlay {
    Shader shader;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_crosshair;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_held_block;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_dragged_item;
    BlockID heldBlockID;
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
    void set_holding_block( BlockID holding_block );
    // chest_renderer/chest_contents are non-null while a chest is open: the
    // creative palette is suppressed and the chest panel draws on top of the
    // player inventory. held_block_light is the scene light sampled at the
    // camera (sky and torch light combined with the day/night factor), used
    // to shade the in-hand block like the world does.
    void draw( CreativeInventory &creative_inventory, SurvivalInventory &survival_inventory, Hotbar &hotbar, GameMode game_mode, const Renderer &renderer, const Texture &blocksTexture, Input &input, FontRenderer &font, const glm::mat4 &mvp_ui, InventoryRenderer *chest_renderer, const ChestContents *chest_contents, float held_block_light );
    void draw_held_inventory_item( const InventorySlot &held, bool is_holding, int mouse_x, int mouse_y, int block_size, int cell_size, const Renderer &renderer, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp_ui );
    void cleanup( );
};