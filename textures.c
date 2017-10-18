#include <GL/gl.h>
#include <GL/glut.h>

#include "draw2d.h"
#include "textures.h"

int loadTexture( const char *filename, int width, int height, int bmp_header ) {
    GLuint texture;
    unsigned char *data;
    FILE *file;
    file = fopen( filename, "rb" );
    if ( file == NULL ) {
        pr_debug( "Error rendering texture %s", filename );
        return 0;
    }
    size_t mem_size = width * height * 4 + bmp_header;
    data = ( unsigned char * )malloc( mem_size );
    size_t read_size = fread( data, mem_size, 1, file );
    if ( read_size != mem_size ) {
        pr_debug( "Texture file wrong size. Expected:%ld got:%ld", mem_size, read_size );
    }
    fclose( file );

    // Image is really in ARGB but we need BGRA
    for ( int i = 0; i < width * height; i++ ) {
        char a = data[ i * 4 + 0 + bmp_header ];
        char r = data[ i * 4 + 1 + bmp_header ];
        char g = data[ i * 4 + 2 + bmp_header ];
        char b = data[ i * 4 + 3 + bmp_header ];

        data[ i * 4 + 0 + bmp_header ] = r;
        data[ i * 4 + 1 + bmp_header ] = g;
        data[ i * 4 + 2 + bmp_header ] = b;
        data[ i * 4 + 3 + bmp_header ] = a;
    }

    glGenTextures( 1, &texture );
    glBindTexture( GL_TEXTURE_2D, texture );
    glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

    float fLargest;
    glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest );
    // glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);

    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
    gluBuild2DMipmaps( GL_TEXTURE_2D, 3, width, height, GL_BGRA, GL_UNSIGNED_BYTE, data + bmp_header );

    free( data );
    return texture;
}

int dirt_texture, grass_texture, grass_side_texture, sky_texture, water_texture;
void textures_populate( ) {
    grass_side_texture = loadTexture( "./bitmaps/grass_side.bmp", 16, 16, 138 );
    grass_texture = loadTexture( "./bitmaps/grass-top.bmp", 16, 16, 138 );
    dirt_texture = loadTexture( "./bitmaps/dirt.bmp", 16, 16, 138 );
    sky_texture = loadTexture( "./bitmaps/sky4.bmp", 2048, 1024, 138 );
    water_texture = loadTexture( "./bitmaps/water.bmp", 16, 16, 138 );
}

int textures_getDirtTexture( ) {
    return dirt_texture;
}
int textures_getGrassTexture( ) {
    return grass_texture;
}
int textures_getSkyTexture( ) {
    return sky_texture;
}
int textures_getGrassSideTexture( ) {
    return grass_side_texture;
}
int textures_getWaterTexture( ) {
    return water_texture;
}
