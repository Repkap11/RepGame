#include "common/mobs.hpp"
#include "common/RepGame.hpp"

static unsigned int ib_data_player[] = {
    0, 1, 2, //
    2, 3, 0, //

    0, 3, 2, //
    2, 1, 0, //

};
#define IB_PLAYER_SIZE ( 3 * 4 )

void mobs_init( Mobs *mobs, VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement ) {

    {
        index_buffer_init( &mobs->ib );
        vertex_buffer_init( &mobs->vb_mob_shape );
        vertex_buffer_init( &mobs->vb_mob_placement );
        vertex_array_init( &mobs->va );
        vertex_array_add_buffer( &mobs->va, &mobs->vb_mob_shape, vbl_object_vertex, 0, 0 );
        vertex_array_add_buffer( &mobs->va, &mobs->vb_mob_placement, vbl_object_placement, 1, vbl_object_vertex->current_size );
    }
    {
        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
            mobs->mob_position.face[ i ] = DIRT - 1;
        }
        mobs->mob_position.face[ FACE_FRONT ] = GOLD_BLOCK - 1;
        mobs->mob_position.face[ FACE_BACK ] = DIAMOND_BLOCK - 1;
        mobs->shouldDraw = 1;
    }
    {
        vertex_buffer_set_data( &mobs->vb_mob_shape, vd_data_player_object, sizeof( ObjectVertex ) * VB_DATA_SIZE_PLAYER );
        vertex_buffer_set_data( &mobs->vb_mob_placement, &mobs->mob_position, sizeof( ObjectPosition ) * 1 );
        index_buffer_set_data( &mobs->ib, ib_data_solid, IB_SOLID_SIZE );
    }
}

void mobs_update_position( Mobs *mobs, float x, float y, float z, glm::mat4 &rotation ) {
    mobs->mob_position.transform = glm::translate( glm::mat4( 1.0 ), glm::vec3( x + 0.5f, y + 0.5f, z + 0.5f ) ) * rotation * glm::translate( glm::mat4( 1.0 ), glm::vec3( -0.5f, -0.5f, -0.5f ) );
    vertex_buffer_set_data( &mobs->vb_mob_placement, &mobs->mob_position, sizeof( ObjectPosition ) * 1 );
}

void mobs_draw( Mobs *mobs, Renderer *renderer, Shader *shader ) {
    if ( mobs->shouldDraw ) {
        renderer_draw( renderer, &mobs->va, &mobs->ib, shader, 1 );
    }
    showErrors( );
}

void mobs_cleanup( Mobs *mobs ) {
}
