#ifndef HEADER_RENDER_ORDER_H
#define HEADER_RENDER_ORDER_H

typedef enum { RenderOrder_Transparent, RenderOrder_Glass, RenderOrder_Water, RenderOrder_Solid, LAST_RENDER_ORDER } RenderOrder;

int chunk_get_render_order_ib_size( RenderOrder renderOrder );
int chunk_get_render_order_is_solid( RenderOrder renderOrder );
int chunk_get_render_order_casts_shadow( RenderOrder renderOrder );
int chunk_get_render_order_is_visible( RenderOrder renderOrder );

#endif