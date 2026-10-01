#version 300 es
#define MAX_ROTATABLE_BLOCK 100u
#define eps 0.00001f
// Toggle the fwidth-based lighting blend that fights shimmer at grazing
// angles. fwidth is a derivative instruction that costs register bandwidth
// and can lower occupancy on tile-based mobile GPUs. Disabled on LOW
// graphics platforms (Android/WASM) where other aliasing artifacts dominate
// and the ~8% fragment cost is not worth it.
#if !defined(REPGAME_LOW_GRAPHICS)
#define USE_FWIDTH_LIGHTING 1
#else
#define USE_FWIDTH_LIGHTING 0
#endif
#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

precision highp float;
precision lowp sampler2DArray;

uniform float u_shouldDiscardAlpha;
uniform sampler2DArray u_Texture;
uniform float u_ReflectionHeight;
uniform float u_RandomRotationBlocks[MAX_ROTATABLE_BLOCK];
uniform float u_ShowRotation;
uniform int u_TintUnderWater;
uniform int u_Underwater;
uniform float u_ReflectionDotSign;
uniform int u_DrawToReflection;
uniform float u_ExtraAlpha;
uniform vec3 u_Origin;
#if !defined(REPGAME_LOW_GRAPHICS)
uniform int u_OpaqueFog;

uniform float u_FogNear;
uniform float u_FogFar;
uniform float u_WaterFogNear;
uniform float u_WaterFogFar;
uniform vec3 u_CameraPos;
uniform vec3 u_SkyAvgColor;

// X_HIGH fluid effects: set only during the dedicated water pass (which draws
// both WATER and LAVA, RenderOrder_Water). u_Time is always 0 unless u_FluidAnim.
uniform float u_Time;
uniform int u_FluidAnim;
#endif

in vec2 v_TexCoordBlock;
in float v_corner_lighting;
#if !defined(REPGAME_LOW_GRAPHICS)
flat in float v_center_lighting;
#endif
in float v_planarDot;
in vec3 v_world_coords;

flat in uint v_blockID;
flat in int v_needs_rotate;
flat in int v_block_auto_rotates;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 reflection;
#if !defined(REPGAME_LOW_GRAPHICS)
layout(location = 2) out vec4 fogFactor;
layout(location = 3) out vec4 skyColor;
#endif

// layout( location = 1 ) out vec4 color;
// layout( location = 0 ) out vec4 reflection;

