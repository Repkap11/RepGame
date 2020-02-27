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
#include <map>

#define MAX_MOB_COUNT 20

class Mobs {
    IndexBuffer ib;
    VertexArray va;
    std::vector<ObjectPosition> mob_positions;
    std::map<int, int> mob_index_lookup;
    VertexBuffer vb_mob_placement;
    VertexBuffer vb_mob_shape;
    int shouldDraw;
    glm::mat4 initial_mat;
    static void init_mob( ObjectPosition *mob, int id );

  public:
    Mobs( );
    Mobs( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_position );
    void add_mob( unsigned int mob_id );
    void update_position( int mob_id, float x, float y, float z, const glm::mat4 &rotation );
    void remove_mob( int mob_id );
    void draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader );
    void cleanup( );
    int check_consistency( );
};

#endif