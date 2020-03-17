#include "common/ParticleCollection.hpp"
#include "common/RepGame.hpp"

ParticleCollection::ParticleCollection( ) {
}
ParticleCollection::ParticleCollection( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement ) {

    {
        index_buffer_init( &this->ib );
        vertex_buffer_init( &this->vb_particle_shape );
        vertex_buffer_init( &this->vb_particle_placement );
        vertex_array_init( &this->va );
        vertex_array_add_buffer( &this->va, &this->vb_particle_shape, vbl_object_vertex, 0, 0 );
        vertex_array_add_buffer( &this->va, &this->vb_particle_placement, vbl_object_placement, 1, vbl_object_vertex->current_size );
    }
    this->shouldDraw = 0;

    {
        vertex_buffer_set_data( &this->vb_particle_shape, vd_data_player_object, sizeof( ParticleVertex ) * VB_DATA_SIZE_PLAYER );
        vertex_buffer_set_data( &this->vb_particle_placement, this->particle_positions.data( ), sizeof( ParticlePosition ) * 1 );
        index_buffer_set_data( &this->ib, ib_data_solid, IB_SOLID_SIZE );
    }
    glm::mat4 scale = glm::scale( glm::mat4( 1.0 ), glm::vec3( 0.5f ) );
    glm::mat4 un_translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( -0.5f, -0.5f, -0.5f ) );
    this->initial_mat = scale * un_translate;
}

void ParticleCollection::remove( int particle_id ) {
    if ( this->particle_index_lookup.find( particle_id ) == this->particle_index_lookup.end( ) ) {
        pr_debug( "Can't remove an invalid particle_id:%d", particle_id );
        return;
    }
    std::vector<ParticlePosition> &positions = this->particle_positions;
    int removed_index = this->particle_index_lookup.at( particle_id );
    this->particle_index_lookup.erase( particle_id );

    ParticlePosition *swapped_particle = &positions.back( );
    if ( swapped_particle->id != ( unsigned int )particle_id ) {
        this->particle_index_lookup[ swapped_particle->id ] = removed_index;

        // Warning, after this call swapped_particle is now the other particle!!
        std::swap( positions[ removed_index ], positions.back( ) );
    }
    positions.pop_back( );
    if ( positions.empty( ) ) {
        this->shouldDraw = false;
    }
    pr_debug( "Removed particle:%d", particle_id );
}

void ParticleCollection::add( unsigned int particle_id ) {
    pr_debug( "Added new particle:%u", particle_id );
    std::vector<ParticlePosition> &positions = this->particle_positions;
    int next_index = positions.size( );
    this->particle_index_lookup[ particle_id ] = next_index;
    positions.emplace_back( );

    ParticlePosition *particle = &positions.back( );
    this->init_particle( &positions.back( ), particle_id );
    particle->id = particle_id;
    this->shouldDraw = 1;
}

void ParticleCollection::update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation ) {
    int mod_index = this->particle_index_lookup[ particle_id ];
    if ( mod_index < 0 ) {
        pr_debug( "Can't update position for non-existent particle %d", particle_id );
        return;
    }
    glm::mat4 translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( x, y, z ) );
    ParticlePosition *particle = &this->particle_positions[ mod_index ];

    particle->transform = translate * rotation * this->initial_mat;
    // TODO only update part of the VB, or only update it once on draw.
    vertex_buffer_set_data( &this->vb_particle_placement, this->particle_positions.data( ), sizeof( ParticlePosition ) * this->particle_positions.size( ) );
}

int ParticleCollection::check_consistency( ) {
    for ( ParticlePosition &position : this->particle_positions ) {
        unsigned int id = position.id;
        float val = position.transform[ 0 ][ 0 ];
        // pr_test( "Position %d" );
    }
    int expected_num_Particle = this->particle_index_lookup.size( );
    int expected_num_Particle2 = this->particle_positions.size( );
    // pr_test( "Num Particle:%d %d", expected_num_Particle2, expected_num_Particle );

    return expected_num_Particle2 != expected_num_Particle;
}

void ParticleCollection::draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader ) {
    if ( this->shouldDraw ) {
        shader_set_uniform_mat4f( shader, "u_MVP", mvp );
        renderer_draw( renderer, &this->va, &this->ib, shader, this->particle_positions.size( ) );
    }
    showErrors( );
}

void ParticleCollection::cleanup( ) {
}
