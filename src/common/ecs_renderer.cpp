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

    // registry.on_construct<ParticlePosition>( ).connect<&ECS_Renderer::on_construct>( *this );
    // registry.on_update<ParticlePosition>( ).connect<&ECS_Renderer::on_update>( *this );
    // registry.on_destroy<ParticlePosition>( ).connect<&ECS_Renderer::on_destroy>( *this );
}

void ECS_Renderer::on_construct( entt::registry &registry, entt::entity entity ) {
    pr_debug( "ECS_Renderer::on_construct" );
    this->vertex_buffer_dirty = true;
}

void ECS_Renderer::on_update( entt::registry &registry, entt::entity entity ) {
    // vertex_buffer_dirty = true;
    if ( this->vertex_buffer_dirty ) {
        pr_debug( "ECS_Renderer::nothing to do" );
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
    // const PlayerId &player_id = registry.get<const PlayerId>( entity );

    vertex_buffer_set_subdata( &this->vb_particle_placement, this_partiel_position, offset * sizeof( ParticlePosition ), sizeof( ParticlePosition ) );
    pr_debug( "ECS_Renderer::on_update: offset:%d", offset );
};

void ECS_Renderer::on_destroy( entt::registry &registry, entt::entity entity ) {
    pr_debug( "ECS_Renderer::on_destroy" );
    this->vertex_buffer_dirty = true;
}

void ECS_Renderer::add( unsigned int particle_id ) {
    pr_debug( "Adding particle_id:%d", particle_id );
    this->vertex_buffer_dirty = true;
    const entt::entity entity = registry.create( );
    entity_map.emplace( particle_id, entity );
    registry.emplace<PlayerId>( entity, particle_id );
    ParticlePosition particle;
    init_particle( &particle, particle_id );
    registry.emplace<ParticlePosition>( entity, particle );
}
void ECS_Renderer::update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation ) {
    pr_debug( "Updating particle_id:%d", particle_id );

    auto it = entity_map.find( particle_id );
    if ( it == entity_map.end( ) ) {
        pr_debug( "No player with that ID could be found." );
        return;
    }
    entt::entity entity = it->second;
    glm::mat4 translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( x, y, z ) );
    ParticlePosition &particle = registry.get<ParticlePosition>( entity );

    particle.transform = translate * rotation * this->initial_mat;
    // registry.patch<ParticlePosition>( entity );
    on_update( registry, entity );
}

void ECS_Renderer::remove( unsigned int particle_id ) {
    pr_debug( "Removing particle_id:%d", particle_id );
    this->vertex_buffer_dirty = true;
    auto it = entity_map.find( particle_id );
    if ( it == entity_map.end( ) ) {
        // No player with that ID could be found.
        return;
    }
    registry.destroy( it->second );
    entity_map.erase( it );
}

void ECS_Renderer::draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader ) {
    // pr_debug( "draw:%p", this );

    auto registry_group = registry.group<ParticlePosition>( );
    ParticlePosition **available_particles = registry_group.storage<ParticlePosition>( )->raw( );
    if ( available_particles == NULL ) {
        pr_debug( "draw: No particles to draw." );
        return;
    }
    ParticlePosition *particle_positions = *available_particles;
    std::size_t particle_count = registry_group.size( );
    if ( particle_count == 0 ) {
        pr_debug( "draw: No particles to draw" );
        return;
    }
    if ( particle_positions == NULL ) {
        pr_debug( "draw: Particle null" );
        return;
    }

    if ( this->vertex_buffer_dirty ) {
        vertex_buffer_set_data( &this->vb_particle_placement, particle_positions, sizeof( ParticlePosition ) * particle_count );
        vertex_buffer_dirty = false;
        pr_debug( "draw: Clearing vertex_buffer_ditry" );
    } else {
        pr_debug( "draw: Not vertex_buffer_ditry" );
        vertex_buffer_bind( &this->vb_particle_placement );
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
