#include "common/RepGame.hpp"

struct PlayerId {
    unsigned int player_id;
};

ECS_Renderer::ECS_Renderer( ) {
}

ECS_Renderer::ECS_Renderer( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement ) {
    {
        index_buffer_init( &this->ib );
        vertex_buffer_init( &this->vb_particle_shape );
        vertex_buffer_init( &this->vb_particle_placement );
        vertex_array_init( &this->va );
        vertex_array_add_buffer( &this->va, &this->vb_particle_shape, vbl_object_vertex, 0, 0 );
        vertex_array_add_buffer( &this->va, &this->vb_particle_placement, vbl_object_placement, 1, vbl_object_vertex->current_size );
    }
    showErrors( );

    {
        vertex_buffer_set_data( &this->vb_particle_shape, vd_data_player_object, sizeof( ParticleVertex ) * VB_DATA_SIZE_PLAYER );
        vertex_buffer_set_data( &this->vb_particle_placement, NULL, 0 );
        index_buffer_set_data( &this->ib, ib_data_solid, IB_SOLID_SIZE );
    }
    showErrors( );

    glm::mat4 scale = glm::scale( glm::mat4( 1.0 ), glm::vec3( 0.5f ) );
    glm::mat4 un_translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( -0.5f, -0.5f, -0.5f ) );
    this->initial_mat = scale * un_translate;
    this->vertex_buffer_dirty = true;

    registry.on_construct<ParticlePosition>( ).connect<&ECS_Renderer::on_construct>( this );
    registry.on_update<ParticlePosition>( ).connect<&ECS_Renderer::on_update>( this );
    registry.on_destroy<ParticlePosition>( ).connect<&ECS_Renderer::on_destroy>( this );
}

void ECS_Renderer::on_construct( entt::registry &registry, entt::entity entity ) {
    pr_debug( "ECS_Renderer::on_construct" );
}

void ECS_Renderer::on_update( entt::registry &registry, entt::entity entity ) {
    if ( vertex_buffer_dirty ) {
        // No need for partial updates, and full update is about to happen.`
        return;
    }
    auto registry_group = registry.group<const ParticlePosition>( );
    const ParticlePosition *const *available_particles = registry_group.storage<const ParticlePosition>( )->raw( );
    if ( available_particles == NULL ) {
        pr_debug( "Somehow there is nothing???" );
        return;
    }
    const ParticlePosition *first_particle_positions = *available_particles;
    const ParticlePosition *this_partiel_position = &registry.get<const ParticlePosition>( entity );
    int offset = this_partiel_position - first_particle_positions;
    vertex_buffer_set_subdata( &this->vb_particle_placement, this_partiel_position, offset * sizeof( ParticlePosition ), sizeof( ParticlePosition ) );

    pr_debug( "ECS_Renderer::on_update: offset:%d", offset );
};

void ECS_Renderer::on_destroy( entt::registry &registry, entt::entity entity ) {
    pr_debug( "ECS_Renderer::on_destroy" );
}

void ECS_Renderer::add( unsigned int particle_id ) {
    const entt::entity entity = registry.create( );
    registry.emplace<PlayerId>( entity, particle_id );
    ParticlePosition &particle = registry.emplace<ParticlePosition>( entity );
    init_particle( &particle, particle_id );
    entity_map.emplace( particle_id, entity );
    vertex_buffer_dirty = true;
}
void ECS_Renderer::update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation ) {
    auto it = entity_map.find( particle_id );
    if ( it == entity_map.end( ) ) {
        pr_debug( "No player with that ID could be found." );
        return;
    }
    entt::entity entity = it->second;
    glm::mat4 translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( x, y, z ) );
    ParticlePosition &particle = registry.get<ParticlePosition>( entity );
    registry.patch<ParticlePosition>( entity );
    particle.transform = translate * rotation * this->initial_mat;
}

void ECS_Renderer::remove( unsigned int player_id ) {
    auto it = entity_map.find( player_id );
    if ( it == entity_map.end( ) ) {
        // No player with that ID could be found.
        return;
    }
    registry.destroy( it->second );
    entity_map.erase( it );
    vertex_buffer_dirty = true;
}

void ECS_Renderer::draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader ) {

    auto registry_group = registry.group<ParticlePosition>( );
    ParticlePosition **available_particles = registry_group.storage<ParticlePosition>( )->raw( );
    if ( available_particles == NULL ) {
        // pr_debug( "No particles to draw." );
        return;
    }
    ParticlePosition *particle_positions = *available_particles;
    std::size_t particle_count = registry_group.size( );
    if ( particle_count == 0 ) {
        // I don't think this can happen, since it would be null before.
        return;
    }

    if ( vertex_buffer_dirty ) {
        vertex_buffer_set_data( &this->vb_particle_placement, particle_positions, sizeof( ParticlePosition ) * particle_count );
        vertex_buffer_dirty = false;
    }
    shader_set_uniform_mat4f( shader, "u_MVP", mvp );
    renderer_draw( renderer, &this->va, &this->ib, shader, particle_count );
    showErrors( );
}
void ECS_Renderer::cleanup( ) {
    entity_map.clear( );
    registry.clear( );
}

int ECS_Renderer::check_consistency( ) {
    return true;
}
