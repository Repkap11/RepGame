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
    VertexBuffer vb_mob_placement;

    CubeFace mob_shape;
    VertexBuffer vb_mob_shape;
    int shouldDraw;

} Mobs;

void mobs_init( Mobs *mobs, VertexBufferLayout *vbl_mob_shape, VertexBufferLayout *vbl_mob_placement );
void mobs_update_position( Mobs *mobs, float x, float y, float z, glm::mat4 &rotation );
void mobs_draw( Mobs *mobs, Renderer *renderer, Shader *shader );
void mobs_cleanup( Mobs *mobs );

#endif