#pragma once

typedef enum { RenderOrder_Transparent, RenderOrder_Water, RenderOrder_Flowers, RenderOrder_Opaque, LAST_RENDER_ORDER } RenderOrder;

int render_order_ib_size( RenderOrder renderOrder );
int render_order_casts_shadow( RenderOrder renderOrder );
int render_order_is_visible( RenderOrder renderOrder );
int render_order_can_mesh( RenderOrder renderOrder );
int render_order_can_be_shaded( RenderOrder renderOrder );