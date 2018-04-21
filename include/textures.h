#ifndef HEADER_TEXTURES_H
#define HEADER_TEXTURES_H
#include <GL/gl.h>

typedef struct {
    unsigned int m_RendererId; //
} Texture;

#include "block_definitions.h"

void texture_init_blocks( Texture *texture );
void texture_init_sky( Texture *texture );
// Texture textures_get_texture( Textures *textures, BlockID id );
void texture_destroy( Texture *texture );

void texture_bind( Texture *texture, unsigned int texture_slot );
void texture_unbind( Texture *texture, unsigned int texture_slot );
#endif
