#ifndef HEADER_MOBS_H
#define HEADER_MOBS_H

#include "common/Particle.hpp"
#include "common/ecs_renderer.hpp"

class MultiplayerAvatars {
    ECS_Renderer *ecs_renderer;
    entt::entity entity_map[ MAX_CLIENT_FDS ];

    void init_particle( ParticlePosition &mob );

  public:
    void init( ECS_Renderer *ecs_renderer );
    void add( unsigned int particle_id );
    void update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation );
    void remove( unsigned int particle_id );
};

#endif