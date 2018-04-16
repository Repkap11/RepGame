#ifndef HEADER_TEXTURES_H
#define HEADER_TEXTURES_H
#include <GL/gl.h>

typedef struct {
    unsigned int m_RendererId; //
} Texture;

#include "block_definitions.h"

typedef struct {
    unsigned int *m_RendererIds; //
    unsigned int num_RendererIds;
} Textures;

void textures_init_blocks( Textures *textures );
void textures_init_sky( Textures *textures );
Texture textures_get_texture( Textures *textures, BlockID id );
void textures_destroy( Textures *textures );

void texture_bind( Texture *texture );
void texture_unbind( Texture *texture );
#endif
