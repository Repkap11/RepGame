#include "common/mobs.hpp"
void mobs_init( Mobs *mobs ) {
    {
        // These are from CubeFace
        vertex_buffer_layout_init( &mobs->vbl_mob_shape );
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 3 ); // Coords
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 2 ); // Texture coords
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 1 ); // Face type (top, sides, bottom)
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 1 ); // Corner_shift

        // These are from BlockCoords
        vertex_buffer_layout_init( &mobs->vbl_mob_placement );
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 3 ); // block 3d world coords
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 3 ); // Multiples (mesh)
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 3 ); // which texture (block type 1)
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 3 ); // which texture (block type 2)
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 3 ); // packed lighting
        vertex_buffer_layout_push_float( &mobs->vbl_mob_shape, 3 ); // packed lighting
    }
    {
        index_buffer_init( &mobs->ib );
        vertex_buffer_init( &mobs->vb_mob_shape );
        vertex_buffer_init( &mobs->vb_places );
        vertex_array_init( &mobs->va );
        vertex_array_add_buffer( &mobs->va, &mobs->vb_mob_shape, &mobs->vbl_mob_shape, 0, 0 );
        vertex_array_add_buffer( &mobs->va, &mobs->vb_places, &mobs->vbl_mob_placement, 1, mobs->vbl_mob_shape.current_size );
    }
}

void mobs_update_position( ) {
}

void mobs_draw( Mobs *mobs, Renderer *renderer, glm::mat4 &mvp ) {
}

void mobs_cleanup( Mobs *mobs ) {
}
