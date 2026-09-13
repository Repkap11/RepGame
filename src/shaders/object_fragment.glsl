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
uniform sampler2DArray u_SkyTexture;

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
        vec3 viewDir = normalize(v_world_coords.xyz - u_CameraPos);
        float skyTheta = atan(viewDir.z, viewDir.x);
        float skyU = fract(skyTheta / 6.28318531);
        vec3 dynamicFogColor = texture(u_SkyTexture, vec3(skyU, 0.5, 0.0)).rgb;
        if(u_DrawToReflection == 0) {
            finalColor.rgb = mix(finalColor.rgb, dynamicFogColor, colorFog);
            if(u_OpaqueFog == 1) {
                finalReflection.a = alphaFog;
                finalColor.a = 1.0;
            } else {
                finalColor.a *= (1.0 - alphaFog);
            }
        } else {
            finalReflection.rgb = mix(finalReflection.rgb, dynamicFogColor, colorFog);
            finalReflection.a *= (1.0 - alphaFog);
        }
    }

    color = finalColor;
    reflection = finalReflection;
}
