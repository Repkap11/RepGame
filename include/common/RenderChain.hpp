#ifndef HEADER_RENDER_CHAIN_H
#define HEADER_RENDER_CHAIN_H

#include "common/renderer/texture.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/renderer/vertex_buffer.hpp"
#include "common/renderer/vertex_array.hpp"
#include "common/renderer/index_buffer.hpp"
#include "common/renderer/renderer.hpp"
#include "common/renderer/shader.hpp"

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
    void init( const VertexBufferLayout &vbl_element, const VertexBufferLayout &vbl_instance, //
               const Element *element_data, int element_data_count,                           //
               const unsigned int *ib_data, unsigned int ib_data_count ) {

        if ( ib_data != NULL ) {
            this->ib.init( );
            this->ib.set_data( ib_data, ib_data_count );
        }
        this->va.init( );
        this->vb_element.init( );
        this->vb_instance.init( );
        this->va.add_buffer( this->vb_element, vbl_element );
        this->update_element( element_data, element_data_count );

        this->va.add_buffer( this->vb_instance, vbl_instance );
        this->vb_instance.set_data( NULL, 0 );
        showErrors( );

        registry.on_construct<Instance>( ).template connect<&RenderChain<Element, Instance>::on_construct>( *this );
        registry.on_update<Instance>( ).template connect<&RenderChain<Element, Instance>::on_update>( *this );
        registry.on_destroy<Instance>( ).template connect<&RenderChain<Element, Instance>::on_destroy>( *this );
    }

    void update_element( const void *element_data, int element_data_count ) {
        this->vb_element.set_data( element_data, sizeof( Element ) * element_data_count );
    }

    std::pair<entt::entity, Instance &> create_instance( ) {
        const entt::entity entity = registry.create( );
        Instance &instance = registry.emplace<Instance>( entity );
        return std::pair<entt::entity, Instance &>( entity, instance );
    }
    Instance &get_instance( const entt::entity &entity ) {
        return registry.get<Instance>( entity );
    }
    void invalidate( const entt::entity &entity ) {
        registry.patch<Instance>( entity );
    }

    void remove( const entt::entity &entity ) {
        registry.destroy( entity );
    }

    std::size_t length( ) {
        return registry.group<Instance>( ).size( );
    }

    void draw( const Renderer &renderer, const Shader &shader, const IndexBuffer &ib, bool drawLines ) {
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
            this->vb_instance.set_data( instances, sizeof( Instance ) * instance_count );
            showErrors( );
            this->instance_count_changed = false;
        }
        // pr_debug( "RenderChaing drawing:%ld", instance_count );
        if ( drawLines ) {
            renderer.draw_lines( this->va, ib, shader, instance_count );
        } else {
            renderer.draw( this->va, ib, shader, instance_count );
        }
        showErrors( );
    }

    inline void draw( const Renderer &renderer, const Shader &shader, const IndexBuffer &ib ) {
        this->draw( renderer, shader, ib, false );
    }
    inline void draw_lines( const Renderer &renderer, const Shader &shader, const IndexBuffer &ib ) {
        this->draw( renderer, shader, ib, true );
    }

    inline void draw( const Renderer &renderer, const Shader &shader ) {
        this->draw( renderer, shader, this->ib, false );
    }

    inline void draw_lines( const Renderer &renderer, const Shader &shader ) {
        this->draw( renderer, shader, this->ib, true );
    }

    void clear( ) {
        registry.clear<Instance>( );
        this->instance_count_changed = true;
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
        this->vb_instance.set_subdata( this_instance_position, offset * sizeof( Instance ), sizeof( Instance ) );
        showErrors( );
    }
    void on_destroy( entt::registry &registry, entt::entity entity ) {
        this->instance_count_changed = true;
    }
};

#endif