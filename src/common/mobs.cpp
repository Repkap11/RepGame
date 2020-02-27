#include "common/mobs.hpp"
#include "common/RepGame.hpp"

static unsigned int ib_data_player[] = {
    0, 1, 2, //
    2, 3, 0, //

    0, 3, 2, //
    2, 1, 0, //

};
#define IB_PLAYER_SIZE ( 3 * 4 )

void Mobs::init_mob( ObjectPosition *mob, int id ) {
    mob->face[ FACE_TOP ] = STEVE_HEAD_TOP - 1;
    mob->face[ FACE_BOTTOM ] = STEVE_HEAD_BOTTOM - 1;
    mob->face[ FACE_RIGHT ] = STEVE_HEAD_RIGHT - 1;
    mob->face[ FACE_FRONT ] = STEVE_HEAD_FRONT - 1;
    mob->face[ FACE_LEFT ] = STEVE_HEAD_LEFT - 1;
    mob->face[ FACE_BACK ] = STEVE_HEAD_BACK - 1;
    mob->id = id;
}

Mobs::Mobs( ) {
}
Mobs::Mobs( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement ) {

    {
        index_buffer_init( &this->ib );
        vertex_buffer_init( &this->vb_mob_shape );
        vertex_buffer_init( &this->vb_mob_placement );
        vertex_array_init( &this->va );
        vertex_array_add_buffer( &this->va, &this->vb_mob_shape, vbl_object_vertex, 0, 0 );
        vertex_array_add_buffer( &this->va, &this->vb_mob_placement, vbl_object_placement, 1, vbl_object_vertex->current_size );
    }
    this->shouldDraw = 0;

    {
        vertex_buffer_set_data( &this->vb_mob_shape, vd_data_player_object, sizeof( ObjectVertex ) * VB_DATA_SIZE_PLAYER );
        vertex_buffer_set_data( &this->vb_mob_placement, this->mob_positions.data( ), sizeof( ObjectPosition ) * 1 );
        index_buffer_set_data( &this->ib, ib_data_solid, IB_SOLID_SIZE );
    }
    glm::mat4 scale = glm::scale( glm::mat4( 1.0 ), glm::vec3( 0.5f ) );
    glm::mat4 un_translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( -0.5f, -0.5f, -0.5f ) );
    this->initial_mat = scale * un_translate;
}

void Mobs::remove_mob( int mob_id ) {
    if ( this->mob_index_lookup.find( mob_id ) == this->mob_index_lookup.end( ) ) {
        pr_debug( "Can't remove an invalid mob_id:%d", mob_id );
        return;
    }
    std::vector<ObjectPosition> &positions = this->mob_positions;
    int removed_index = this->mob_index_lookup.at( mob_id );
    this->mob_index_lookup.erase( mob_id );

    ObjectPosition *swapped_mob = &positions.back( );
    if ( swapped_mob->id != ( unsigned int )mob_id ) {
        this->mob_index_lookup[ swapped_mob->id ] = removed_index;

        // Warning, after this call swapped_mob is now the other mob!!
        std::swap( positions[ removed_index ], positions.back( ) );
    }
    positions.pop_back( );
    if ( positions.empty( ) ) {
        this->shouldDraw = false;
    }
    pr_debug( "Removed mob:%d", mob_id );
}

void Mobs::add_mob( unsigned int mob_id ) {
    pr_debug( "Added new mob:%u", mob_id );
    std::vector<ObjectPosition> &positions = this->mob_positions;
    int next_index = positions.size( );
    this->mob_index_lookup[ mob_id ] = next_index;
    positions.emplace_back( );
    Mobs::init_mob( &positions.back( ), mob_id );
    this->shouldDraw = 1;
}

void Mobs::update_position( int mob_id, float x, float y, float z, const glm::mat4 &rotation ) {
    int mod_index = this->mob_index_lookup[ mob_id ];
    if ( mod_index < 0 ) {
        pr_debug( "Can't update position for non-existent mob %d", mob_id );
        return;
    }
    glm::mat4 translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( x, y, z ) );
    ObjectPosition *mob = &this->mob_positions[ mod_index ];

    mob->transform = translate * rotation * this->initial_mat;
    // TODO only update part of the VB, or only update it once on draw.
    vertex_buffer_set_data( &this->vb_mob_placement, this->mob_positions.data( ), sizeof( ObjectPosition ) * this->mob_positions.size( ) );
}

int Mobs::check_consistency( ) {
    for ( ObjectPosition &position : this->mob_positions ) {
        unsigned int id = position.id;
        float val = position.transform[ 0 ][ 0 ];
        // pr_test( "Position %d" );
    }
    int expected_num_mobs = this->mob_index_lookup.size( );
    int expected_num_mobs2 = this->mob_positions.size( );
    // pr_test( "Num Mobs:%d %d", expected_num_mobs2, expected_num_mobs );

    return expected_num_mobs2 != expected_num_mobs;
}

void Mobs::draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader ) {
    if ( this->shouldDraw ) {
        shader_set_uniform_mat4f( shader, "u_MVP", mvp );
        renderer_draw( renderer, &this->va, &this->ib, shader, this->mob_positions.size( ) );
    }
    showErrors( );
}

void Mobs::cleanup( ) {
}
