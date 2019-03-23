#include "common/render_order.hpp"
#include "common/block.hpp"

int render_order_ib_size( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Water:
            return IB_WATER_SIZE;
        default:
            return IB_SOLID_SIZE;
    }
}

int render_order_is_pickable( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Transparent:
        case RenderOrder_Water:
            return false;
        default:
            return true;
    }
}

int render_order_casts_shadow( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Solid:
        case RenderOrder_Leafs:
            return true;
        default:
            return false;
    }
}
int render_order_is_visible( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Transparent:
            return false;
        default:
            return true;
    }
}
int render_order_can_mesh( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Solid:
        case RenderOrder_Water:

            return true;
        default:
            return false;
    }
}
