#version 300 es
#define eps 0.00001f
#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;
uniform float u_Daylight;
uniform float u_MinAmbient;
uniform float u_ReflectionHeight;
uniform int u_TintUnderWater;
uniform int u_Underwater;
uniform int u_DrawToReflection;
uniform float u_ExtraAlpha;
uniform vec3 u_Origin;
// u_IsSky is set unconditionally by world.cpp (the sky draws through this
// shader on every tier), so it must be declared unconditionally too — the
// min-ambient floor uses it to keep the night sky from washing out.
uniform int u_IsSky;
#if !defined(REPGAME_LOW_GRAPHICS)
uniform int u_OpaqueFog;

uniform float u_FogNear;
uniform float u_FogFar;
uniform float u_WaterFogNear;
uniform float u_WaterFogFar;
uniform vec3 u_CameraPos;
uniform vec3 u_SkyAvgColor;
#endif

layout( location = 0 ) out vec4 color;
layout( location = 1 ) out vec4 reflection;
#if !defined(REPGAME_LOW_GRAPHICS)
layout( location = 2 ) out vec4 fogFactor;
layout( location = 3 ) out vec4 skyColor;
#endif

in vec2 v_tex_coords;
in float v_light;
// Per-instance flood-fill light (x=sky, y=block, 0-1), baked into instance
// data on the CPU. Unused by the sky (u_IsSky=1 takes the old path).
flat in vec2 v_light_field;
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
    if ( u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS || ( u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && v_world_coords.y < ( -0.125f - u_Origin.y - eps ) ) ) {
        texColor = mix( texColor, vec4( 0.122f, 0.333f, 1.0f, 1.0f ), 0.5f );
    }
    // Non-sky objects (mobs, debris) modulate their CPU-sampled flood-fill
    // light: the sky channel scales with the day/night factor, the block
    // channel (torches etc.) doesn't — same combine as chunk_fragment.
    // v_light is the per-face directional shade applied on top. u_MinAmbient
    // floors non-sky objects so unlit caves aren't invisible; the sky itself
    // keeps the old path (v_light * daylight) or the night sky would wash out.
    float scene_light;
    if ( u_IsSky == 0 ) {
        scene_light = v_light * max( max( v_light_field.x * u_Daylight, v_light_field.y ), u_MinAmbient );
    } else {
        scene_light = v_light * u_Daylight;
    }
    vec4 lightedColor = texColor * vec4( scene_light, scene_light, scene_light, u_ExtraAlpha );

    vec4 finalColor = lightedColor;
    vec4 finalReflection = lightedColor;
    if ( u_DrawToReflection == 1 ) {
        finalColor.a *= 0.0f;
    } else {
        finalReflection.a *= 0.0f;
    }

#if !defined(REPGAME_LOW_GRAPHICS)
    // Distance fog for non-sky objects (mobs/avatars).
    // Color reaches full fog color before alpha fade starts.
    // Fog color is sampled from the sky texture at the horizon.
    if(u_IsSky == 0) {
        float dist = distance(v_world_coords.xyz, u_CameraPos);
        // Underwater murk: same gates as the chunk shader — the camera's
        // medium picks the fog range, the fragment's position picks the tint.
        bool cameraUnderWater = u_TintUnderWater != TINT_UNDER_WATER_OBJECT_NEVER;
        bool belowWater = u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS ||
                          (u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && v_world_coords.y < (-0.125f - u_Origin.y - eps));
        float fogNear = cameraUnderWater ? u_WaterFogNear : u_FogNear;
        float fogFar = cameraUnderWater ? u_WaterFogFar : u_FogFar;
        float fogLinear = clamp((dist - fogNear) / (fogFar - fogNear), 0.0, 1.0);
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
                // Skip the alpha fade underwater — see the chunk shader: it
                // reveals the untinted sky and breaks alpha>0 validity checks.
                if(!belowWater) {
                    finalColor.a *= (1.0 - alphaFog);
                }
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

#endif
    // Underwater on tiers without the framebuffer composite: apply the blue
    // screen wash here — covers the sky too (it draws through this shader).
    if(u_Underwater != 0) {
        finalColor.rgb = mix(finalColor.rgb, vec3(0.04f, 0.16f, 0.55f), 0.45f);
    }
    color = finalColor;
    reflection = finalReflection;
}
