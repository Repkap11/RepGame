#include <GL/gl.h>
#include <GL/glut.h>

#include "draw2d.h"
#include "textures.h"

#define BYTEX_PER_PIXEL 4
GLuint *loadTextures( const char *filename, int width, int height, int bmp_header, int tile_size_across, int tile_size_down, int *out_num_extures ) {
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
        char r = data[ i * BYTEX_PER_PIXEL + 1 + bmp_header ];
        char g = data[ i * BYTEX_PER_PIXEL + 2 + bmp_header ];
        char b = data[ i * BYTEX_PER_PIXEL + 3 + bmp_header ];

        data[ i * BYTEX_PER_PIXEL + 0 + bmp_header ] = r;
        data[ i * BYTEX_PER_PIXEL + 1 + bmp_header ] = g;
        data[ i * BYTEX_PER_PIXEL + 2 + bmp_header ] = b;
        data[ i * BYTEX_PER_PIXEL + 3 + bmp_header ] = a;
    }

    int textures_across = width / tile_size_across;
    int textures_down = height / tile_size_down;
    *out_num_extures = textures_across * textures_down;
    GLuint *textures = ( GLuint * )malloc( ( *out_num_extures ) * sizeof( GLuint ) );
    glGenTextures( *out_num_extures, textures );

    unsigned char *working_data = ( unsigned char * )malloc( tile_size_across * tile_size_down * 4 );
    for ( int i = 0; i < textures_across * textures_down; i++ ) {
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

        glBindTexture( GL_TEXTURE_2D, textures[ i ] );

        glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

        // float fLargest;
        // glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest );
        // glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);

        glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
        glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
        glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
        gluBuild2DMipmaps( GL_TEXTURE_2D, 3, tile_size_across, tile_size_down, GL_BGRA, GL_UNSIGNED_BYTE, working_data );
    }
    free( working_data );
    free( data );
    return textures;
}

GLuint *textures, *sky_textures;
int num_textures, num_skys;
void textures_populate( ) {
    sky_textures = loadTextures( "./bitmaps/sky4.bmp", 2048, 1024, 138, 2048, 1024, &num_skys );
    textures = loadTextures( "./bitmaps/textures.bmp", 256, 256, 138, 16, 16, &num_textures );
}

GLuint textures_getDirtTexture( ) {
    return textures[ 2 ];
}
GLuint textures_getGrassTexture( ) {
    return textures[ 0 ];
}
GLuint textures_getSkyTexture( ) {
    return sky_textures[ 0 ];
}
GLuint textures_getGrassSideTexture( ) {
    return textures[ 3 ];
}
GLuint textures_getWaterTexture( ) {
    return textures[ 14 ];
}

void textures_free( ) {
    glDeleteTextures( num_textures, textures );
    glDeleteTextures( num_skys, sky_textures );
    free( textures );
    free( sky_textures );
}
