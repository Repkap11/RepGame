#ifndef HEADER_RENDER_ORDER_H
#define HEADER_RENDER_ORDER_H

typedef enum { RenderOrder_Transparent, RenderOrder_Water, RenderOrder_Glass, RenderOrder_Flowers, RenderOrder_Leafs, RenderOrder_Solid, LAST_RENDER_ORDER } RenderOrder;

int render_order_ib_size( RenderOrder renderOrder );
int render_order_is_pickable( RenderOrder renderOrder );
int render_order_casts_shadow( RenderOrder renderOrder );
int render_order_is_visible( RenderOrder renderOrder );
int render_order_can_mesh( RenderOrder renderOrder );
int render_order_collides_with_player( RenderOrder renderOrder );
int render_order_can_be_shaded( RenderOrder renderOrder );
float render_order_get_no_light( RenderOrder renderOrder );

#endif