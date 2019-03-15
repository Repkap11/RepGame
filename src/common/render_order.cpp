#include "common/render_order.hpp"
#include "common/block.hpp"

int chunk_get_render_order_ib_size( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Water:
            return IB_WATER_SIZE;
        default:
            return IB_SOLID_SIZE;
    }
}

int chunk_get_render_order_is_solid( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Transparent:
        case RenderOrder_Water:
            return false;
        default:
            return true;
    }
}
int chunk_get_render_order_casts_shadow( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Solid:
            return true;
        default:
            return false;
    }
}
int chunk_get_render_order_is_visible( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Transparent:
            return false;
        default:
            return true;
    }
}