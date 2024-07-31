#include "common/RepGame.hpp"

struct PlayerId {
    unsigned int player_id;
};

ECS_Renderer::ECS_Renderer( ) {}

ECS_Renderer::ECS_Renderer( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement ) {
    {
        index_buffer_init( &this->ib );
        vertex_buffer_init( &this->vb_particle_shape );
        vertex_buffer_init( &this->vb_particle_placement );
        vertex_array_init( &this->va );
        vertex_array_add_buffer( &this->va, &this->vb_particle_shape, vbl_object_vertex, 0, 0 );
        vertex_array_add_buffer( &this->va, &this->vb_particle_placement, vbl_object_placement, 1, vbl_object_vertex->current_size );
    }

    {
        vertex_buffer_set_data( &this->vb_particle_shape, vd_data_player_object, sizeof( ParticleVertex ) * VB_DATA_SIZE_PLAYER );
        vertex_buffer_set_data( &this->vb_particle_placement, NULL, 0 );
        index_buffer_set_data( &this->ib, ib_data_solid, IB_SOLID_SIZE );
    }
    glm::mat4 scale = glm::scale( glm::mat4( 1.0 ), glm::vec3( 0.5f ) );
    glm::mat4 un_translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( -0.5f, -0.5f, -0.5f ) );
    this->initial_mat = scale * un_translate;
    this->vertex_buffer_dirty = true;
}

void ECS_Renderer::add( unsigned int player_id ) {
    const entt::entity entity = registry.create( );
    registry.emplace<PlayerId>( entity, player_id );
    entity_map.emplace( player_id, entity );
    vertex_buffer_dirty = true;
}
void ECS_Renderer::update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation ) {
    auto it = entity_map.find( particle_id );
    if ( it == entity_map.end( ) ) {
        return;
    }
    entt::entity entity = it->second;
    glm::mat4 translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( x, y, z ) );
    ParticlePosition &particle = registry.get_or_emplace<ParticlePosition>( entity );
    particle.transform = translate * rotation * this->initial_mat;
    vertex_buffer_dirty = true;
}

void ECS_Renderer::remove( unsigned int player_id ) {
    auto it = entity_map.find( player_id );
    if ( it == entity_map.end( ) ) {
        return;
    }
    registry.destroy( it->second );
    vertex_buffer_dirty = true;
}

void ECS_Renderer::draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader ) {

    auto registry_group = registry.group<ParticlePosition>( );
    ParticlePosition *particle_positions = *registry_group.storage<ParticlePosition>( )->raw( );
    std::size_t particle_count = registry_group.size( );

    vertex_buffer_set_data( &this->vb_particle_placement, particle_positions, sizeof( ParticlePosition ) * particle_count );
    shader_set_uniform_mat4f( shader, "u_MVP", mvp );
    renderer_draw( renderer, &this->va, &this->ib, shader, particle_count );
}
void ECS_Renderer::cleanup( ) {
    entity_map.clear( );
    registry.clear( );
}

int ECS_Renderer::check_consistency( ) {
    return true;
}
