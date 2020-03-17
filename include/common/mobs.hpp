#ifndef HEADER_MOBS_H
#define HEADER_MOBS_H

#include "common/ParticleCollection.hpp"

class Mobs : public ParticleCollection {
  public:
    Mobs( );
    Mobs( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_position );

  protected:
    void init_particle( ParticlePosition *mob, int particle_id ) override;
};

#endif