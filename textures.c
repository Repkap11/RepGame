#include <GL/gl.h>
#include <GL/glut.h>

#include "draw2d.h"
#include "textures.h"

int loadTexture( const char *filename ) {
    GLuint texture;
    int width, height;
    unsigned char *data;
    FILE *file;
    file = fopen( filename, "rb" );
    if ( file == NULL ) {
        pr_debug( "Error rendering texture %s", filename );
        return 0;
    }
    width = 512;
    height = 512;
    int bmp_header = 122;
    data = ( unsigned char * )malloc( width * height * 3 + bmp_header );
    // int size = fseek(file,);
    fread( data, width * height * 3 + bmp_header, 1, file );
    pr_debug( "Finished reading file" );
    fclose( file );

    glGenTextures( 1, &texture );
    glBindTexture( GL_TEXTURE_2D, texture );
    glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST );

    float fLargest;
    glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest );
    pr_debug( "Largest Fitering:%f", fLargest );
    // glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);

    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
    gluBuild2DMipmaps( GL_TEXTURE_2D, 3, width, height, GL_BGR, GL_UNSIGNED_BYTE, data + bmp_header );
    // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

    free( data );
    pr_debug( "Done with loading texture" );
    return texture;
}

int dirt_texture, grass_texture;
void textures_populate( ) {
    dirt_texture = loadTexture( "./bitmaps/dirt.bmp" );
    grass_texture = loadTexture( "./bitmaps/grass.bmp" );
}

int textures_getDirtTexture( ) {
    return dirt_texture;
}
int textures_getGrassTexture( ) {
    return grass_texture;
}
