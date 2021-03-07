#version 300 es
#define eps 0.00001f
#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;
uniform float u_ReflectionHeight;
uniform int u_TintUnderWater;
uniform int u_DrawToReflection;
uniform float u_ExtraAlpha;

layout( location = 0 ) out vec4 color;
layout( location = 1 ) out vec4 reflection;

in vec2 v_tex_coords;
in float v_light;
flat in float v_blockID;
in float v_planarDot;
in vec4 v_world_coords;

void main( ) {
    if ( v_planarDot < 0.0 && u_ReflectionHeight != 0.0 ) {
        discard;
    }
    vec4 texColor = texture( u_Texture, vec3( v_tex_coords, v_blockID ) );
    // vec4 texColor = vec4( 1, 0, 0, 1 );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    if ( u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS || ( u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && v_world_coords.y < ( -0.125f - eps ) ) ) {
        texColor = mix( texColor, vec4( 0.122f, 0.333f, 1.0f, 1.0f ), 0.8f );
    }
    vec4 lightedColor = texColor * vec4( v_light, v_light, v_light, u_ExtraAlpha );

    vec4 finalColor = lightedColor;
    vec4 finalReflection = lightedColor;
    if ( u_DrawToReflection == 1 ) {
        finalColor.a *= 0.0f;
    } else {
        finalReflection.a *= 0.0f;
    }
    color = finalColor;
    reflection = finalReflection;
}
