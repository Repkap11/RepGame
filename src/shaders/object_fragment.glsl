#version 300 es
#define eps 0.00001f
#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2
// Same warm artificial-light tint as chunk_fragment.glsl — keep in sync.
#define BLOCK_LIGHT_TINT vec3(1.0, 0.90, 0.75)

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
uniform int u_OpaqueFog;
// Set only on the item-sprite program: sample exact atlas texels (texelFetch,
// lod 0) so the extrusion slices alpha-test per texel without mipmap bleed.
uniform int u_ExactTexel;

uniform float u_FogNear;
uniform float u_FogFar;
uniform float u_WaterFogNear;
uniform float u_WaterFogFar;
uniform vec3 u_CameraPos;
uniform vec3 u_SkyAvgColor;
// Same as chunk_fragment: fog target for the submerged leg of the view ray.
uniform vec3 u_WaterFogColor;
// Camera dimension's sea surface in absolute world y. The sky keeps the
// -0.125 view-space horizon instead: its dome is camera-centred (u_Origin=0),
// so "below -0.125" means below the eye line in every dimension.
uniform float u_SeaSurfaceY;
#define SEA_PLANE_Y ( ( u_IsSky == 1 ? -0.125f : u_SeaSurfaceY ) - u_Origin.y )

layout( location = 0 ) out vec4 color;
layout( location = 1 ) out vec4 reflection;
layout( location = 2 ) out vec4 fogFactor;
layout( location = 3 ) out vec4 skyColor;

in vec2 v_tex_coords;
// mob_vertex clamps the sample to the face's skin rect inset by half a texel
// (same edge-clamp idea as chunk_fragment's v_tex_min/v_tex_max); the other
// programs sharing this fragment output a no-op bound.
flat in vec4 v_tex_bounds;
in float v_light;
// Per-instance flood-fill light (x=sky, y=block, 0-1), baked into instance
// data on the CPU. Unused by the sky (u_IsSky=1 takes the old path).
flat in vec2 v_light_field;
flat in float v_blockID;
// Primed TNT drives this from its fuse clock (primed_tnt_vertex); other
// programs sharing this fragment output 0.
flat in float v_flash;
in float v_planarDot;
in vec4 v_world_coords;

