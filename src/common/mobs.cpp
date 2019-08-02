#include "common/mobs.hpp"
#include "common/RepGame.hpp"

static unsigned int ib_data_player[] = {
    0, 1, 2, //
    2, 3, 0, //

    0, 3, 2, //
    2, 1, 0, //

};
#define IB_PLAYER_SIZE ( 3 * 4 )

void mobs_init_mob( ObjectPosition *mob, int id ) {
    mob->face[ FACE_TOP ] = STEVE_HEAD_TOP - 1;
    mob->face[ FACE_BOTTOM ] = STEVE_HEAD_BOTTOM - 1;
    mob->face[ FACE_RIGHT ] = STEVE_HEAD_RIGHT - 1;
    mob->face[ FACE_FRONT ] = STEVE_HEAD_FRONT - 1;
    mob->face[ FACE_LEFT ] = STEVE_HEAD_LEFT - 1;
    mob->face[ FACE_BACK ] = STEVE_HEAD_BACK - 1;
    mob->id = id;
}

void mobs_init( Mobs *mobs, VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement ) {

    {
        index_buffer_init( &mobs->ib );
        vertex_buffer_init( &mobs->vb_mob_shape );
        vertex_buffer_init( &mobs->vb_mob_placement );
        vertex_array_init( &mobs->va );
        vertex_array_add_buffer( &mobs->va, &mobs->vb_mob_shape, vbl_object_vertex, 0, 0 );
        vertex_array_add_buffer( &mobs->va, &mobs->vb_mob_placement, vbl_object_placement, 1, vbl_object_vertex->current_size );
    }
    mobs->shouldDraw = 0;

    {
        vertex_buffer_set_data( &mobs->vb_mob_shape, vd_data_player_object, sizeof( ObjectVertex ) * VB_DATA_SIZE_PLAYER );
        vertex_buffer_set_data( &mobs->vb_mob_placement, mobs->mob_positions.data( ), sizeof( ObjectPosition ) * 1 );
        index_buffer_set_data( &mobs->ib, ib_data_solid, IB_SOLID_SIZE );
    }
    glm::mat4 scale = glm::scale( glm::mat4( 1.0 ), glm::vec3( 0.5f ) );
    glm::mat4 un_translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( -0.5f, -0.5f, -0.5f ) );
    mobs->initial_mat = scale * un_translate;
    for ( int i = 0; i < MAX_MOB_COUNT; i++ ) {
        mobs->mob_index_lookup[ i ] = -1;
    }
}

void mobs_remove_mob( Mobs *mobs, int mob_id ) {
    std::vector<ObjectPosition> &positions = mobs->mob_positions;
    int removed_index = mobs->mob_index_lookup[ mob_id ];
    mobs->mob_index_lookup[ mob_id ] = -1;

    ObjectPosition *swapped_mob = &positions.back( );
    // ObjectPosition *removed_mob = &positions[ removed_index ];
    int swapped_index = positions.size( ) - 1;
    // swapped_mob->id = removed_index;

    std::swap( positions[ removed_index ], positions.back( ) );

    mobs->mob_index_lookup[ swapped_mob->id ] = removed_index;
    positions.pop_back( );
    if ( positions.empty( ) ) {
        mobs->shouldDraw = false;
    }
    pr_debug( "Removed mob:%d", mob_id );
}

void mobs_add_mob( Mobs *mobs, unsigned int mob_id ) {
    if ( mob_id >= MAX_MOB_COUNT ) {
        pr_debug( "Mob id too high, got %d, max:%d", mob_id, MAX_MOB_COUNT );
        return;
    }
    pr_debug( "Added new mob:%d", mob_id );
    std::vector<ObjectPosition> &positions = mobs->mob_positions;
    int next_index = positions.size( );
    mobs->mob_index_lookup[ mob_id ] = next_index;
    positions.emplace_back( );
    mobs_init_mob( &positions.back( ), mob_id );
    mobs->shouldDraw = 1;
}

void mobs_update_position( Mobs *mobs, int mob_id, float x, float y, float z, glm::mat4 &rotation ) {
    int mod_index = mobs->mob_index_lookup[ mob_id ];
    if ( mod_index < 0 ) {
        pr_debug( "Not addign mob" );
        return;
        // pr_debug( "Adding mob" );
        // mobs_add_mob( mobs, mob_id );
        // mod_index = mobs->mob_index_lookup[ mob_id ];
        // if ( mod_index < 0 ) {
        //     pr_debug( "After adding a mob, its index shouln't be negative" );
        // }
    }
    glm::mat4 translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( x, y, z ) );
    ObjectPosition *mob = &mobs->mob_positions[ mod_index ];

    mob->transform = translate * rotation * mobs->initial_mat;
    // TODO only update part of the VB, or only update it once on draw.
    vertex_buffer_set_data( &mobs->vb_mob_placement, mobs->mob_positions.data( ), sizeof( ObjectPosition ) * mobs->mob_positions.size( ) );
}

void mobs_draw( Mobs *mobs, Renderer *renderer, Shader *shader ) {
    if ( mobs->shouldDraw ) {
        renderer_draw( renderer, &mobs->va, &mobs->ib, shader, mobs->mob_positions.size( ) );
    }
    showErrors( );
}

void mobs_cleanup( Mobs *mobs ) {
}
