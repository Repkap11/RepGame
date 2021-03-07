#version 300 es
#define MAX_ROTATABLE_BLOCK 100u
#define eps 0.00001f
#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

precision highp float;
precision lowp sampler2DArray;

uniform float u_shouldDiscardAlpha;
uniform sampler2DArray u_Texture;
uniform float u_ReflectionHeight;
uniform float u_RandomRotationBlocks[ MAX_ROTATABLE_BLOCK ];
uniform float u_ShowRotation;
uniform int u_TintUnderWater;
uniform float u_ReflectionDotSign;
uniform int u_DrawToReflection;
uniform float u_ExtraAlpha;

in vec2 v_TexCoordBlock;
in float v_corner_lighting;
in float v_planarDot;
in vec3 v_world_coords;

flat in uint v_blockID;
flat in int v_shouldDiscardNoLight;
flat in int v_needs_rotate;
flat in int v_block_auto_rotates;

layout( location = 0 ) out vec4 color;
layout( location = 1 ) out vec4 reflection;

// layout( location = 1 ) out vec4 color;
// layout( location = 0 ) out vec4 reflection;

void main( ) {
    if ( v_shouldDiscardNoLight == 1 ) {
        discard;
    }
    if ( v_planarDot * u_ReflectionDotSign < 0.0 && u_ReflectionHeight != 0.0 ) {
        discard;
    }
    vec2 working_fract = vec2( fract( v_TexCoordBlock.x ), fract( v_TexCoordBlock.y ) );
    vec2 working_int = vec2( v_TexCoordBlock.x - working_fract.x, v_TexCoordBlock.y - working_fract.y );
    // working_fract = vec2( 1.0 - working_fract.x, working_fract.y );
    int mod_sum = -1;
    vec3 adjusted_face = vec3( 1, 1, 1 );
    if ( bool( v_block_auto_rotates ) ) {
        // if ( v_blockID == 0u ) {//Grass
        int x_mod = int( working_int.x ) % 32;
        int y_mod = int( working_int.y ) % 32;
        mod_sum = ( 27 * x_mod + y_mod + v_needs_rotate ) % 32;
        if ( mod_sum >= 8 && mod_sum < 16 ) {
            int offset = 1 - ( mod_sum % 2 );
            mod_sum = ( mod_sum + 2 * offset ) % 8;
        } else if ( mod_sum >= 24 && mod_sum < 32 ) {
            mod_sum = 7 - ( mod_sum % 8 );
        } else if ( mod_sum >= 16 && mod_sum < 24 ) {
            int offset = 4 - ( mod_sum % 5 );
            mod_sum = ( mod_sum + 2 * offset ) % 8;
        }

        if ( mod_sum == 0 ) {
            adjusted_face.r = 2.0f;
            working_fract = vec2( 1.0f - working_fract.y, working_fract.x );
        } else if ( mod_sum == 1 ) {
            adjusted_face.b = 2.0f;
            working_fract = vec2( 1.0f - working_fract.x, 1.0f - working_fract.y );
        } else if ( mod_sum == 2 ) {
            adjusted_face.g = 2.0f;
            working_fract = vec2( working_fract.y, 1.0f - working_fract.x );
        } else {
            working_fract = vec2( working_fract.x, working_fract.y );
        }
    }
    vec2 working = working_int + working_fract;

    vec4 texColor = texture( u_Texture, vec3( working, v_blockID ) );
    
    if ( texColor.a == 0.0 ) {
        discard;
    }
    if ( u_shouldDiscardAlpha == 1.0f && texColor.a < 0.7 ) {
        discard;
    }
    // if ( float( mod_sum ) == u_ShowRotation ) {
    //     texColor.r *= 2.1f;
    // }
    if ( u_ShowRotation != -2.0f ) {
        texColor.rgb *= adjusted_face;
    }
    if ( u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS || ( u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && v_world_coords.y < ( -0.125f - eps ) ) ) {
        texColor = mix( texColor, vec4( 0.122f, 0.333f, 1.0f, 1.0f ), 0.7f );
    }
    float corner_light = v_corner_lighting;
    vec4 lightedColor = texColor * vec4( corner_light, corner_light, corner_light, u_ExtraAlpha );
    
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
