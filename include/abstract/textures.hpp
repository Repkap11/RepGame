#ifndef HEADER_TEXTURES_H
#define HEADER_TEXTURES_H

typedef struct {
    unsigned int m_RendererId; //
    unsigned int slot;
} Texture;

#include "block_definitions.hpp"

void textures_set_texture_data( unsigned char *textures, int textures_len );

void texture_init_blocks( Texture *texture );
void texture_init_sky( Texture *texture );
// Texture textures_get_texture( Textures *textures, BlockID id );
void texture_destroy( Texture *texture );
void texture_bind( Texture *texture, unsigned int texture_slot );
void texture_unbind( Texture *texture, unsigned int texture_slot );
#endif
