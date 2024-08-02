#include "common/RepGame.hpp"

ECS_Renderer::ECS_Renderer( ) {
}

struct ParticleLife {
    long lifetime;
};

struct ECS_Render_VB_Face {};
struct ECS_Render_VB_Particle {};

long current_time = 0;

void ECS_Renderer::init( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement ) {
    {
        index_buffer_init( &this->ib_particle );
        vertex_buffer_init( &this->vb_particle_shape );
        vertex_buffer_init( &this->vb_particle_placement );

        index_buffer_init( &this->ib_face );
        vertex_buffer_init( &this->vb_face_shape );
        vertex_buffer_init( &this->vb_face_placement );

        vertex_array_init( &this->va_particle );
        vertex_array_add_buffer( &this->va_particle, &this->vb_particle_shape, vbl_object_vertex, 0, 0 );
        vertex_array_add_buffer( &this->va_particle, &this->vb_particle_placement, vbl_object_placement, 1, vbl_object_vertex->current_size );

        vertex_array_init( &this->va_face );
        vertex_array_add_buffer( &this->va_face, &this->vb_face_shape, vbl_object_vertex, 0, 0 );
        vertex_array_add_buffer( &this->va_face, &this->vb_face_placement, vbl_object_placement, 1, vbl_object_vertex->current_size );
    }
    showErrors( );
    {
        vertex_buffer_set_data( &this->vb_particle_shape, vd_data_player_object, sizeof( ParticleVertex ) * VB_DATA_SIZE_PARTICLE );
        vertex_buffer_set_data( &this->vb_particle_placement, NULL, 0 );
        index_buffer_set_data( &this->ib_particle, ib_data_solid, IB_SOLID_SIZE );

        vertex_buffer_set_data( &this->vb_face_shape, vd_data_face_object, sizeof( ParticleVertex ) * VB_DATA_SIZE_FACE );
        vertex_buffer_set_data( &this->vb_face_placement, NULL, 0 );
        index_buffer_set_data( &this->ib_face, ib_data_face, IB_FACE_SIZE );
    }
    showErrors( );

    glm::mat4 scale = glm::scale( glm::mat4( 1.0 ), glm::vec3( 0.5f ) );
    glm::mat4 un_translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( -0.5f, -0.5f, -0.5f ) );
    this->initial_mat = scale * un_translate;
    this->vertex_buffer_dirty = true;

    registry.on_construct<ParticlePosition>( ).connect<&ECS_Renderer::on_construct>( *this );
    registry.on_update<ParticlePosition>( ).connect<&ECS_Renderer::on_update>( *this );
    registry.on_destroy<ParticlePosition>( ).connect<&ECS_Renderer::on_destroy>( *this );
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
    bool isFace = registry.all_of<ECS_Render_VB_Face>( entity );
    VertexBuffer *which_vb;
    if ( isFace ) {
        which_vb = &this->vb_face_placement;
    } else {
        which_vb = &this->vb_particle_placement;
    }

    vertex_buffer_set_subdata( which_vb, this_partiel_position, offset * sizeof( ParticlePosition ), sizeof( ParticlePosition ) );
    // pr_debug( "ECS_Renderer::on_update: offset:%d", offset );
};

void ECS_Renderer::on_destroy( entt::registry &registry, entt::entity entity ) {
    pr_debug( "ECS_Renderer::on_destroy" );
    this->vertex_buffer_dirty = true;
}

const std::pair<entt::entity, std::reference_wrapper<ParticlePosition>> ECS_Renderer::create_particle( long lifetime ) {
    const entt::entity entity = registry.create( );
    ParticlePosition &particle = registry.emplace<ParticlePosition>( entity );
    registry.emplace<ECS_Render_VB_Particle>( entity );

    if ( lifetime > 0 ) {
        registry.emplace<ParticleLife>( entity, current_time + lifetime );
    }
    return std::pair<entt::entity, std::reference_wrapper<ParticlePosition>>( entity, particle );
}

