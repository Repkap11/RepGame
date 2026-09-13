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
uniform int u_OpaqueFog;

uniform vec3 u_FogColor;
uniform float u_FogNear;
uniform float u_FogFar;
uniform vec3 u_CameraPos;
uniform int u_IsSky;
uniform vec3 u_SkyAvgColor;

layout( location = 0 ) out vec4 color;
#if !defined(REPGAME_LOW_GRAPHICS)
layout( location = 1 ) out vec4 reflection;
layout( location = 2 ) out vec4 fogFactor;
layout( location = 3 ) out vec4 skyColor;
#endif

in vec2 v_tex_coords;
in float v_light;
flat in float v_blockID;
in float v_planarDot;
in vec4 v_world_coords;

void main( ) {
#if !defined(REPGAME_LOW_GRAPHICS)
    if ( v_planarDot < 0.0 && u_ReflectionHeight != 0.0 ) {
        discard;
    }
#endif
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
#if !defined(REPGAME_LOW_GRAPHICS)
    vec4 finalReflection = lightedColor;
    if ( u_DrawToReflection == 1 ) {
        finalColor.a *= 0.0f;
    } else {
        finalReflection.a *= 0.0f;
    }

    // Distance fog for non-sky objects (mobs/avatars).
    // Color reaches full fog color before alpha fade starts.
    // Fog color is sampled from the sky texture at the horizon.
    if(u_IsSky == 0) {
        float dist = distance(v_world_coords.xyz, u_CameraPos);
        float fogLinear = clamp((dist - u_FogNear) / (u_FogFar - u_FogNear), 0.0, 1.0);
        float colorFog = clamp(fogLinear / 0.5, 0.0, 1.0);
        colorFog = colorFog * colorFog * (3.0 - 2.0 * colorFog);
        float alphaFog = clamp((fogLinear - 0.5) / 0.5, 0.0, 1.0);
        alphaFog = alphaFog * alphaFog * (3.0 - 2.0 * alphaFog);
        vec3 dynamicFogColor = u_SkyAvgColor;
        if(u_DrawToReflection == 0) {
            // Color blend toward dynamicFogColor over first half, alpha fade
            // over second half. Fullscreen shader then blends toward actual
            // sky color using alphaFog.
            finalColor.rgb = mix(finalColor.rgb, dynamicFogColor, colorFog);
            if(u_OpaqueFog == 1) {
                fogFactor = vec4(0.0, 0.0, 0.0, alphaFog);
                finalColor.a = 1.0;
            } else {
                finalColor.a *= (1.0 - alphaFog);
                fogFactor = vec4(0.0, 0.0, 0.0, 0.0);
            }
        } else {
            finalReflection.rgb = mix(finalReflection.rgb, dynamicFogColor, colorFog);
            finalReflection.a *= (1.0 - alphaFog);
            fogFactor = vec4(0.0, 0.0, 0.0, 0.0);
        }
    }
    else {
        // Sky: fog factor 1.0 (fully "fogged" = fully sky). This ensures
        // MSAA-resolved edge pixels between sky and terrain have a high fog
        // factor, so the fullscreen shader blends them toward the sky color
        // instead of leaving a visible border.
        fogFactor = vec4(0.0, 0.0, 0.0, 1.0);
    }

    // The sky color attachment captures the actual rendered sky for exact
    // fog-blend matching. The sky writes its color; non-sky objects write
    // black (preserved by replace blending so the sky color stays).
    skyColor = (u_IsSky == 1 && u_DrawToReflection == 0) ? vec4(finalColor.rgb, 1.0) : vec4(0.0);

    reflection = finalReflection;
#endif
    color = finalColor;
}
