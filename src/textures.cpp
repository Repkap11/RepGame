#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glut.h>
#include "file_utils.h"

#include "RepGame.h"
#include "draw2d.h"
#include "textures.h"

#define BYTEX_PER_PIXEL 4
unsigned int loadTexture( const char *filename, int width, int height, int bmp_header, int tile_size_across, int tile_size_down ) {
    unsigned char *data;
    FILE *file;
    file = fopen( filename, "rb" );
    if ( file == NULL ) {
        pr_debug( "Error rendering texture %s", filename );
        return 0;
    }
    size_t mem_size = width * height * BYTEX_PER_PIXEL + bmp_header;
    data = ( unsigned char * )malloc( mem_size );
    size_t read_size = fread( data, mem_size, 1, file );
    if ( read_size != 1 ) {
        pr_debug( "Texture file wrong size. Expected:%ld", mem_size );
    }
    fclose( file );

    // Image is really in ARGB but we need BGRA
    for ( int i = 0; i < width * height; i++ ) {
        char a = data[ i * BYTEX_PER_PIXEL + 0 + bmp_header ];
        char b = data[ i * BYTEX_PER_PIXEL + 1 + bmp_header ];
        char g = data[ i * BYTEX_PER_PIXEL + 2 + bmp_header ];
        char r = data[ i * BYTEX_PER_PIXEL + 3 + bmp_header ];

        data[ i * BYTEX_PER_PIXEL + 0 + bmp_header ] = r;
        data[ i * BYTEX_PER_PIXEL + 1 + bmp_header ] = g;
        data[ i * BYTEX_PER_PIXEL + 2 + bmp_header ] = b;
        data[ i * BYTEX_PER_PIXEL + 3 + bmp_header ] = a;
    }

    unsigned int textures_across = width / tile_size_across;
    unsigned int textures_down = height / tile_size_down;
    unsigned int layer_count = textures_across * textures_down;
    //*out_num_textures = textures_across * textures_down;
    unsigned int texture;
    glGenTextures( 1, &texture );
    glBindTexture( GL_TEXTURE_2D_ARRAY, texture );
    pr_debug( "About to tex 3d" );
    glTexStorage3D( GL_TEXTURE_2D_ARRAY,              //
                    1,                                // mipLevelCount
                    GL_RGBA8,                         //
                    tile_size_across, tile_size_down, //
                    layer_count );
    pr_debug( "Just to tex 3d" );

    // glPixelStorei( GL_UNPACK_ROW_LENGTH, width );

    unsigned char *working_data = ( unsigned char * )malloc( tile_size_across * tile_size_down * 4 );
    for ( int i = 0; i < layer_count; i++ ) {
        int tex_coord_x = ( i % textures_across );
        int tex_coord_y = ( textures_down - 1 ) - ( i / textures_across );
        int text_coord_base = tex_coord_x * tile_size_across + tex_coord_y * tile_size_down * width;

        for ( int pixel_y = 0; pixel_y < tile_size_down; pixel_y++ ) {
            for ( int pixel_x = 0; pixel_x < tile_size_across; pixel_x++ ) {
                int working_point = pixel_y * tile_size_across + pixel_x;

                int target_point = text_coord_base + pixel_y * width + pixel_x;

                working_data[ BYTEX_PER_PIXEL * working_point + 0 ] = data[ BYTEX_PER_PIXEL * target_point + bmp_header + 0 ];
                working_data[ BYTEX_PER_PIXEL * working_point + 1 ] = data[ BYTEX_PER_PIXEL * target_point + bmp_header + 1 ];
                working_data[ BYTEX_PER_PIXEL * working_point + 2 ] = data[ BYTEX_PER_PIXEL * target_point + bmp_header + 2 ];
                working_data[ BYTEX_PER_PIXEL * working_point + 3 ] = data[ BYTEX_PER_PIXEL * target_point + bmp_header + 3 ];
            }
        }

        glTexSubImage3D( GL_TEXTURE_2D_ARRAY,                 //
                         0,                                   // Mipmap Level
                         0, 0, i,                             // offset
                         tile_size_across, tile_size_down, 1, //
                         GL_RGBA,                             //
                         GL_UNSIGNED_BYTE,                    //
                         working_data );
    }
    free( working_data );

    pr_debug( "Just to tex 3d" );

    // glGenerateMipmap( GL_TEXTURE_2D_ARRAY );

    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR

    // glGenerateMipmap( GL_TEXTURE_2D_ARRAY );

    free( data );
    return texture;
}

void texture_init_blocks( Texture *texture ) {
    char *dir = getRepGamePath( );
    char bufferText[ BUFSIZ ];
    sprintf( bufferText, "%s%s", dir, "/bitmaps/textures.bmp" );
    texture->m_RendererId = loadTexture( bufferText, 256, 256, 138, 16, 16 );
}
void texture_init_sky( Texture *texture ) {
    char *dir = getRepGamePath( );
    char bufferSky[ BUFSIZ ];
    sprintf( bufferSky, "%s%s", dir, "/bitmaps/sky4.bmp" );
    // texture->m_RendererId = loadTexture( bufferSky, 2048, 1024, 138, 2048, 1024 );
    texture->m_RendererId = 0;
}

void texture_bind( Texture *texture, unsigned int texture_slot ) {
    glActiveTexture( GL_TEXTURE0 + texture_slot );
    glBindTexture( GL_TEXTURE_2D_ARRAY, texture->m_RendererId );
}
void texture_unbind( Texture *texture, unsigned int texture_slot ) {
    glActiveTexture( GL_TEXTURE0 + texture_slot );
    glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
}

void texture_destroy( Texture *texture ) {
    glDeleteTextures( 1, &texture->m_RendererId );
}