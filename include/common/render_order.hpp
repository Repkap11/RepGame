#pragma once

typedef enum { RenderOrder_Transparent, RenderOrder_Translucent, RenderOrder_Water, RenderOrder_Flowers, RenderOrder_Opaque, LAST_RENDER_ORDER } RenderOrder;

// Bit for ChunkLoader::draw's order_mask (which render orders to draw).
constexpr unsigned render_order_bit( RenderOrder o ) { return 1u << o; }

int render_order_ib_size( RenderOrder renderOrder );
int render_order_casts_shadow( RenderOrder renderOrder );
int render_order_is_visible( RenderOrder renderOrder );
int render_order_can_mesh( RenderOrder renderOrder );
int render_order_can_be_shaded( RenderOrder renderOrder );