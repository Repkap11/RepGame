#pragma once

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
        return registry.storage<Instance>( ).size( );
    }

    void draw( const Renderer &renderer, const Shader &shader, const IndexBuffer &ib ) {
        auto &pool = registry.storage<Instance>( );
        const std::size_t instance_count = pool.size( );
        if ( instance_count == 0 ) {
            return;
        }
        if ( this->instance_count_changed ) {
            // basic_storage is paginated (ENTT_PACKED_PAGE=1024): raw() is the
            // page table, so size the buffer then upload each contiguous page
            // in place — no staging copy, still one draw call.
            constexpr std::size_t page_size = entt::component_traits<Instance>::page_size;
            static_assert( page_size > 0u, "RenderChain expects paginated component storage" );
            Instance *const *pages = pool.raw( );
            this->vb_instance.reserve( static_cast<unsigned int>( sizeof( Instance ) * instance_count ) );
            for ( std::size_t base = 0; base < instance_count; base += page_size ) {
                const std::size_t remaining = instance_count - base;
                const std::size_t count = remaining < page_size ? remaining : page_size;
                this->vb_instance.set_subdata( pages[ base / page_size ],
                                               static_cast<unsigned int>( base * sizeof( Instance ) ),
                                               static_cast<unsigned int>( count * sizeof( Instance ) ) );
            }
            showErrors( );
            this->instance_count_changed = false;
        }
        // pr_debug( "RenderChaing drawing:%ld", instance_count );
        renderer.draw( this->va, ib, shader, instance_count );
        showErrors( );
    }

    inline void draw( const Renderer &renderer, const Shader &shader ) {
        this->draw( renderer, shader, this->ib );
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
        auto &pool = registry.storage<Instance>( );
        // The buffer is indexed by packed position — a raw()-base pointer diff
        // is garbage for instances on page 1+ (paginated storage), which made
        // glBufferSubData write out of range once a chain passed 1024.
        const std::size_t offset = pool.index( entity );
        this->vb_instance.set_subdata( &pool.get( entity ), offset * sizeof( Instance ), sizeof( Instance ) );
        showErrors( );
    }
    void on_destroy( entt::registry &registry, entt::entity entity ) {
        this->instance_count_changed = true;
    }
};