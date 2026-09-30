#include <math.h>
#include <cstdlib>

#include "common/RepGame.hpp"
#include "common/sky_box.hpp"
#include "common/renderer/shader.hpp"
#include "common/Particle.hpp"

#define STACK_COUNT 20
#define SLICES_COUNT 20
#define SKY_RADIUS 750.0f

#define SKY_BOX_VERTEX_COUNT ( ( SLICES_COUNT + 2 ) * ( STACK_COUNT + 1 ) )
#define SKY_BOX_TRIANGLES_COUNT ( ( SLICES_COUNT ) * ( STACK_COUNT + 1 ) )
#define SKY_BOX_INDEX_COUNT ( SKY_BOX_TRIANGLES_COUNT * 6 )

MK_TEXTURE( sky4, 2048, 1024, 2048, 1024 );

void SkyBox::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position ) {
    ParticleVertex *vb_data = static_cast<ParticleVertex *>( calloc( SKY_BOX_VERTEX_COUNT, sizeof( ParticleVertex ) ) );
    // Calc The Vertices
    for ( int i = 0; i <= STACK_COUNT; ++i ) {

        float V = i / static_cast<float>( STACK_COUNT );
        float phi = V * glm::pi<float>( );
        float y = -cosf( phi );

        // Loop Through SLICES_COUNT
        for ( int j = 0; j <= SLICES_COUNT + STACK_COUNT; ++j ) {

            float U = ( j / static_cast<float>( SLICES_COUNT ) ); // * 0.75f + 0.05f;
            float theta = U * ( glm::pi<float>( ) * 2 );

            // Calc The Vertex Positions
            const float x = cosf( theta ) * sinf( phi );
            const float z = sinf( theta ) * sinf( phi );

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

    unsigned int *ib_data = static_cast<unsigned int *>( malloc( SKY_BOX_INDEX_COUNT * sizeof( unsigned int ) ) );
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
    free( ib_data );
    free( vb_data );
    auto pair = this->render_chain_sky_box.create_instance( );
    ParticlePosition &sky_instance = pair.second;
    sky_instance = { { }, glm::mat4( 1.0f ) }; // The sky's vertexes are scaled, no need to scale instance.

    this->texture.init( texture_source_sky4, 1 );

    // Pre-compute the average color of the entire sky texture for use as the
    // fog color. This gives a single uniform color that is representative of
    // the sky without being tied to any particular direction.
    // To recompute after changing the sky texture, set SKY_DEBUG_COMPUTE_AVG to 1.
#define SKY_DEBUG_COMPUTE_AVG 0
#if ( defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) ) && SKY_DEBUG_COMPUTE_AVG
    {
        const int sky_w = 2048, sky_h = 1024;
        unsigned char *sky_data = ( unsigned char * )malloc( sky_w * sky_h * 4 );
        this->texture.bind( );
        glGetTexImage( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, sky_data );
        float r = 0, g = 0, b = 0;
        for ( int i = 0; i < sky_w * sky_h; i++ ) {
            r += sky_data[ i * 4 + 0 ];
            g += sky_data[ i * 4 + 1 ];
            b += sky_data[ i * 4 + 2 ];
        }
        float count = sky_w * sky_h;
        m_avgColor = glm::vec3( r / count / 255.0f, g / count / 255.0f, b / count / 255.0f );
        pr_debug( "SkyBox avg color: %f, %f, %f", m_avgColor.r, m_avgColor.g, m_avgColor.b );
        free( sky_data );
    }
#else
    // Hardcoded average of all pixels in the sky texture.
    // Recompute by setting SKY_DEBUG_COMPUTE_AVG to 1 above.
    m_avgColor = glm::vec3( 0.729091f, 0.788694f, 0.851192f );
#endif
}

void SkyBox::draw( const Renderer &renderer, Shader &shader ) {
    shader.set_uniform1i_texture( "u_Texture", this->texture );
    this->render_chain_sky_box.draw( renderer, shader );
}

void SkyBox::destroy( ) {
    this->render_chain_sky_box.clear( );
    this->texture.destroy( );
}