#version 320 es

layout( vertices = 3 ) out;
in vec3 v_position[];
in vec2 v_TexCoordBlock[];
in float v_blockID[];
in float v_corner_light[];

out vec3 tc_position[];
out vec2 tc_TexCoordBlock[];
out float tc_blockID[];
out float tc_corner_light[];

#define TessLevelInner 2.0
#define TessLeevelOuter 2.0

void main( ) {
    tc_TexCoordBlock[ gl_InvocationID ] = v_TexCoordBlock[ gl_InvocationID ];
    tc_blockID[ gl_InvocationID ] = v_blockID[ gl_InvocationID ];
    tc_position[ gl_InvocationID ] = v_position[ gl_InvocationID ];
    tc_corner_light[ gl_InvocationID ] = v_corner_light[ gl_InvocationID ];

    if ( gl_InvocationID == 0 ) {
        gl_TessLevelInner[ 0 ] = TessLevelInner;
        gl_TessLevelOuter[ 0 ] = TessLeevelOuter;
        gl_TessLevelOuter[ 1 ] = TessLeevelOuter;
        gl_TessLevelOuter[ 2 ] = TessLeevelOuter;
    }
}
