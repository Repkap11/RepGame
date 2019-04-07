#include <math.h>

#include "common/RepGame.hpp"
#include "common/sky_box.hpp"
#include "common/abstract/shader.hpp"

#define Stacks 20
#define Slices 20
#define Radius 750.0f

#define SKY_BOX_VERTEX_COUNT ( ( Slices + 2 ) * ( Stacks + 1 ) )
#define SKY_BOX_TRIANGLES_COUNT ( ( Slices ) * ( Stacks + 1 ) )
#define SKY_BOX_INDEX_COUNT ( SKY_BOX_TRIANGLES_COUNT * 6 )

MK_SHADER( sky_box_vertex );
MK_SHADER( sky_box_fragment );

void sky_box_init( SkyBox *skyBox ) {
    SkyVertex *vb_data = ( SkyVertex * )calloc( SKY_BOX_VERTEX_COUNT, sizeof( SkyVertex ) );
    // Calc The Vertices
    for ( int i = 0; i <= Stacks; ++i ) {

        float V = i / ( float )Stacks;
        float phi = V * glm::pi<float>( );
        float y = -cosf( phi );

        // Loop Through Slices
        for ( int j = 0; j <= Slices + Stacks; ++j ) {

            float U = ( j / ( ( float )Slices ) ); // * 0.75f + 0.05f;
            float theta = U * ( glm::pi<float>( ) * 2 );

            // Calc The Vertex Positions
            float x = cosf( theta ) * sinf( phi );
            float z = sinf( theta ) * sinf( phi );

            // Push Back Vertex Data
            SkyVertex *vertex = &vb_data[ i * ( Slices + 1 ) + j ];
            vertex->x = x * Radius;
            vertex->y = y * Radius;
            vertex->z = z * Radius;
            vertex->u = U;
            vertex->v = V;
        }
    }

    unsigned int *ib_data = ( unsigned int * )malloc( SKY_BOX_INDEX_COUNT * sizeof( unsigned int ) );
    index_buffer_init( &skyBox->ib );
    // Calc The Index Positions
    for ( int i = 0; i < SKY_BOX_TRIANGLES_COUNT; ++i ) {

        ib_data[ i * 6 + 0 ] = i;
        ib_data[ i * 6 + 1 ] = i + Slices + 1;
        ib_data[ i * 6 + 2 ] = i + Slices;

        ib_data[ i * 6 + 3 ] = i + Slices + 1;
        ib_data[ i * 6 + 4 ] = i;
        ib_data[ i * 6 + 5 ] = i + 1;
        if ( i * 6 + 5 > SKY_BOX_INDEX_COUNT ) {
            pr_debug( "Out of bounds" );
        }
    }
    index_buffer_set_data( &skyBox->ib, ib_data, SKY_BOX_INDEX_COUNT );

    // These are from SkyVertex
    vertex_buffer_layout_init( &skyBox->vbl );
    vertex_buffer_layout_push_float( &skyBox->vbl, 3 ); // Coords
    vertex_buffer_layout_push_float( &skyBox->vbl, 2 ); // TxCoords

    skyBox->vertex_size = SKY_BOX_VERTEX_COUNT;

    vertex_buffer_init( &skyBox->vb );
    vertex_buffer_set_data( &skyBox->vb, vb_data, sizeof( SkyVertex ) * skyBox->vertex_size );

    vertex_array_init( &skyBox->va );

    vertex_array_add_buffer( &skyBox->va, &skyBox->vb, &skyBox->vbl, 0, 0 );
    shader_init( &skyBox->shader, &sky_box_vertex, &sky_box_fragment );
    texture_init_sky( &skyBox->texture );
    free( ib_data );
    free( vb_data );
}

void sky_box_draw( SkyBox *skyBox, Renderer *renderer, glm::mat4 &mvp_sky ) {
    shader_set_uniform1i( &skyBox->shader, "u_Texture", skyBox->texture.slot );
    shader_set_uniform_mat4f( &skyBox->shader, "u_MVP", mvp_sky );
    renderer_draw( renderer, &skyBox->va, &skyBox->ib, &skyBox->shader, 1 );
}

void sky_box_destroy( SkyBox *skyBox ) {
    vertex_buffer_layout_destroy( &skyBox->vbl );
    index_buffer_destroy( &skyBox->ib );
    vertex_array_destroy( &skyBox->va );
    vertex_buffer_destroy( &skyBox->vb );
    shader_destroy( &skyBox->shader );
    texture_destroy( &skyBox->texture );
}