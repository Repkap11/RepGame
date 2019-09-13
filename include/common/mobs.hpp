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
#include <vector>

#define MAX_MOB_COUNT 20

typedef struct {
    IndexBuffer ib;
    VertexArray va;
    std::vector<ObjectPosition> mob_positions;
    int mob_index_lookup[ MAX_MOB_COUNT ];
    VertexBuffer vb_mob_placement;
    VertexBuffer vb_mob_shape;
    int shouldDraw;
    glm::mat4 initial_mat;

} Mobs;

void mobs_init( Mobs *mobs, VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_position );
void mobs_add_mob( Mobs *mobs, unsigned int mob_id );
void mobs_update_position( Mobs *mobs, int mob_id, float x, float y, float z, const glm::mat4 &rotation );
void mobs_remove_mob( Mobs *mobs, int mob_id );
void mobs_draw( Mobs *mobs, const glm::mat4 &mvp, Renderer *renderer, Shader *shader );
void mobs_cleanup( Mobs *mobs );

#endif