void main() {
    if(v_planarDot * u_ReflectionDotSign < 0.0f && u_ReflectionHeight != 0.0f) {
        discard;
    }
    vec2 working_fract = vec2(fract(v_TexCoordBlock.x), fract(v_TexCoordBlock.y));
    vec2 working_int = vec2(v_TexCoordBlock.x - working_fract.x, v_TexCoordBlock.y - working_fract.y);
    // working_fract = vec2( 1.0 - working_fract.x, working_fract.y );
    int mod_sum = -1;
    vec3 adjusted_face = vec3(1, 1, 1);
    if(bool(v_block_auto_rotates)) {
        // if ( v_blockID == 0u ) {//Grass
        int x_mod = int(working_int.x) % 32;
        int y_mod = int(working_int.y) % 32;
        mod_sum = (27 * x_mod + y_mod + v_needs_rotate) % 32;
        if(mod_sum >= 8 && mod_sum < 16) {
            int offset = 1 - (mod_sum % 2);
            mod_sum = (mod_sum + 2 * offset) % 8;
        } else if(mod_sum >= 24 && mod_sum < 32) {
            mod_sum = 7 - (mod_sum % 8);
        } else if(mod_sum >= 16 && mod_sum < 24) {
            int offset = 4 - (mod_sum % 5);
            mod_sum = (mod_sum + 2 * offset) % 8;
        }

        if(mod_sum == 0) {
            adjusted_face.r = 2.0f;
            working_fract = vec2(1.0f - working_fract.y, working_fract.x);
        } else if(mod_sum == 1) {
            adjusted_face.b = 2.0f;
            working_fract = vec2(1.0f - working_fract.x, 1.0f - working_fract.y);
        } else if(mod_sum == 2) {
            adjusted_face.g = 2.0f;
            working_fract = vec2(working_fract.y, 1.0f - working_fract.x);
        } else {
            working_fract = vec2(working_fract.x, working_fract.y);
        }
    }
    vec2 working = working_int + working_fract;

    vec4 texColor = texture(u_Texture, vec3(working, v_blockID));

    if(texColor.a == 0.0f) {
        discard;
    }
#if !defined(REPGAME_LOW_GRAPHICS)
    // X_HIGH animated fluids: scrolling + sine-warped UVs. Only runs during
    // the dedicated water pass (u_FluidAnim), which contains just water/lava.
    if(u_FluidAnim == 1) {
        vec2 wpos = v_world_coords.xz;
        if(v_blockID == 94u) { // WATER top texture (WATER block id - 1)
            vec2 flow = vec2(u_Time * 0.045f, u_Time * 0.032f);
            vec2 warp = vec2(sin(wpos.x * 1.7f + u_Time * 1.8f), sin(wpos.y * 1.5f + u_Time * 1.4f)) * 0.18f;
            vec4 c1 = texture(u_Texture, vec3(working + flow + warp, v_blockID));
            vec4 c2 = texture(u_Texture, vec3(working * 0.63f - flow * 1.3f - warp * 0.7f, v_blockID));
            texColor = mix(c1, c2, 0.5f);
            if(texColor.a == 0.0f) {
                discard;
            }
            // Fresnel: transparent looking straight down, opaque at grazing
            // angles. Water only — lava must stay opaque.
            vec3 viewDir = normalize(u_CameraPos - v_world_coords);
            float fresnel = 0.18f + 0.82f * pow(1.0f - clamp(abs(viewDir.y), 0.0f, 1.0f), 3.0f);
            // Push the water texel toward a deeper blue and keep most of the
            // surface coverage even looking straight down — the terrain blend
            // underneath reads as "transparent" otherwise.
            texColor.rgb = mix(texColor.rgb, vec3(0.04f, 0.18f, 0.58f), 0.78f);
            texColor.a *= 0.8f + 0.2f * fresnel;
        } else if(v_blockID == 93u) { // LAVA top texture
            vec2 flow = vec2(u_Time * 0.008f, u_Time * 0.011f);
            vec2 warp = vec2(sin(wpos.x * 0.9f + u_Time * 0.45f), sin(wpos.y * 1.1f + u_Time * 0.38f)) * 0.07f;
            texColor = texture(u_Texture, vec3(working + flow + warp, v_blockID));
            texColor.rgb *= 0.96f + 0.07f * sin(u_Time * 1.3f + (wpos.x + wpos.y) * 0.6f);
            if(texColor.a == 0.0f) {
                discard;
            }
        }
    }
#endif
    // Alpha-tested passes (opaque + flowers): mipmaps average opaque pixels
    // (alpha=1) with transparent neighbours (alpha=0, RGB=0), which
    // premultiplies and lowers the alpha. Un-premultiply by dividing RGB
    // by alpha to recover the original opaque colour, then snap alpha to
    // 1.0 so surviving pixels render fully opaque (no sky bleed-through).
#if defined(REPGAME_LOW_GRAPHICS)
    // Low graphics: simple early-out discard at 0.8 to reduce overdraw.
    if(u_shouldDiscardAlpha == 1.0f && texColor.a < 0.8f) {
        discard;
    }
#else
    if(u_shouldDiscardAlpha == 1.0f) {
        if(texColor.a < 0.1f) {
            discard;
        }
        texColor.rgb /= texColor.a;
        texColor.a = 1.0f;
    }
#endif
    // if ( float( mod_sum ) == u_ShowRotation ) {
    //     texColor.r *= 2.1f;
    // }
    if(u_ShowRotation != -2.0f) {
        texColor.rgb *= adjusted_face;
    }
    if(u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS || (u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && v_world_coords.y < (-0.125f - u_Origin.y - eps))) {
        texColor = mix(texColor, vec4(0.122f, 0.333f, 1.0f, 1.0f), 0.45f);
    }
    float corner_light = v_corner_lighting;
#if USE_FWIDTH_LIGHTING
    // At grazing angles, perspective-correct interpolation of per-corner
    // lighting creates high-frequency brightness shimmer. Detect this
    // with fwidth (rate of change across neighboring pixels) and blend
    // toward the flat average only where the gradient is steep. Head-on
    // faces have near-zero fwidth and keep smooth corner interpolation.
    float lightWidth = fwidth(v_corner_lighting);
    float lightBlend = smoothstep(0.02f, 0.15f, lightWidth);
    corner_light = mix(corner_light, v_center_lighting, lightBlend);
#endif
    vec4 lightedColor = texColor * vec4(corner_light, corner_light, corner_light, u_ExtraAlpha);

    vec4 finalColor = lightedColor;
    vec4 finalReflection = lightedColor;
    if(u_DrawToReflection == 1) {
        finalColor.a = 0.0f;
    } else {
        finalReflection.a = 0.0f;
    }

    // Distance fog: blend terrain color toward fog color AND reduce alpha.
    // The color blend reaches full fog color before the alpha fade starts,
    // so terrain is fully fog-colored before it blends with the sky.
    // The fog color is sampled from the sky texture at the horizon in the
    // view direction, so it matches the actual sky color behind the terrain.
#if !defined(REPGAME_LOW_GRAPHICS)
    float dist = distance(v_world_coords, u_CameraPos);
    // Underwater murk: the fog range is determined by the medium the
    // *camera* is in — with the head under water, even blocks above the
    // surface are viewed through water and fog over the short range.
    bool cameraUnderWater = u_TintUnderWater != TINT_UNDER_WATER_OBJECT_NEVER;
    // Per-fragment "below the surface" still gates the water tint and the
    // alpha-fade skip, so a straddled view keeps its waterline split.
    bool belowWater = u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS ||
                      (u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && v_world_coords.y < (-0.125f - u_Origin.y - eps));
    float fogNear = cameraUnderWater ? u_WaterFogNear : u_FogNear;
    float fogFar = cameraUnderWater ? u_WaterFogFar : u_FogFar;
    float fogLinear = clamp((dist - fogNear) / (fogFar - fogNear), 0.0f, 1.0f);
    // Color blend: reaches 100% fog color by 50% of the fog range.
    float colorFog = clamp(fogLinear / 0.5f, 0.0f, 1.0f);
    colorFog = colorFog * colorFog * (3.0f - 2.0f * colorFog);
    // Alpha fade: starts at 50% of the fog range, reaches full at the edge.
    float alphaFog = clamp((fogLinear - 0.5f) / 0.5f, 0.0f, 1.0f);
    alphaFog = alphaFog * alphaFog * (3.0f - 2.0f * alphaFog);

    // Use the pre-computed average sky color as the fog color.
    vec3 dynamicFogColor = u_SkyAvgColor;

    if(u_DrawToReflection == 0) {
        // Color blend: terrain → dynamicFogColor (average sky color) over
        // the first half of the fog range. Alpha fade over the second half.
        // In framebuffer mode, the fullscreen shader then blends the fog-colored
        // terrain toward the actual sky color (from the sky color attachment)
        // using alphaFog, so distant terrain seamlessly merges with the skybox.
        finalColor.rgb = mix(finalColor.rgb, dynamicFogColor, colorFog);
        if(u_OpaqueFog == 1) {
            // Store alphaFog in the fog texture for the post-process sky
            // blend (above water this is the sky; underwater it's the
            // water-range fog so the composite fades toward the sky
            // attachment's "seen through water" content).
            fogFactor = vec4(0.0f, 0.0f, 0.0f, alphaFog);
            // Underwater fragments skip the alpha fade: it was designed to
            // dissolve terrain into the sky, but underwater the murk comes
            // from the color blend and reduced alpha reveals untinted sky /
            // breaks alpha>0 validity checks (visible block edges). Below-water
            // translucent fragments (the water surface) keep a fraction of
            // their texture alpha so the sky and terrain above the surface
            // stay clearly visible through it.
            if(u_shouldDiscardAlpha == 1.0f) {
                finalColor.a = 1.0f;
            } else if(belowWater) {
                finalColor.a *= 0.5f;
            } else {
                finalColor.a *= (1.0f - alphaFog);
            }
        } else {
            // LOW quality / no framebuffer: per-fragment alpha reduction, no
            // post-process.
            if(!belowWater) {
                finalColor.a *= (1.0f - alphaFog);
            } else if(u_shouldDiscardAlpha != 1.0f) {
                // Same see-through surface as the framebuffer path, but only
                // for translucent fragments — opaque terrain keeps alpha 1.
                finalColor.a *= 0.5f;
            }
            fogFactor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    } else {
        finalReflection.rgb = mix(finalReflection.rgb, dynamicFogColor, colorFog);
        finalReflection.a *= (1.0f - alphaFog);
        fogFactor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // Chunks are never sky; write black to preserve the sky color attachment
    // (replace blending keeps the sky color written by the sky pass).
    skyColor = vec4(0.0);
#endif
    // Underwater on tiers without the framebuffer composite: apply the blue
    // screen wash here instead (the FSQ composite handles it otherwise).
    if(u_Underwater != 0) {
        finalColor.rgb = mix(finalColor.rgb, vec3(0.04f, 0.16f, 0.55f), 0.45f);
    }
    color = finalColor;
    reflection = finalReflection;
}
