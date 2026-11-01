#version 300 es
uniform mat4 u_MVP;
uniform float u_ReflectionHeight;
uniform vec3 u_Origin;

#define FACE_TOP 0u
#define FACE_BOTTOM 1u
#define FACE_RIGHT 2u
#define FACE_FRONT 3u
#define FACE_LEFT 4u
#define FACE_BACK 5u

// See ParticleVertex in Particle.hpp (same layout as object_vertex.glsl):
// the shared extruded-sprite mesh lives in item_entities.cpp.
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;
layout( location = 2 ) in uint faceType;

// See ItemSpriteInstance in item_entities.hpp
layout( location = 3 ) in uint texLayer;
layout( location = 4 ) in mat4 transform;
// Per-instance flood-fill light at the item center (x=sky, y=block, 0-1);
// the mat4 above takes locations 4-7.
layout( location = 8 ) in vec2 instance_light;

out vec2 v_tex_coords;
// object_fragment clamps to this bound (mob faces only);
// a huge rect leaves this program's sampling untouched.
flat out vec4 v_tex_bounds;
out float v_light;
flat out vec2 v_light_field;
flat out float v_blockID;
flat out float v_id;
flat out float v_flash;
out float v_planarDot;
out vec4 v_world_coords;

void main( ) {
    v_world_coords = transform * vec4( position, 1 ) - vec4( u_Origin, 0.0 );
    gl_Position = u_MVP * v_world_coords;
    v_planarDot = dot( v_world_coords, vec4( 0, 1, 0, u_ReflectionHeight ) );

    v_tex_coords = texCoords;
    v_tex_bounds = vec4( -1e6, -1e6, 1e6, 1e6 );
    v_flash = 0.0f;
    // The whole sprite samples one atlas layer (the item's flat icon).
    v_blockID = float( texLayer );

    float face_light;
    if ( faceType == FACE_TOP ) {
        face_light = 1.0;
    } else if ( faceType == FACE_BOTTOM ) {
        face_light = 0.5;
    } else {
        face_light = 0.75;
    }
    v_light = face_light;
    v_light_field = instance_light;
}
