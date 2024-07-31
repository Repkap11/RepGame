#version 300 es
uniform mat4 u_MVP;
uniform float u_ReflectionHeight;

#define FACE_TOP 0u
#define FACE_BOTTOM 1u
#define FACE_RIGHT 2u
#define FACE_FRONT 3u
#define FACE_LEFT 4u
#define FACE_BACK 5u

// See ParticleVertex in block.hpp
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;
layout( location = 2 ) in uint faceType;

// See ParticlePosition in chunk.hpp
layout( location = 3 ) in uvec3 blockTexture;
layout( location = 4 ) in mat4 transform;

out vec2 v_tex_coords;
out float v_light;
flat out float v_blockID;
flat out float v_id;
out float v_planarDot;
out vec4 v_world_coords;


void main( ) {
    v_world_coords = transform * vec4( position, 1 );
    gl_Position = u_MVP * v_world_coords;
    v_planarDot = dot( v_world_coords, vec4( 0, 1, 0, u_ReflectionHeight ) );

    v_tex_coords = texCoords;
    uint shift = ( faceType % 2u ) * 16u;
    // 0xffffu is max short 16u is sizeof(short)

    float face_light;
    if ( faceType == FACE_TOP ) {
        face_light = 1.0;
    } else if ( faceType == FACE_BOTTOM ) {
        face_light = 0.5;
    } else {
        face_light = 0.75;
    }
    v_light = face_light;
    v_blockID = float( ( blockTexture[ faceType / 2u ] & ( 0xffffu << shift ) ) >> shift );
}
