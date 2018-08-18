#version 310 es
#extension GL_ANDROID_extension_pack_es31a : require
precision highp float;

uniform mat4 u_MVP;

layout( triangles, equal_spacing, ccw ) in;
in vec3 tc_position[];
in vec2 tc_TexCoordBlock[];
in flat float tc_blockID[];
in float tc_corner_light[];

out vec3 te_PatchDistance;
out vec3 te_position;
out vec2 te_TexCoordBlock;
out flat float te_blockID;
out float te_corner_light;

void main( ) {
    te_TexCoordBlock = tc_TexCoordBlock[ 0 ];
    te_blockID = tc_blockID[ 0 ];
    te_corner_light = tc_corner_light[ 0 ];
    vec3 p0 = gl_TessCoord.x * tc_position[ 0 ];
    vec3 p1 = gl_TessCoord.y * tc_position[ 1 ];
    vec3 p2 = gl_TessCoord.z * tc_position[ 2 ];
    te_PatchDistance = gl_TessCoord;
    te_position = ( p0 + p1 + p2 );
    gl_Position = u_MVP * vec4( te_position, 1 );
}
