#define GL_GLEXT_PROTOTYPES
#include "common/utils/file_utils.hpp"

#include "common/RepGame.hpp"
#include "common/abstract/textures.hpp"
#include <cstring>

#if defined( REPGAME_ANDROID )
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
unsigned char *cached_texture[ 2 ];
void textures_set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len ) {
    for ( int i = 0; i < textures_len; i += 4 ) {
        unsigned char a = textures[ i + 0 ];
        unsigned char b = textures[ i + 1 ];
        unsigned char g = textures[ i + 2 ];
        unsigned char r = textures[ i + 3 ];
        textures[ i + 0 ] = r;
        textures[ i + 1 ] = g;
        textures[ i + 2 ] = b;
        textures[ i + 3 ] = a;
    }
    cached_texture[ which_texture ] = textures;
}
#else
void textures_set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len ) {
    pr_debug( "textures_set_texture_data doesnt need to be called on Linux WASM or Windows" );
}
#endif

#if defined( REPGAME_WASM )
unsigned char *readTextureDataFromFile( const char *filename, int mem_size ) {
    unsigned char *data;
    FILE *file;
    pr_debug( "Reading texture:%s", filename );
    file = fopen( filename, "rb" );
    if ( file == NULL ) {
        pr_debug( "Error rendering texture %s", filename );
        return 0;
    }
    data = ( unsigned char * )malloc( mem_size );
    size_t read_size = fread( data, mem_size, 1, file );
    if ( read_size != 1 ) {
        pr_debug( "Texture file wrong size. Expected:%d", mem_size );
    }
    fclose( file );
    for ( int i = 0; i < mem_size; i += 4 ) {
        unsigned char a = data[ i + 0 ];
        unsigned char b = data[ i + 1 ];
        unsigned char g = data[ i + 2 ];
        unsigned char r = data[ i + 3 ];
        data[ i + 0 ] = r;
        data[ i + 1 ] = g;
        data[ i + 2 ] = b;
        data[ i + 3 ] = a;
    }
    return data;
}
#endif
#if defined( REPGAME_ANDROID )
unsigned char *readTextureDataFromCache( const char *filename, int mem_size ) {
    // TODO this is such a hack
    pr_debug( "Returning texture for %d", filename );
    if ( strcmp( filename, "bitmaps/textures.bin" ) == 0 )
        return cached_texture[ 0 ];
    if ( strcmp( filename, "bitmaps/sky4.bin" ) == 0 )
        return cached_texture[ 1 ];
    pr_debug( "Cant find a texture for:%s", filename );
    exit( 1 );
    return NULL;
}
#endif

#define BYTEX_PER_PIXEL 4
unsigned int loadTexture( const TextureSourceData *texture_source ) {
    int bmp_header = texture_source->header_size;
    unsigned char *data;

    int mem_size = texture_source->width * texture_source->height * BYTEX_PER_PIXEL + bmp_header;
    int needs_free;

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
    needs_free = 0;
    if ( texture_source->blob->length != mem_size ) {
        int diff = texture_source->blob->length - mem_size;
        pr_debug( "Unexpected Source Size blob:%d calculated:%d diff:%d", texture_source->blob->length, mem_size, diff );
        exit( 1 );
    }
    data = ( unsigned char * )texture_source->blob->source;
#elif defined( REPGAME_WASM )
    needs_free = 1;
    data = readTextureDataFromFile( texture_source->filename, mem_size );
#elif defined( REPGAME_ANDROID )
    needs_free = 0;
    data = readTextureDataFromCache( texture_source->filename, mem_size );
#endif

    unsigned int textures_across = texture_source->width / texture_source->tile_size_across;
    unsigned int textures_down = texture_source->height / texture_source->tile_size_down;
    unsigned int layer_count = textures_across * textures_down;
    unsigned int texture;
    glGenTextures( 1, &texture );
    glBindTexture( GL_TEXTURE_2D_ARRAY, texture );
#if defined( REPGAME_WASM )
    glTexImage3D( GL_TEXTURE_2D_ARRAY,                                              //
                  0,                                                                // mipLevelCount
                  GL_RGBA8,                                                         //
                  texture_source->tile_size_across, texture_source->tile_size_down, //
                  layer_count, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
#else
    glTexStorage3D( GL_TEXTURE_2D_ARRAY,                                              //
                    5,                                                                // mipLevelCount
                    GL_RGBA8,                                                         //
                    texture_source->tile_size_across, texture_source->tile_size_down, //
                    layer_count );
#endif
    glPixelStorei( GL_UNPACK_ROW_LENGTH, texture_source->width );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, texture_source->height );
    for ( unsigned int i = 0; i < layer_count; i++ ) {
        int tex_coord_x = ( i % textures_across );
        int tex_coord_y = ( textures_down - 1 ) - ( i / textures_across );
        int text_coord_base = tex_coord_x * texture_source->tile_size_across + tex_coord_y * texture_source->tile_size_down * texture_source->width;
        glTexSubImage3D( GL_TEXTURE_2D_ARRAY,                                                 //
                         0,                                                                   // Mipmap Level
                         0, 0, i,                                                             // offset
                         texture_source->tile_size_across, texture_source->tile_size_down, 1, //
                         GL_RGBA,
                         GL_UNSIGNED_BYTE, //
                         data + bmp_header + text_coord_base * BYTEX_PER_PIXEL );
    }

    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT );
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR ); // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST );              // GL_NEAREST GL_LINEAR_MIPMAP_LINEAR
    float max_ani;
    glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_ani );
    pr_debug( "Max ani:%f", max_ani );
    if ( needs_free ) {
        free( data );
    }
    glTexParameterf( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_ani );
    glGenerateMipmap( GL_TEXTURE_2D_ARRAY );
    return texture;
}

static int next_slot = 1;
void texture_init( Texture *texture, const TextureSourceData *texture_source ) {
    texture->slot = next_slot;
    next_slot++;
    glActiveTexture( GL_TEXTURE0 + texture->slot );
    texture->m_RendererId = loadTexture( texture_source );
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