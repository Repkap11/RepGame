#version 310 es
#extension GL_ANDROID_extension_pack_es31a : require

precision highp float;

layout( vertices = 3 ) out;
in vec3 v_position[];
in vec2 v_TexCoordBlock[];
in flat float v_blockID[];
in float v_corner_light[];

out vec3 tc_position[];
out vec2 tc_TexCoordBlock[];
out flat float tc_blockID[];
out float tc_corner_light[];

#define TessLevelInner 1.0f
#define TessLevelOuter 2.0f

void main( ) {
    tc_TexCoordBlock[ gl_InvocationID ] = v_TexCoordBlock[ gl_InvocationID ];
    tc_blockID[ gl_InvocationID ] = v_blockID[ gl_InvocationID ];
    tc_position[ gl_InvocationID ] = v_position[ gl_InvocationID ];
    tc_corner_light[ gl_InvocationID ] = v_corner_light[ gl_InvocationID ];

    gl_TessLevelOuter[ gl_InvocationID ] = TessLevelOuter;

    if ( gl_InvocationID == 0 ) {
        gl_TessLevelInner[ 0 ] = TessLevelInner;
    }
}
