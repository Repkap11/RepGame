#pragma once

#include "common/inventory_renderer.hpp"
#include "common/item.hpp"
#include <map>

// By-reference draw() params only — forward declared so this header stays
// standalone-parseable.
class FontRenderer;
struct UiRect;

// Max chars the creative search box holds (not counting the terminator).
#define CREATIVE_SEARCH_MAX 48

// Name match for the creative search box: case-insensitive substring match
// where '_' acts as a space on both sides, so raw queries work.
bool creative_search_matches( const char *name, const char *query );

class CreativeInventory {
    // Headless test access (test_creative): fields are filled directly
    // since init() does GL work — mirrors InventoryRenderer's friends.
    friend int test_creative( );
    int width;
    int height;
    int num_blocks_max;
    InventorySlot *slots;
    int selected_page;
    // Search-box query ('f' focuses the box while the palette is open):
    // empty is the normal paged view; non-empty filters CREATIVE_BLOCKS by
    // item_name. Normalized lowercase with '_' folded to ' '.
    char search_text[ CREATIVE_SEARCH_MAX ];
    int search_len;
    // The search box: a single tinted quad laid out above the panel top
    // edge (centered screen coords), rebuilt by apply_layout.
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_search;
    float search_box_x;
    float search_box_y;
    float search_box_w;
    float search_box_h;
    int screen_width;
    int screen_height;
    void load_blocks_for_selected_page( );

  public:
    InventoryRenderer inventory_renderer;
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height );
    // Adopts the layout pass's items-slab and search-box rects and rebuilds
    // the slot + search-bar instances (compute_layout must already have
    // sized the grid).
    void apply_layout( int width, int height, const UiRect &items, const UiRect &search );
    // search_visible draws the search box + its text above the panel (the
    // flag lives in Input::search_focused — the palette itself only owns
    // the query text).
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader, bool search_visible, FontRenderer &font, const glm::mat4 &mvp_ui );
    void incrementSelectedPage( int offset );
    ItemID whichItemClicked( int screen_x, int screen_y );
    // Search edits; each refills the slots from the new query.
    void searchAppendChar( char c );
    void searchBackspace( );
    void searchClear( );
    const char *searchText( ) const {
        return this->search_text;
    }
    // True when the raw screen point is inside the search box rect (same
    // centered, y-up coords as InventoryRenderer::containsPoint).
    bool searchBoxContainsPoint( int screen_x, int screen_y ) const;
    void cleanup( );
};
