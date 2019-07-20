#ifndef HEADER_MOBS_H
#define HEADER_MOBS_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/textures.hpp"
#include "abstract/renderer.hpp"
#include "block.hpp"
#include "chunk.hpp"

typedef struct {
    IndexBuffer ib;
    VertexArray va;
    BlockCoords mob_placement;
    VertexBuffer vb_places;
    int shouldDraw;
    VertexBuffer vb_mob_shape;
    VertexBufferLayout vbl_mob_shape;
    VertexBufferLayout vbl_mob_placement;
} Mobs;

void mobs_init( Mobs *mobs );
void mobs_update_position( );
void mobs_draw( Mobs *mobs, Renderer *renderer, glm::mat4 &mvp );
void mobs_cleanup( Mobs *mobs );

#endif