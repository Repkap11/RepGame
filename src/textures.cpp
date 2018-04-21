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
        char r = data[ i * BYTEX_PER_PIXEL + 1 + bmp_header ];
        char g = data[ i * BYTEX_PER_PIXEL + 2 + bmp_header ];
        char b = data[ i * BYTEX_PER_PIXEL + 3 + bmp_header ];

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
    glTexStorage3D( GL_TEXTURE_2D_ARRAY, //
                    4,                   // mipLevelCount
                    GL_RGBA8,            //
                    width, height,       //
                    textures_across * textures_down );
    pr_debug( "Just to tex 3d" );

    //glTexSubImage3D( GL_TEXTURE_2D_ARRAY, 0 /*Mipmap Level*/, 0 /*x_offset*/, 0 /*y_offset*/, 0 /*layer index offset*/, width, height, layer_count, GL_RGBA, GL_UNSIGNED_BYTE, data );
pr_debug( "Just to tex 3d" );

    // glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
    // gGenerateMipmap( GL_TEXTURE_2D_ARRAY );

    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR

    // unsigned char *working_data = ( unsigned char * )malloc( tile_size_across * tile_size_down * 4 );
    // for ( int i = 0; i < textures_across * textures_down; i++ ) {
    //     int tex_coord_x = ( i % textures_across );
    //     int tex_coord_y = ( textures_down - 1 ) - ( i / textures_across );
    //     int text_coord_base = tex_coord_x * tile_size_across + tex_coord_y * tile_size_down * width;

    //     for ( int pixel_y = 0; pixel_y < tile_size_down; pixel_y++ ) {
    //         for ( int pixel_x = 0; pixel_x < tile_size_across; pixel_x++ ) {
    //             int working_point = pixel_y * tile_size_across + pixel_x;

    //             int target_point = text_coord_base + pixel_y * width + pixel_x;

    //             working_data[ BYTEX_PER_PIXEL * working_point + 0 ] = data[ BYTEX_PER_PIXEL * target_point + bmp_header + 0 ];
    //             working_data[ BYTEX_PER_PIXEL * working_point + 1 ] = data[ BYTEX_PER_PIXEL * target_point + bmp_header + 1 ];
    //             working_data[ BYTEX_PER_PIXEL * working_point + 2 ] = data[ BYTEX_PER_PIXEL * target_point + bmp_header + 2 ];
    //             working_data[ BYTEX_PER_PIXEL * working_point + 3 ] = data[ BYTEX_PER_PIXEL * target_point + bmp_header + 3 ];
    //         }
    //     }

    //     glBindTexture( GL_TEXTURE_2D, textures[ i ] );

    //     glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

    //     // float fLargest;
    //     // glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest );
    //     // glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);

    //     //gluBuild2DMipmaps( GL_TEXTURE_2D, 3, tile_size_across, tile_size_down, GL_BGRA, GL_UNSIGNED_BYTE, working_data );
    // }
    // free( working_data );
    // free( data );
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
    texture->m_RendererId = loadTexture( bufferSky, 2048, 1024, 138, 2048, 1024 );
}

// Texture textures_get_texture( Textures *textures, BlockID id ) {
//     switch ( id ) {
//         case GRASS:
//             return 0;
//             break;

//         case STONE:
//             return 1;
//             break;

//         case DIRT:
//             return 2;
//             break;

//         case GRASS_SIDE:
//             return 3;
//             break;

//         case WOOD_PLANK:
//             return 4;
//             break;

//         case DOUBLE_SLAB:
//             return 5;
//             break;

//         case SLAB_TOP:
//             return 6;
//             break;

//         case BRICK:
//             return 7;
//             break;

//         case TNT:
//             return 8;
//             break;

//         case TNT_TOP:
//             return 9;
//             break;

//         case TNT_BOTTOM:
//             return 10;
//             break;

//         case WATER:
//             return 14;
//             break;

//         case COBBLESTONE:
//             return 16;
//             break;

//         case BEDROCK:
//             return 17;
//             break;

//         case SAND:
//             return 18;
//             break;

//         case GRAVEL:
//             return 19;
//             break;

//         case WOOD_LOG_SIDE:
//             return 20;
//             break;

//         case WOOD_LOG_TOP:
//             return 21;
//             break;

//         case IRON_BLOCK:
//             return 22;
//             break;

//         case GOLD_BLOCK:
//             return 23;
//             break;

//         case DIAMOND_BLOCK:
//             return 24;
//             break;

//         default:
//             pr_debug( "Warning, using default texture for %d", id );
//             return 1;
//             break;
//     }
// }

void texture_bind( Texture *texture, unsigned int texture_slot ) {
    glActiveTexture( GL_TEXTURE0 + texture_slot );
    glBindTexture( GL_TEXTURE_2D, texture->m_RendererId );
}
void texture_unbind( Texture *texture, unsigned int texture_slot ) {
    glActiveTexture( GL_TEXTURE0 + texture_slot );
    glBindTexture( GL_TEXTURE_2D, 0 );
}

void texture_destroy( Texture *texture ) {
    glDeleteTextures( 1, &texture->m_RendererId );
}