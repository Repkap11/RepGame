#include "common/mobs.hpp"
#include "common/RepGame.hpp"

void mobs_init( Mobs *mobs, VertexBufferLayout *vbl_mob_shape, VertexBufferLayout *vbl_mob_placement ) {

    {
        index_buffer_init( &mobs->ib );
        vertex_buffer_init( &mobs->vb_mob_shape );
        vertex_buffer_init( &mobs->vb_mob_placement );
        vertex_array_init( &mobs->va );
        vertex_array_add_buffer( &mobs->va, &mobs->vb_mob_shape, vbl_mob_shape, 0, 0 );
        vertex_array_add_buffer( &mobs->va, &mobs->vb_mob_placement, vbl_mob_placement, 1, vbl_mob_shape->current_size );
    }
    {
        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
            mobs->mob_placement.face[ i ] = DIRT - 1;
        }
        mobs->mob_placement.face[ FACE_FRONT ] = GOLD_BLOCK - 1;
        // mobs->mob_placement.face[ FACE_BACK ] = DIAMOND_BLOCK - 1;
        mobs->mob_placement.x = 0;
        mobs->mob_placement.y = 0;
        mobs->mob_placement.z = 0;
        mobs->mob_placement.mesh_x = 1;
        mobs->mob_placement.mesh_y = 1;
        mobs->mob_placement.mesh_z = 1;
        mobs->shouldDraw = 1;
    }
    {

    } {
        // vertex_buffer_set_data( &mobs->vb_mob_shape, &mobs->mob_shape, sizeof( CubeFace ) * 1 );
        vertex_buffer_set_data( &mobs->vb_mob_shape, vd_data_solid, sizeof( CubeFace ) * VB_DATA_SIZE_SOLID );
        vertex_buffer_set_data( &mobs->vb_mob_placement, &mobs->mob_placement, sizeof( BlockCoords ) * 1 );
        index_buffer_set_data( &mobs->ib, ib_data_solid, IB_SOLID_SIZE );
    }
}

void mobs_update_position( Mobs *mobs, float x, float y, float z, glm::mat4 &rotation ) {
    mobs->mob_placement.x = x;
    mobs->mob_placement.y = y;
    mobs->mob_placement.z = z;

#if MOB_ROTATION
    glm::vec3 translate = glm::vec3(           //
        ( float )mobs->mob_placement.x + 0.5f, //
        ( float )mobs->mob_placement.y + 0.5f, //
        ( float )mobs->mob_placement.z + 0.5f );

    glm::mat4 trans_mat = glm::translate( glm::mat4( 1.0f ), translate );
    glm::mat4 trans_inverse_mat = glm::translate( glm::mat4( 1.0f ), glm::vec3( -1.0f ) * translate );

    mobs->mob_placement.rotation = trans_mat * rotation * trans_inverse_mat;
#endif

    vertex_buffer_set_data( &mobs->vb_mob_placement, &mobs->mob_placement, sizeof( BlockCoords ) * 1 );
}

void mobs_draw( Mobs *mobs, Renderer *renderer, Shader *shader ) {
    if ( mobs->shouldDraw ) {
        renderer_draw( renderer, &mobs->va, &mobs->ib, shader, 1 );
    }
}

void mobs_cleanup( Mobs *mobs ) {
}
