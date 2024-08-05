#include <math.h>

#include "common/RepGame.hpp"
#include "common/sky_box.hpp"
#include "common/abstract/shader.hpp"
#include "common/Particle.hpp"

#define STACK_COUNT 20
#define SLICES_COUNT 20
#define SKY_RADIUS 750.0f

#define SKY_BOX_VERTEX_COUNT ( ( SLICES_COUNT + 2 ) * ( STACK_COUNT + 1 ) )
#define SKY_BOX_TRIANGLES_COUNT ( ( SLICES_COUNT ) * ( STACK_COUNT + 1 ) )
#define SKY_BOX_INDEX_COUNT ( SKY_BOX_TRIANGLES_COUNT * 6 )

MK_TEXTURE( sky4, 2048, 1024, 2048, 1024 );

void SkyBox::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position ) {
    ParticleVertex *vb_data = ( ParticleVertex * )calloc( SKY_BOX_VERTEX_COUNT, sizeof( ParticleVertex ) );
    // Calc The Vertices
    for ( int i = 0; i <= STACK_COUNT; ++i ) {

        float V = i / ( float )STACK_COUNT;
        float phi = V * glm::pi<float>( );
        float y = -cosf( phi );

        // Loop Through SLICES_COUNT
        for ( int j = 0; j <= SLICES_COUNT + STACK_COUNT; ++j ) {

            float U = ( j / ( ( float )SLICES_COUNT ) ); // * 0.75f + 0.05f;
            float theta = U * ( glm::pi<float>( ) * 2 );

            // Calc The Vertex Positions
            float x = cosf( theta ) * sinf( phi );
            float z = sinf( theta ) * sinf( phi );

            // Push Back Vertex Data
            ParticleVertex *vertex = &vb_data[ i * ( SLICES_COUNT + 1 ) + j ];
            vertex->x = x * SKY_RADIUS;
            vertex->y = y * SKY_RADIUS;
            vertex->z = z * SKY_RADIUS;
            vertex->u = U;
            vertex->v = V;
            vertex->which_face = FACE_TOP; // Top isn't affected by light
        }
    }

    unsigned int *ib_data = ( unsigned int * )malloc( SKY_BOX_INDEX_COUNT * sizeof( unsigned int ) );
    // Calc The Index Positions
    for ( int i = 0; i < SKY_BOX_TRIANGLES_COUNT; ++i ) {

        ib_data[ i * 6 + 0 ] = i;
        ib_data[ i * 6 + 1 ] = i + SLICES_COUNT + 1;
        ib_data[ i * 6 + 2 ] = i + SLICES_COUNT;

        ib_data[ i * 6 + 3 ] = i + SLICES_COUNT + 1;
        ib_data[ i * 6 + 4 ] = i;
        ib_data[ i * 6 + 5 ] = i + 1;
        if ( i * 6 + 5 > SKY_BOX_INDEX_COUNT ) {
            pr_debug( "Out of bounds" );
        }
    }

    this->render_chain_sky_box.init( vbl_object_vertex, vbl_object_position, vb_data, SKY_BOX_VERTEX_COUNT, ib_data, SKY_BOX_INDEX_COUNT );
    auto pair = this->render_chain_sky_box.create_instance( );
    ParticlePosition &sky_instance = pair.second;
    sky_instance = { { }, glm::mat4( 1.0f ) }; // The sky's vertexes are scaled, no need to scale instance.

    this->texture.init( texture_source_sky4, 1 );
}

void SkyBox::draw( const Renderer &renderer, Shader &shader ) {
    shader.set_uniform1i_texture( "u_Texture", this->texture );
    this->render_chain_sky_box.draw( renderer, shader );
}

void SkyBox::destroy( ) {
    this->render_chain_sky_box.clear( );
    this->texture.destroy( );
}