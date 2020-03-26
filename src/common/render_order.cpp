#include "common/render_order.hpp"
#include "common/block.hpp"

#define IB_FLOWERS_SIZE ( 3 * 4 * 2 )

int render_order_ib_size( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Water:
            return IB_WATER_SIZE;
        case RenderOrder_Flowers:
            return IB_FLOWERS_SIZE;
        default:
            return IB_SOLID_SIZE;
    }
}

int render_order_can_be_shaded( RenderOrder renderOrder ) {
    switch ( renderOrder ) {
        case RenderOrder_Opaque:
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
        case RenderOrder_Opaque:
        case RenderOrder_Water:
            return true;
        default:
            return false;
    }
}