void main( ) {
    if ( v_planarDot < 0.0 && u_ReflectionHeight != 0.0 ) {
        discard;
    }
    vec2 uv = clamp( v_tex_coords, v_tex_bounds.xy, v_tex_bounds.zw );
    vec4 texColor;
    if ( u_ExactTexel != 0 ) {
        texColor = texelFetch( u_Texture, ivec3( clamp( ivec2( floor( uv * 16.0 ) ), 0, 15 ), int( v_blockID + 0.5 ) ), 0 );
    } else {
        texColor = texture( u_Texture, vec3( uv, v_blockID ) );
    }
    // vec4 texColor = vec4( 1, 0, 0, 1 );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    // Flash overlay: positive is the white primed-TNT blink, negative is the
    // red mob hurt tint (mob_vertex).
    if ( v_flash < 0.0 ) {
        texColor.rgb = mix( texColor.rgb, vec3( 1.0, 0.0, 0.0 ), -v_flash );
    } else {
        texColor.rgb = mix( texColor.rgb, vec3( 1.0 ), v_flash );
    }
    // Same water-side tint as chunk_fragment: full mix inside the medium,
    // scaled by the water-column crossing for above-surface geometry seen
    // from a submerged camera — a mob on land still reads through water.
    if ( u_TintUnderWater != TINT_UNDER_WATER_OBJECT_NEVER ) {
        float tintAmt = 0.5f;
        if ( v_world_coords.y >= ( SEA_PLANE_Y - eps ) ) {
            float surfY = SEA_PLANE_Y;
            float loY = min( u_CameraPos.y, v_world_coords.y );
            float hiY = max( u_CameraPos.y, v_world_coords.y );
            float crossFrac = ( clamp( surfY, loY, hiY ) - loY ) / max( hiY - loY, eps );
            float crossLen = distance( v_world_coords.xyz, u_CameraPos ) * crossFrac;
            tintAmt *= clamp( crossLen / u_WaterFogNear, 0.0f, 1.0f );
        }
        texColor = mix( texColor, vec4( 0.122f, 0.333f, 1.0f, 1.0f ), tintAmt );
    }
    // Non-sky objects (mobs, debris) modulate their CPU-sampled flood-fill
    // light: the sky channel scales with the day/night factor, the block
    // channel (torches etc.) doesn't and is tinted warm — same combine as
    // chunk_fragment. v_light is the per-face directional shade applied on
    // top. u_MinAmbient linearly lifts non-sky objects so unlit caves
    // aren't invisible; the sky itself keeps the old path
    // (v_light * daylight) or the night sky would wash out.
    vec3 scene_light;
    if ( u_IsSky == 0 ) {
        vec3 lit = max( vec3( v_light_field.x * u_Daylight ), BLOCK_LIGHT_TINT * v_light_field.y );
        scene_light = v_light * mix( vec3( u_MinAmbient ), vec3( 1.0 ), lit );
    } else {
        scene_light = vec3( v_light * u_Daylight );
    }
    vec4 lightedColor = texColor * vec4( scene_light, u_ExtraAlpha );

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
        // Same medium-split fog as chunk_fragment: the air range runs on the
        // above-surface leg of the view ray, the water range on the
        // submerged leg. u_TintUnderWater still gates the tint/alpha
        // handling, not the fog range.
        float surfY = SEA_PLANE_Y;
        float loY = min(u_CameraPos.y, v_world_coords.y);
        float hiY = max(u_CameraPos.y, v_world_coords.y);
        float waterPath = dist * clamp((clamp(surfY, loY, hiY) - loY) / max(hiY - loY, eps), 0.0f, 1.0f);
        // Same gate as chunk_fragment: a camera below the sea plane in air
        // (dry caves, the nether) isn't looking through water.
        bool cameraUnderWater = u_TintUnderWater != TINT_UNDER_WATER_OBJECT_NEVER;
        if(!cameraUnderWater && u_CameraPos.y < surfY) {
            waterPath = 0.0f;
        }
        float airPath = dist - waterPath;
        float airFogLinear = clamp((airPath - u_FogNear) / (u_FogFar - u_FogNear), 0.0, 1.0);
        // Same two-way column as chunk_fragment: a dry camera sees light
        // that crossed the water down and back, so the leg counts double.
        float waterPathEff = (!cameraUnderWater && u_CameraPos.y >= surfY) ? waterPath * 2.0f : waterPath;
        float waterFogLinear = clamp((waterPathEff - u_WaterFogNear) / (u_WaterFogFar - u_WaterFogNear), 0.0, 1.0);
        float fogLinear = 1.0 - (1.0 - airFogLinear) * (1.0 - waterFogLinear);
        float waterShare = waterFogLinear / max(fogLinear, 0.001);
        // Same surface-crossing occlusion as chunk_fragment: above-surface
        // geometry seen from inside converges to murk, easing at the zenith.
        float aboveOcc = 0.0f;
        if(cameraUnderWater && v_world_coords.y > surfY + eps) {
            float cosExit = (v_world_coords.y - u_CameraPos.y) / max(dist, eps);
            aboveOcc = mix(1.0f, 0.6f, smoothstep(0.6f, 0.9f, cosExit));
            waterShare = 1.0f;
        }
        // Same edge convergence as chunk_fragment: the murk target yields
        // to the sky over the second half of the air fog band.
        float airFade = clamp((airFogLinear - 0.3f) / 0.4f, 0.0f, 1.0f);
        waterShare *= 1.0f - airFade * airFade * (3.0f - 2.0f * airFade);
        bool belowWater = u_TintUnderWater != TINT_UNDER_WATER_OBJECT_NEVER &&
                          v_world_coords.y < (surfY - eps);
        // Same as chunk_fragment: a dry camera's water murk ramps over the
        // whole band; everything else saturates by half range.
        float colorHalf = (!cameraUnderWater && u_CameraPos.y >= surfY) ? mix(0.5f, 1.0f, waterShare) : 0.5f;
        float colorFog = clamp(fogLinear / colorHalf, 0.0, 1.0);
        colorFog = colorFog * colorFog * (3.0 - 2.0 * colorFog);
        // Same TIR occlusion as chunk_fragment — the mirror is not
        // absorption, so it isn't paced by the fog distance.
        colorFog = max(colorFog, aboveOcc);
        float alphaFog = clamp((fogLinear - 0.5) / 0.5, 0.0, 1.0);
        alphaFog = alphaFog * alphaFog * (3.0 - 2.0 * alphaFog);
        vec3 dynamicFogColor = mix(u_SkyAvgColor, u_WaterFogColor, waterShare);
        if(u_DrawToReflection == 0) {
            // Color blend toward dynamicFogColor over first half, alpha fade
            // over second half. Fullscreen shader then blends toward actual
            // sky color using alphaFog.
            finalColor.rgb = mix(finalColor.rgb, dynamicFogColor, colorFog);
            if(u_OpaqueFog == 1) {
                // B = water share, same as chunk_fragment: the composite
                // picks the sky vs murk blend target per pixel.
                fogFactor = vec4(colorFog, 0.0, waterShare, alphaFog);
                // Opaque fragments pin alpha to 1 — the fog attachment owns
                // the distance fade. Translucent texels (held/dropped stained
                // glass) keep their coverage alpha so draw-time blending
                // still shows through, matching the chunk shader's
                // translucent pass: halved underwater so alpha>0 validity
                // checks keep working, fog-faded otherwise.
                if(finalColor.a >= 1.0f) {
                    finalColor.a = 1.0;
                } else if(belowWater) {
                    finalColor.a *= 0.5f;
                } else {
                    finalColor.a *= (1.0f - alphaFog);
                }
            } else {
                // Skip the alpha fade underwater — see the chunk shader: it
                // reveals the untinted sky and breaks alpha>0 validity checks.
                // Above-surface fragments seen from inside the water need it
                // off too: the surface crossing already occludes them via
                // colorFog, and fading to alpha 0 shows the bright sky
                // backdrop through the silhouette instead.
                if(!belowWater && !(cameraUnderWater && v_world_coords.y > surfY + eps)) {
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
        fogFactor = vec4(1.0, 0.0, 0.0, 1.0);
    }

    // The sky color attachment captures the actual rendered sky for exact
    // fog-blend matching. The sky writes its color; non-sky objects write
    // black (preserved by replace blending so the sky color stays).
    skyColor = (u_IsSky == 1 && u_DrawToReflection == 0) ? vec4(finalColor.rgb, 1.0) : vec4(0.0);

    // Underwater on tiers without the framebuffer composite: apply the blue
    // screen wash here — covers the sky too (it draws through this shader).
    if(u_Underwater != 0) {
        finalColor.rgb = mix(finalColor.rgb, vec3(0.04f, 0.16f, 0.55f), 0.45f);
    }
    color = finalColor;
    reflection = finalReflection;
}
