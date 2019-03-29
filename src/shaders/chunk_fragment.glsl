#version 300 es

precision highp float;
precision lowp sampler2DArray;

in vec2 v_TexCoordBlock;
flat in float v_blockID;
in float v_corner_lighting;
flat in int v_shouldDiscardNoLight;
in float v_planarDot;
uniform float u_shouldDiscardAlpha;
uniform sampler2DArray u_Texture;
uniform float u_ReflectionHeight;
flat in int v_needs_rotate;
uniform float u_ShowRotation;

layout( location = 0 ) out vec4 color;

void main( ) {
    if ( v_shouldDiscardNoLight == 1 ) {
        discard;
    }
    if ( v_planarDot < 0.0 && u_ReflectionHeight != 0.0 ) {
        discard;
    }
    vec2 working_fract = vec2( fract( v_TexCoordBlock.x ), fract( v_TexCoordBlock.y ) );
    vec2 working_int = vec2( v_TexCoordBlock.x - working_fract.x, v_TexCoordBlock.y - working_fract.y );

    int set_color = 0;
    int mod_sum = -1;
    if ( v_blockID == 0.0f ) {
        int x_mod = int( working_int.x ) % 32;
        int y_mod = int( working_int.y ) % 32;
        mod_sum = ( 27 * x_mod + y_mod + v_needs_rotate ) % 32;
        if ( mod_sum >= 8 && mod_sum < 16 ) {
            int offset = 1 - ( mod_sum % 2 );
            mod_sum = ( mod_sum + 2 * offset ) % 8;
        }
        if ( mod_sum >= 24 && mod_sum < 32 ) {
            mod_sum = 7 - ( mod_sum % 8 );
        }
        if ( mod_sum >= 16 && mod_sum < 24 ) {
            int offset = 4 - ( mod_sum % 5 );
            mod_sum = ( mod_sum + 2 * offset ) % 8;
        }

        if ( mod_sum == 0 ) {
            working_fract = vec2( 1.0f - working_fract.y, working_fract.x );
        } else if ( mod_sum == 1 ) {
            working_fract = vec2( 1.0f - working_fract.x, 1.0f - working_fract.y );
        } else if ( mod_sum == 2 ) {
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
    if ( float( mod_sum ) == u_ShowRotation ) {
        texColor.r *= 2.1f;
    }
    float corner_light = v_corner_lighting;
    color = texColor * vec4( corner_light, corner_light, corner_light, 1 );
}
