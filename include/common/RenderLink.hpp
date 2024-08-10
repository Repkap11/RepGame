#ifndef HEADER_RENDER_LINK_H
#define HEADER_RENDER_LINK_H

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

template <typename Element> class RenderLink {
    IndexBuffer ib;
    VertexArray va;
    VertexBuffer vb_element;

  public:
    void init( const VertexBufferLayout &vbl_element,                       //
               const Element *element_data, int element_data_count, //
               const unsigned int *ib_data, unsigned int ib_data_count ) {

        if ( ib_data != NULL ) {
            this->ib.init( );
            this->ib.set_data( ib_data, ib_data_count );
        }
        this->va.init( );
        this->vb_element.init( );
        this->va.add_buffer( this->vb_element, vbl_element);
        this->update_element( element_data, element_data_count );
        showErrors( );
    }

    void update_element( const void *element_data, int element_data_count ) {
        this->vb_element.set_data( element_data, sizeof( Element ) * element_data_count );
    }

    void draw( const Renderer &renderer, const Shader &shader, const IndexBuffer &ib ) {
        renderer.draw( this->va, ib, shader, 1 );
        showErrors( );
    }

    void draw( const Renderer &renderer, const Shader &shader ) {
        this->draw( renderer, shader, this->ib );
    }
};

#endif