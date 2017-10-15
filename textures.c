#include <GL/gl.h>
#include <GL/glut.h>

#include "draw2d.h"
#include "textures.h"

int loadTexture( const char *filename, int size, int bmp_header ) {
    GLuint texture;
    int width, height;
    unsigned char *data;
    FILE *file;
    file = fopen( filename, "rb" );
    if ( file == NULL ) {
        pr_debug( "Error rendering texture %s", filename );
        return 0;
    }
    width = size;
    height = size;
    data = ( unsigned char * )malloc( width * height * 3 + bmp_header );
    // int size = fseek(file,);
    fread( data, width * height * 3 + bmp_header, 1, file );
    fclose( file );

    glGenTextures( 1, &texture );
    glBindTexture( GL_TEXTURE_2D, texture );
    glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

    float fLargest;
    glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest );
    // glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);

    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
    gluBuild2DMipmaps( GL_TEXTURE_2D, 3, width, height, GL_BGR, GL_UNSIGNED_BYTE, data + bmp_header );
    // glTexImage2DMultisample( GL_TEXTURE_2D_MULTISAMPLE, 12, GL_RGBA8, 1, 1, false );
    // glTexStorage2D( GL_TEXTURE_2D, 12, GL_RGBA8, width, height );
    // glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, data + bmp_header );
    // glGenerateMipmap( GL_TEXTURE_2D ); // Generate num_mipmaps number of mipmaps here.

    // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

    free( data );
    return texture;
}

int dirt_texture, grass_texture, grass_side_texture, sky_texture;
void textures_populate( ) {
    grass_side_texture = loadTexture( "./bitmaps/grass_side.bmp", 16, 54 );
    grass_texture = loadTexture( "./bitmaps/grass-top.bmp", 16, 54 );
    dirt_texture = loadTexture( "./bitmaps/dirt.bmp", 16, 54 );
    sky_texture = loadTexture( "./bitmaps/sky3.bmp", 64, 54 );
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
