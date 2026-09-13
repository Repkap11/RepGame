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
uniform float u_RandomRotationBlocks[MAX_ROTATABLE_BLOCK];
uniform float u_ShowRotation;
uniform int u_TintUnderWater;
uniform float u_ReflectionDotSign;
uniform int u_DrawToReflection;
uniform float u_ExtraAlpha;
uniform int u_OpaqueFog;
uniform int u_AlphaToCoverage;

uniform vec3 u_FogColor;
uniform float u_FogNear;
uniform float u_FogFar;
uniform vec3 u_CameraPos;
uniform sampler2DArray u_SkyTexture;

in vec2 v_TexCoordBlock;
in float v_corner_lighting;
flat in float v_center_lighting;
in float v_planarDot;
in vec3 v_world_coords;

flat in uint v_blockID;
flat in int v_needs_rotate;
flat in int v_block_auto_rotates;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 reflection;
layout(location = 2) out vec4 fogFactor;

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
    // Alpha-tested passes (opaque + flowers): mipmaps average opaque pixels
    // (alpha=1) with transparent neighbours (alpha=0, RGB=0), which
    // premultiplies and lowers the alpha. Un-premultiply by dividing RGB
    // by alpha to recover the original opaque colour, then snap alpha to
    // 1.0 so surviving pixels render fully opaque (no sky bleed-through).
    if(u_shouldDiscardAlpha == 1.0f) {
        if(texColor.a < 0.1f) {
            discard;
        }
        texColor.rgb /= texColor.a;
        texColor.a = 1.0f;
    }
    // if ( float( mod_sum ) == u_ShowRotation ) {
    //     texColor.r *= 2.1f;
    // }
    if(u_ShowRotation != -2.0f) {
        texColor.rgb *= adjusted_face;
    }
    if(u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS || (u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && v_world_coords.y < (-0.125f - eps))) {
        texColor = mix(texColor, vec4(0.122f, 0.333f, 1.0f, 1.0f), 0.7f);
    }
    float corner_light = v_corner_lighting;
    // At grazing angles, perspective-correct interpolation of per-corner
    // lighting creates high-frequency brightness shimmer. Detect this
    // with fwidth (rate of change across neighboring pixels) and blend
    // toward the flat average only where the gradient is steep. Head-on
    // faces have near-zero fwidth and keep smooth corner interpolation.
    float lightWidth = fwidth(v_corner_lighting);
    float lightBlend = smoothstep(0.02f, 0.15f, lightWidth);
    corner_light = mix(corner_light, v_center_lighting, lightBlend);
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
    float dist = distance(v_world_coords, u_CameraPos);
    float fogLinear = clamp((dist - u_FogNear) / (u_FogFar - u_FogNear), 0.0f, 1.0f);
    // Color blend: reaches 100% fog color by 50% of the fog range.
    float colorFog = clamp(fogLinear / 0.5f, 0.0f, 1.0f);
    colorFog = colorFog * colorFog * (3.0f - 2.0f * colorFog);
    // Alpha fade: starts at 50% of the fog range, reaches full at the edge.
    float alphaFog = clamp((fogLinear - 0.5f) / 0.5f, 0.0f, 1.0f);
    alphaFog = alphaFog * alphaFog * (3.0f - 2.0f * alphaFog);

    // Sample the sky texture at the horizon (V=0.5) in the view direction
    // to get a fog color that matches the actual sky behind the terrain.
    vec3 viewDir = normalize(v_world_coords - u_CameraPos);
    float skyTheta = atan(viewDir.z, viewDir.x);
    float skyU = fract(skyTheta / 6.28318531f);
    vec3 dynamicFogColor = texture(u_SkyTexture, vec3(skyU, 0.5f, 0.0f)).rgb;

    if(u_DrawToReflection == 0) {
        finalColor.rgb = mix(finalColor.rgb, dynamicFogColor, colorFog);
        if(u_OpaqueFog == 1) {
            // Store fog factor in dedicated fog texture (location 2) for
            // post-process sky blending. Color alpha stays at 1.0 for
            // opaque terrain (no see-through to caves) and natural alpha for
            // water (blends with terrain behind it). With alpha-to-coverage
            // the natural alpha is kept so the hardware can convert it to
            // per-sample coverage; fog still reduces it at distance.
            fogFactor = vec4(0.0f, 0.0f, 0.0f, alphaFog);
            if(u_shouldDiscardAlpha == 1.0f) {
                // Opaque + alpha-tested foliage: keep alpha at 1.0 so the
                // post-process fog composite handles distance fade. With
                // alpha-to-coverage the per-sample coverage is already
                // determined by the natural alpha before this point, so
                // forcing alpha to 1.0 here doesn't undo the edge smoothing.
                finalColor.a = 1.0f;
            } else {
                finalColor.a *= (1.0f - alphaFog);
            }
        } else {
            // LOW quality: per-fragment alpha reduction.
            finalColor.a *= (1.0f - alphaFog);
            fogFactor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    } else {
        finalReflection.rgb = mix(finalReflection.rgb, dynamicFogColor, colorFog);
        finalReflection.a *= (1.0f - alphaFog);
        fogFactor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    color = finalColor;
    reflection = finalReflection;
}
