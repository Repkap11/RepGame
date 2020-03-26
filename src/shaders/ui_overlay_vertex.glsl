#version 300 es
uniform mat4 u_MVP;

#define FACE_TOP 0u
#define FACE_BOTTOM 1u
#define FACE_RIGHT 2u
#define FACE_FRONT 3u
#define FACE_LEFT 4u
#define FACE_BACK 5u

// From UIOverlayVertex
layout( location = 0 ) in vec2 screen_position;
layout( location = 1 ) in uint is_block;
layout( location = 2 ) in vec3 texture_coord;
layout( location = 3 ) in vec4 tint;
layout( location = 4 ) in uint face_type;

flat out uint v_is_block;
out vec3 v_TexCoordBlock;
out vec4 v_Tint;

void main( ) {
    gl_Position = u_MVP * vec4( screen_position, -0.1, 1 );
    if ( is_block != 0u ) {
        float face_light;
        if ( face_type == FACE_TOP ) {
            face_light = 1.0;
        } else if ( face_type == FACE_FRONT ) {
            face_light = 0.8;
        } else {
            face_light = 0.6;
        }
        v_TexCoordBlock = texture_coord;
        v_Tint = vec4( tint.rgb * face_light, tint.a );
    } else {
        v_TexCoordBlock = texture_coord;
        v_Tint = tint;
    }
    v_is_block = is_block;
}