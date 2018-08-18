#version 310 es
#extension GL_ANDROID_extension_pack_es31a : require
precision highp float;

uniform mat4 u_MVP;

layout( triangles, equal_spacing, cw ) in;
in vec3 tc_position[];
out vec3 te_PatchDistance;
out vec3 te_position;

void main( ) {
    vec3 p0 = gl_TessCoord.x * tc_position[ 0 ];
    vec3 p1 = gl_TessCoord.y * tc_position[ 1 ];
    vec3 p2 = gl_TessCoord.z * tc_position[ 2 ];
    te_PatchDistance = gl_TessCoord;
    te_position = normalize( p0 + p1 + p2 );
    gl_Position = u_MVP * vec4( te_position, 1 );
}
