#ifndef HEADER_REMDER_CHAIN_H
#define HEADER_REMDER_CHAIN_H

#include "common/abstract/textures.hpp"
#include "common/abstract/vertex_buffer_layout.hpp"
#include "common/abstract/vertex_buffer.hpp"
#include "common/abstract/vertex_array.hpp"
#include "common/abstract/index_buffer.hpp"
#include "common/abstract/renderer.hpp"
#include "common/abstract/shader.hpp"

#include "entt/entity/registry.hpp"

#include "common/Platforms.hpp"
#include "common/Logging.hpp"

template <typename Element, typename Instance> class RenderChain {
    IndexBuffer ib;
    VertexArray va;
    VertexBuffer vb_element;
    VertexBuffer vb_instance;
    entt::registry registry;
    bool instance_count_changed = false;

  public:
    void init( VertexBufferLayout *vbl_element, VertexBufferLayout *vbl_instance, //
               const void *element_data, int element_data_count,                  //
               unsigned int *ib_data, unsigned int ib_data_count ) {

        if ( ib_data != NULL ) {
            index_buffer_init( &this->ib );
            index_buffer_set_data( &this->ib, ib_data, ib_data_count );
        }
        vertex_buffer_init( &this->vb_instance );
        vertex_array_init( &this->va );
        vertex_buffer_init( &this->vb_element );
        vertex_array_add_buffer( &this->va, &this->vb_element, vbl_element, 0, 0 );
        this->update_element( element_data, element_data_count );
        if ( vbl_instance != NULL ) {
            vertex_array_add_buffer( &this->va, &this->vb_instance, vbl_instance, 1, vbl_element->current_size );
            vertex_buffer_set_data( &this->vb_instance, NULL, 0 );
        }
        showErrors( );

        registry.on_construct<Instance>( ).template connect<&RenderChain<Element, Instance>::on_construct>( *this );
        registry.on_update<Instance>( ).template connect<&RenderChain<Element, Instance>::on_update>( *this );
        registry.on_destroy<Instance>( ).template connect<&RenderChain<Element, Instance>::on_destroy>( *this );
    }

    void update_element( const void *element_data, int element_data_count ) {
        vertex_buffer_set_data( &this->vb_element, element_data, sizeof( Element ) * element_data_count );
    }

    std::pair<entt::entity, std::reference_wrapper<Instance>> create_instance( ) {
        const entt::entity entity = registry.create( );
        Instance &instance = registry.emplace<Instance>( entity );
        return std::pair<entt::entity, std::reference_wrapper<Instance>>( entity, instance );
    }
    Instance &get_instance( entt::entity entity ) {
        return registry.get<Instance>( entity );
    }
    void invalidate( entt::entity entity ) {
        registry.patch<Instance>( entity );
    }

    void remove( const entt::entity &entity ) {
        registry.destroy( entity );
    }

    void draw( Renderer *renderer, Shader *shader, IndexBuffer *ib ) {
        auto all_instances = registry.group<Instance>( );
        std::size_t instance_count = all_instances.size( );
        if ( instance_count == 0 ) {
            return;
        }
        Instance **available_instances = all_instances.template storage<Instance>( )->raw( );
        if ( available_instances == NULL ) {
            return;
        }
        Instance *instances = *available_instances;
        if ( instances == NULL ) {
            return;
        }
        if ( this->instance_count_changed ) {
            vertex_buffer_set_data( &this->vb_instance, instances, sizeof( Instance ) * instance_count );
            showErrors( );
            this->instance_count_changed = false;
        }
        // pr_debug( "RenderChaing drawing:%ld", instance_count );
        renderer_draw( renderer, &this->va, ib, shader, instance_count );
        showErrors( );
    }

    void draw( Renderer *renderer, Shader *shader ) {
        this->draw( renderer, shader, &this->ib );
    }

    void cleanup( ) {
        registry.clear( );
    }

  private:
    void on_construct( entt::registry &registry, entt::entity entity ) {
        this->instance_count_changed = true;
    }
    void on_update( entt::registry &registry, entt::entity entity ) {
        // pr_debug( "RenderChain::on_update" );
        if ( this->instance_count_changed ) {
            // No need for partial updates, and full update is about to happen.
            return;
        }
        auto all_instances = registry.group<const Instance>( );
        const Instance *const *available_instances = all_instances.template storage<const Instance>( )->raw( );
        if ( available_instances == NULL ) {
            return;
        }
        const Instance *first_instance_positions = *available_instances;
        const Instance *this_instance_position = &registry.get<const Instance>( entity );
        int offset = this_instance_position - first_instance_positions;
        vertex_buffer_set_subdata( &this->vb_instance, this_instance_position, offset * sizeof( Instance ), sizeof( Instance ) );
        showErrors( );
    }
    void on_destroy( entt::registry &registry, entt::entity entity ) {
        this->instance_count_changed = true;
    }
};

#endif