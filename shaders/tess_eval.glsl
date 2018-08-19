#version 310 es
#extension GL_ANDROID_extension_pack_es31a : require

uniform mat4 u_MVP;

layout( triangles, equal_spacing, ccw ) in;

in vec3 tc_position[];
in vec2 tc_TexCoordBlock[];
in flat float tc_blockID[];
in float tc_corner_light[];

out vec3 te_position;
out vec2 te_TexCoordBlock;
out flat float te_blockID;
out float te_corner_light;
out vec3 te_PatchDistance;

float interpolate1D( float v0, float v1, float v2 ) {
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}
vec2 interpolate2D( vec2 v0, vec2 v1, vec2 v2 ) {
    return vec2( gl_TessCoord.x ) * v0 + vec2( gl_TessCoord.y ) * v1 + vec2( gl_TessCoord.z ) * v2;
}
vec3 interpolate3D( vec3 v0, vec3 v1, vec3 v2 ) {
    return vec3( gl_TessCoord.x ) * v0 + vec3( gl_TessCoord.y ) * v1 + vec3( gl_TessCoord.z ) * v2;
}

void main( ) {
    te_TexCoordBlock = interpolate2D( tc_TexCoordBlock[ 0 ], tc_TexCoordBlock[ 1 ], tc_TexCoordBlock[ 2 ] );
    te_TexCoordBlock = interpolate2D( tc_TexCoordBlock[ 0 ], tc_TexCoordBlock[ 1 ], tc_TexCoordBlock[ 2 ] );
    te_blockID = tc_blockID[ 0 ];
    te_corner_light = interpolate1D( tc_corner_light[ 0 ], tc_corner_light[ 1 ], tc_corner_light[ 2 ] );
    te_position = interpolate3D( tc_position[ 0 ], tc_position[ 1 ], tc_position[ 2 ] );

    te_PatchDistance = gl_TessCoord;
    gl_Position = u_MVP * vec4( te_position, 1 );
}
