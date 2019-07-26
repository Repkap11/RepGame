#ifndef HEADER_MOBS_H
#define HEADER_MOBS_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/textures.hpp"
#include "abstract/renderer.hpp"
#include "block.hpp"
#include "chunk.hpp"
#include "common/object.hpp"

typedef struct {
    IndexBuffer ib;
    VertexArray va;
    ObjectPosition mob_position;
    VertexBuffer vb_mob_placement;
    VertexBuffer vb_mob_shape;
    int shouldDraw;

} Mobs;

void mobs_init( Mobs *mobs, VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_position );
void mobs_update_position( Mobs *mobs, float x, float y, float z, glm::mat4 &rotation );
void mobs_draw( Mobs *mobs, Renderer *renderer, Shader *shader );
void mobs_cleanup( Mobs *mobs );

#endif