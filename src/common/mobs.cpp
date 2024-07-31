#include "common/mobs.hpp"
#include "common/RepGame.hpp"

Mobs::Mobs( ) : ECS_Renderer( ) {
}

Mobs::Mobs( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement ) : ECS_Renderer( vbl_object_vertex, vbl_object_placement ) {
}

void Mobs::init_particle( ParticlePosition *mob, int particle_id ) {
    mob->face[ FACE_TOP ] = STEVE_HEAD_TOP - 1;
    mob->face[ FACE_BOTTOM ] = STEVE_HEAD_BOTTOM - 1;
    mob->face[ FACE_RIGHT ] = STEVE_HEAD_RIGHT - 1;
    mob->face[ FACE_FRONT ] = STEVE_HEAD_FRONT - 1;
    mob->face[ FACE_LEFT ] = STEVE_HEAD_LEFT - 1;
    mob->face[ FACE_BACK ] = STEVE_HEAD_BACK - 1;
}
