#include "common/multiplayer_avatars.hpp"
#include "common/RepGame.hpp"

void MultiplayerAvatars::init( ECS_Renderer *ecs_renderer ) {
    this->ecs_renderer = ecs_renderer;
}

void MultiplayerAvatars::add( unsigned int particle_id ) {
    auto data = this->ecs_renderer->create_particle( 0 );
    this->entity_map[ particle_id ] = data.first;
    this->init_particle( data.second );
}

void MultiplayerAvatars::update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation ) {
    // pr_debug( "Updating particle_id:%d", particle_id );
    entt::entity &entity = this->entity_map[ particle_id ];
    this->ecs_renderer->update_position( entity, x, y, z, rotation );

    // Add a trail following the player over time
    auto data = this->ecs_renderer->create_face( 100 );
    this->init_particle( data.second );
    this->ecs_renderer->update_position( data.first, x, y, z, rotation );
}

void MultiplayerAvatars::remove( unsigned int particle_id ) {
    entt::entity &entity = this->entity_map[ particle_id ];
    this->ecs_renderer->remove( entity );
}

void MultiplayerAvatars::init_particle( ParticlePosition &mob ) {
    mob.face[ FACE_TOP ] = STEVE_HEAD_TOP - 1;
    mob.face[ FACE_BOTTOM ] = STEVE_HEAD_BOTTOM - 1;
    mob.face[ FACE_RIGHT ] = STEVE_HEAD_RIGHT - 1;
    mob.face[ FACE_FRONT ] = STEVE_HEAD_FRONT - 1;
    mob.face[ FACE_LEFT ] = STEVE_HEAD_LEFT - 1;
    mob.face[ FACE_BACK ] = STEVE_HEAD_BACK - 1;
}