const std::pair<entt::entity, std::reference_wrapper<ParticlePosition>> ECS_Renderer::create_face( long lifetime ) {
    const entt::entity entity = registry.create( );
    ParticlePosition &particle = registry.emplace<ParticlePosition>( entity );
    registry.emplace<ECS_Render_VB_Face>( entity );
    if ( lifetime > 0 ) {
        registry.emplace<ParticleLife>( entity, current_time + lifetime );
    }
    return std::pair<entt::entity, std::reference_wrapper<ParticlePosition>>( entity, particle );
}

void ECS_Renderer::update_position( const entt::entity &entity, float x, float y, float z, const glm::mat4 &rotation ) {
    glm::mat4 translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( x, y, z ) );
    ParticlePosition &particle = registry.get<ParticlePosition>( entity );
    particle.transform = translate * rotation * this->initial_mat;
    registry.patch<ParticlePosition>( entity );
}

void ECS_Renderer::remove( const entt::entity &entity ) {
    registry.destroy( entity );
}

void ECS_Renderer::draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader ) {
    current_time = current_time + 1l;
    auto timed_particles = registry.view<ParticleLife>( );
    auto all_particles = registry.group<ECS_Render_VB_Particle>( entt::get<ParticlePosition> );
    auto all_faces = registry.group<ECS_Render_VB_Face>( entt::get<ParticlePosition> );

    // timed_particles.sort<ParticleLife>( []( const ParticleLife &lhs, const ParticleLife &rhs ) { return lhs.lifetime > rhs.lifetime; } );
    for ( auto [ entity, life ] : timed_particles.each( ) ) {
        if ( life.lifetime <= current_time ) {
            pr_debug( "Deleting particle: current_time:%ld lifetime:%ld", current_time, life.lifetime );
            registry.destroy( entity );
            break;
        }
    }

    shader_set_uniform_mat4f( shader, "u_MVP", mvp );

    // pr_debug( "Got to 1" );

    ParticlePosition **available_particles = all_particles.storage<ParticlePosition>( )->raw( );
    if ( available_particles != NULL ) {
        ParticlePosition *particle_positions = *available_particles;
        std::size_t particle_count = all_particles.size( );
        if ( particle_count != 0 ) {
            if ( particle_positions != NULL ) {
                // pr_debug( "Got to 2" );
                if ( this->vertex_buffer_dirty ) {
                    vertex_buffer_set_data( &this->vb_particle_placement, particle_positions, sizeof( ParticlePosition ) * particle_count );
                    // pr_debug( "draw: Clearing vertex_buffer_ditry" );
                } else {
                    // pr_debug( "draw: Not vertex_buffer_ditry" );
                    // vertex_buffer_bind( &this->vb_particle_placement );
                }
                // pr_debug( "Got to 3" );

                renderer_draw( renderer, &this->va_particle, &this->ib_particle, shader, particle_count );
                showErrors( );
            }
        }
    }
    // pr_debug( "Got to 4" );

    ParticlePosition **available_faces = all_faces.storage<ParticlePosition>( )->raw( );
    if ( available_faces != NULL ) {
        ParticlePosition *face_positions = *available_faces;
        std::size_t face_count = all_faces.size( );
        if ( face_count != 0 ) {
            if ( face_positions != NULL ) {
                // pr_debug( "Got to 5" );
                if ( this->vertex_buffer_dirty ) {
                    vertex_buffer_set_data( &this->vb_face_placement, face_positions, sizeof( ParticlePosition ) * face_count );
                    // pr_debug( "draw: Clearing vertex_buffer_ditry" );
                } else {
                    // pr_debug( "draw: Not vertex_buffer_ditry" );
                    // vertex_buffer_bind( &this->vb_face_placement );
                }
                // pr_debug( "Got to 6" );
                renderer_draw( renderer, &this->va_face, &this->ib_face, shader, face_count );
                showErrors( );
            }
        }
    }
    vertex_buffer_dirty = false;
}
void ECS_Renderer::cleanup( ) {
    registry.clear( );
}