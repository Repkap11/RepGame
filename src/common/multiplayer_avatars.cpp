#include "common/multiplayer_avatars.hpp"
#include "common/RepGame.hpp"

void MultiplayerAvatars::init( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_position ) {
    this->render_chain.init( vbl_object_vertex, vbl_object_position, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    glm::mat4 scale = glm::scale( glm::mat4( 1.0 ), glm::vec3( 0.5f ) );
    glm::mat4 un_translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( -0.5f, -0.5f, -0.5f ) );
    this->initial_mat = scale * un_translate;
}

void MultiplayerAvatars::add( unsigned int particle_id ) {
    auto data = this->render_chain.create_instance( );
    this->entity_map[ particle_id ] = data.first;
    this->init_particle( data.second );
}

void MultiplayerAvatars::update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation ) {
    // pr_debug( "Updating particle_id:%d", particle_id );
    entt::entity &entity = this->entity_map[ particle_id ];

    ParticlePosition &particle = this->render_chain.get_instance( entity );
    glm::mat4 translate = glm::translate( glm::mat4( 1.0 ), glm::vec3( x, y, z ) );
    particle.transform = translate * rotation * this->initial_mat;
    this->render_chain.invalidate( entity );

    // // Add a trail following the player over time
    // auto data = this->render_chain->create_face( 100 );
    // this->init_particle( data.second );
    // this->render_chain->update_position( data.first, x, y, z, rotation );
}

void MultiplayerAvatars::remove( unsigned int particle_id ) {
    entt::entity &entity = this->entity_map[ particle_id ];
    this->render_chain.remove( entity );
}

void MultiplayerAvatars::init_particle( ParticlePosition &mob ) {
    mob.face[ FACE_TOP ] = STEVE_HEAD_TOP - 1;
    mob.face[ FACE_BOTTOM ] = STEVE_HEAD_BOTTOM - 1;
    mob.face[ FACE_RIGHT ] = STEVE_HEAD_RIGHT - 1;
    mob.face[ FACE_FRONT ] = STEVE_HEAD_FRONT - 1;
    mob.face[ FACE_LEFT ] = STEVE_HEAD_LEFT - 1;
    mob.face[ FACE_BACK ] = STEVE_HEAD_BACK - 1;
}

void MultiplayerAvatars::draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader ) {
    this->render_chain.draw( mvp, renderer, shader );
}

void MultiplayerAvatars::cleanup( ) {
    this->render_chain.cleanup( );
}
