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

    unsigned int textures_across = width / tile_size_across;
    unsigned int textures_down = height / tile_size_down;
    unsigned int layer_count = textures_across * textures_down;
    unsigned int texture;
    glGenTextures( 1, &texture );
    glBindTexture( GL_TEXTURE_2D_ARRAY, texture );
    pr_debug( "About to tex 3d" );
    glTexStorage3D( GL_TEXTURE_2D_ARRAY,              //
                    1,                                // mipLevelCount
                    GL_RGBA8,                         //
                    tile_size_across, tile_size_down, //
                    layer_count );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, width );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, height );

    for ( int i = 0; i < layer_count; i++ ) {
        int tex_coord_x = ( i % textures_across );
        int tex_coord_y = ( textures_down - 1 ) - ( i / textures_across );
        int text_coord_base = tex_coord_x * tile_size_across + tex_coord_y * tile_size_down * width;

        glTexSubImage3D( GL_TEXTURE_2D_ARRAY,                 //
                         0,                                   // Mipmap Level
                         0, 0, i,                             // offset
                         tile_size_across, tile_size_down, 1, //
                         GL_ABGR_EXT,                         //
                         GL_UNSIGNED_BYTE,                    //
                         data + bmp_header + text_coord_base * BYTEX_PER_PIXEL );
    }

    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST );              // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
    glGenerateMipmap( GL_TEXTURE_2D_ARRAY );
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