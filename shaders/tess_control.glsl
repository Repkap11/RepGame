#version 310 es
#extension GL_ANDROID_extension_pack_es31a : require
precision highp float;

layout( vertices = 3 ) out;
in vec3 v_position[];
out vec3 tc_position[];

#define TessLevelInner 3.0f
#define TessLevelOuter 3.0f

void main( ) {
    tc_position[ gl_InvocationID ] = v_position[ gl_InvocationID ];
    if ( gl_InvocationID == 0 ) {
        gl_TessLevelInner[ 0 ] = TessLevelInner;
        gl_TessLevelOuter[ 0 ] = TessLevelOuter;
        gl_TessLevelOuter[ 1 ] = TessLevelOuter;
        gl_TessLevelOuter[ 2 ] = TessLevelOuter;
    }
}
