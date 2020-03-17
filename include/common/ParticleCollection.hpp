#ifndef HEADER_PARTICLE_COLLECTION_H
#define HEADER_PARTICLE_COLLECTION_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/textures.hpp"
#include "abstract/renderer.hpp"
#include "block.hpp"
#include "chunk.hpp"
#include "common/Particle.hpp"
#include <vector>
#include <map>

class ParticleCollection {
    IndexBuffer ib;
    VertexArray va;
    std::vector<ParticlePosition> particle_positions;
    std::map<int, int> particle_index_lookup;
    VertexBuffer vb_particle_placement;
    VertexBuffer vb_particle_shape;
    int shouldDraw;
    glm::mat4 initial_mat;

  protected:
    virtual void init_particle( ParticlePosition *particle, int particle_id ) = 0;

  public:
    ParticleCollection( );
    ParticleCollection( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_position );
    void add( unsigned int particle_id );
    void update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation );
    void remove( int particle_id );
    void draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader );
    void cleanup( );
    int check_consistency( );
};

#endif