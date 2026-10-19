#version 300 es
// Shares CHUNK_SIZE_*/CHUNK_SIZE_INTERNAL_* with the C++ side so the light
// volume divisor below always matches the compiled chunk geometry.
#include "common/chunk_dimensions.hpp"
#define MAX_ROTATABLE_BLOCK 100u
#define eps 0.00001f
// Edge length of the per-chunk light volume in texels (interior + 1-cell halo).
// Per-axis: internal dims need not be equal (e.g. X/Z=128, Y=64).
#define LIGHT_VOLUME_DIM vec3( float( CHUNK_SIZE_INTERNAL_X ), float( CHUNK_SIZE_INTERNAL_Y ), float( CHUNK_SIZE_INTERNAL_Z ) )
// Hard cap on the mip level sampled from the light volume (see light_lod).
#define LIGHT_LOD_MAX 1.0
#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

precision highp float;
precision lowp sampler2DArray;
precision lowp sampler3D;

uniform float u_shouldDiscardAlpha;
uniform sampler2DArray u_Texture;
// Flood-fill lighting: per-chunk internal-volume texture (R=sky, G=block,
// both pre-scaled to 0-1), bound to a shared texture unit per chunk draw.
// GL_RG8 when light mipmaps are off, GL_RGBA8 (B = air mask) when on —
// see light_mipmaps_enabled() in light.cpp.
// u_LightBase = chunk block-origin in u_Origin-rebased coords, u_Daylight
// scales the sky channel for day/night, u_MinAmbient is a light floor so
// unlit caves are never fully black.
uniform sampler3D u_LightTex;
uniform vec3 u_LightBase;
uniform float u_Daylight;
uniform float u_MinAmbient;
uniform float u_ReflectionHeight;
uniform float u_RandomRotationBlocks[MAX_ROTATABLE_BLOCK];
uniform float u_ShowRotation;
uniform int u_TintUnderWater;
uniform int u_Underwater;
uniform float u_ReflectionDotSign;
uniform int u_DrawToReflection;
uniform float u_ExtraAlpha;
uniform vec3 u_Origin;
uniform int u_OpaqueFog;

uniform float u_FogNear;
uniform float u_FogFar;
uniform float u_WaterFogNear;
uniform float u_WaterFogFar;
uniform vec3 u_CameraPos;
uniform vec3 u_SkyAvgColor;
// Sky texture (equirect sphere map, layer 0): lets the no-framebuffer fog
// path blend toward the actual sky color behind each fragment instead of
// fading alpha toward an unknown backdrop.
uniform sampler2DArray u_SkyTexture;

// X_HIGH fluid effects: set only during the dedicated water pass (which draws
// both WATER and LAVA, RenderOrder_Water). u_Time is always 0 unless u_FluidAnim.
uniform float u_Time;
uniform int u_FluidAnim;

// Capture-mode A/B debug toggles (REPGAME_CAPTURE per-view flags bitmask);
// always 0 in normal play.
uniform int u_DebugFlags;

// Footprint-mipmapped light (see light_lod below): LOD bias (higher = blur
// the per-block light pattern at closer range) and how much of the solid-
// inclusive average to keep as smooth distant AO (0 = air-only average).
uniform float u_LightLodBias;
uniform float u_LightFarAO;
// Cutout-foliage smoothing window in texel lods (0 = 1 texel/pixel,
// 3 = 8 texels/pixel): x = lod where the hashed alpha test starts fading
// toward solid, y = lod where it is fully solid. Env-tunable via
// REPGAME_CUTOUT_FADE_START / REPGAME_CUTOUT_FADE_END.
uniform vec2 u_CutoutFade;

in vec2 v_TexCoordBlock;
flat in vec2 v_tex_min;
flat in vec2 v_tex_max;
in float v_face_light;
in float v_planarDot;
in vec3 v_world_coords;
flat in vec3 v_face_normal;

flat in uint v_blockID;
flat in int v_block_auto_rotates;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 reflection;
layout(location = 2) out vec4 fogFactor;
layout(location = 3) out vec4 skyColor;

// layout( location = 1 ) out vec4 color;
// layout( location = 0 ) out vec4 reflection;

void main() {
    // Mip/aniso selection must use derivatives of the continuous quad UV:
    // the per-cell random rotation below makes `working` jump at every block
    // boundary inside a greedy-merged quad, and implicit derivatives across
    // that jump pick a near-1x1 mip — a grid of wrong texels that aliases
    // in the distance.
    vec2 uv_dx = dFdx(v_TexCoordBlock);
    vec2 uv_dy = dFdy(v_TexCoordBlock);
    if(v_planarDot * u_ReflectionDotSign < 0.0f && u_ReflectionHeight != 0.0f) {
        discard;
    }
    vec2 working_fract = vec2(fract(v_TexCoordBlock.x), fract(v_TexCoordBlock.y));
    vec2 working_int = vec2(v_TexCoordBlock.x - working_fract.x, v_TexCoordBlock.y - working_fract.y);
    // working_fract = vec2( 1.0 - working_fract.x, working_fract.y );
    int mod_sum = -1;
    vec3 adjusted_face = vec3(1, 1, 1);
    // u_DebugFlags bit 4: random rotation off (gradients stay unrotated too).
    if(bool(v_block_auto_rotates) && (u_DebugFlags & 4) == 0) {
        // Hash the world cell this face fragment covers so the rotation is a
        // pure function of position — the same no matter how greedy meshing
        // merged the block into quads. Stepping half a cell against the face
        // normal lands inside the block the face belongs to. (The old scheme
        // hashed the merged quad's origin in the vertex shader and corrected
        // per-texel with 27*tile_x + tile_y, which only reproduces the world
        // hash for top/bottom faces merged along x/z — side faces merged
        // along y/z rotated differently depending on run placement.)
        vec3 cell = floor(v_world_coords + u_Origin - v_face_normal * 0.5);
        mod_sum = int(mod(27.0 * mod(cell.x, 32.0) + 3.0 * mod(cell.y, 32.0) + mod(cell.z, 32.0), 32.0));
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
            uv_dx = vec2(-uv_dx.y, uv_dx.x);
            uv_dy = vec2(-uv_dy.y, uv_dy.x);
        } else if(mod_sum == 1) {
            adjusted_face.b = 2.0f;
            working_fract = vec2(1.0f - working_fract.x, 1.0f - working_fract.y);
            uv_dx = -uv_dx;
            uv_dy = -uv_dy;
        } else if(mod_sum == 2) {
            adjusted_face.g = 2.0f;
            working_fract = vec2(working_fract.y, 1.0f - working_fract.x);
            uv_dx = vec2(uv_dx.y, -uv_dx.x);
            uv_dy = vec2(uv_dy.y, -uv_dy.x);
        } else {
            working_fract = vec2(working_fract.x, working_fract.y);
        }
    }
    vec2 working = working_int + working_fract;
    // Edge-clamp inside this face's texture region (the [v_tex_min, v_tex_max]
    // span): bilinear taps at the region boundary would blend in texels from
    // the neighboring band of the atlas layer — for tiles drawn with
    // transparent borders (slab sides, chest edges) that drops alpha below
    // the discard threshold and leaks sky as a hairline between faces.
    // Interior texels are untouched; only the outermost half-texel is pinned.
    vec2 working_lo = v_tex_min + (0.5f / 16.0f);
    vec2 working_hi = max(v_tex_max - (0.5f / 16.0f), working_lo);
    working = clamp(working, working_lo, working_hi);

    vec4 texColor = textureGrad(u_Texture, vec3(working, v_blockID), uv_dx, uv_dy);
    // u_DebugFlags bit 2: texture detail off — resample at a coarse mip.
    if((u_DebugFlags & 2) != 0) {
        texColor.rgb = textureLod(u_Texture, vec3(working, v_blockID), 4.0).rgb;
    }

    if(texColor.a == 0.0f) {
        discard;
    }
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
    // Alpha-tested passes (opaque + flowers): hashed alpha test. Cutout
    // tiles' mips store box-averaged alpha (texture.cpp), so sampled alpha is
    // the coverage of the filtered footprint; surviving iff alpha beats a
    // per-texel hash makes rendered coverage a *linear* function of it —
    // correct at every mip level with no threshold cliff. A fixed cutoff
    // would instead snap a texel in/out as soon as the blend crosses it,
    // banding between mips. The hash is quantized to a footprint-scaled grid
    // (16/2^lod cells per tile) + block cell: at mip 0 each authored texel is
    // a coin flip, and deeper mips keep
    // flip granularity ~pixel-sized, so marginal texels fail as fine noise
    // rather than whole authored texels popping on/off as the footprint
    // slides — a fixed authored grid makes distant flips cover many pixels,
    // which is exactly the distant-canopy shimmer. A 0.1 floor keeps
    // genuinely-invisible texels from sparkling.
    // u_DebugFlags bit 128: plain 0.1 threshold, no dither — the old look.
    // highp on every hash operand is REQUIRED: without it uint defaults to
    // mediump, and on Mesa/AMD fragment shaders that means 16-bit math — the
    // constants wrap mod 2^16, the hash collapses to ~0, and the test silently
    // degenerates to the fixed 0.1 cutoff (and the h2 dither below pins at its
    // darkest factor, dimming all cutout foliage ~40%).
    if(u_shouldDiscardAlpha == 1.0f) {
        float sampled_a = texColor.a;
        float cutoff = 0.1f;
        highp uint h = 0u;
        float texel_lod = 0.0f;
        float cutout_smooth = 0.0f;
        if((u_DebugFlags & 128) == 0) {
            vec3 cellf = floor(v_world_coords + u_Origin - v_face_normal * 0.5f);
            texel_lod = log2(max(16.0f * max(length(uv_dx), length(uv_dy)), 1.0f));
            float grid = 16.0f / exp2(clamp(texel_lod, 0.0f, 5.0f));
            highp ivec2 texel_i = ivec2(floor(fract(working) * grid));
            highp uvec3 ci = uvec3(ivec3(cellf));
            highp uint bid = v_blockID;
            h = (ci.x * 747796405u) ^ (ci.y * 2891336453u) ^ (ci.z * 277803737u) ^
                (uint(texel_i.x) * 1013904223u) ^ (uint(texel_i.y) * 1664525u) ^
                (bid * 22695477u);
            h ^= h >> 15u;
            h *= 747796405u;
            h ^= h >> 16u;
            cutoff = max(float(h) * (1.0f / 4294967296.0f), 0.1f);
            // Converge to solid over the u_CutoutFade lod window: once a leaf
            // texel is a fraction of a pixel the holes can't be resolved, and
            // stochastic 60% coverage renders as salt-and-pepper noise.
            // Fading the cutoff to zero fills the canopy back in — the mip's
            // filtered rgb is already a smooth leaf green, so distant foliage
            // reads as a solid canopy instead of high-frequency speckle.
            cutout_smooth = clamp((texel_lod - u_CutoutFade.x) / max(u_CutoutFade.y - u_CutoutFade.x, 0.01f), 0.0f, 1.0f);
            cutoff *= 1.0f - cutout_smooth;
        }
        if(sampled_a < cutoff) {
            discard;
        }
        texColor.a = 1.0f;
        if((u_DebugFlags & 128) == 0 && (u_DebugFlags & 256) == 0) {
            // Re-inject the authored-scale brightness dither that linear
            // filtering can't carry: a cutout tile's texel-to-texel
            // variation is at the texture's Nyquist limit, so it dies in the
            // first bilinear blend no matter what the mips contain — that
            // loss is the visible mip boundary on leaf canopies. Modulate
            // rgb by a second hash on the same (cell, footprint-texel) key so
            // the speckle is stable, block-sized and identical at every mip
            // level. Ramp in with texel lod (authored dither still wins at
            // mip 0) and only where sampled alpha < 1 marks the tile as
            // cutout-ish, so solid tiles keep their plain filtered colour.
            highp uint h2 = h * 2246822519u;
            h2 ^= h2 >> 13u;
            h2 *= 3266489917u;
            h2 ^= h2 >> 16u;
            float inject = clamp(texel_lod, 0.0f, 1.0f) * (sampled_a < 0.999f ? 1.0f : 0.0f);
            // Log-symmetric range: a plain 0.55-1.45 multiplier is
            // perceptually dark-skewed in sRGB (halving brightness is far
            // more salient than +45%), which dropped the distant canopy's
            // mean ~20% below the authored look. Geometric mean 1.0 keeps
            // dark and bright texels balanced. Amplitude fades out over the
            // u_CutoutFade window alongside the alpha test: once the canopy
            // converges to solid, per-texel brightness noise reads as shimmer
            // rather than leaf detail, so it settles to the filtered mean on
            // the same schedule.
            float amp = 0.8f * (1.0f - cutout_smooth);
            texColor.rgb *= mix(1.0f, exp2(amp * (2.0f * float(h2) * (1.0f / 4294967296.0f) - 1.0f)), inject);
        }
    }
    // if ( float( mod_sum ) == u_ShowRotation ) {
    //     texColor.r *= 2.1f;
    // }
    if(u_ShowRotation != -2.0f) {
        texColor.rgb *= adjusted_face;
    }
    if(u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS || (u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && v_world_coords.y < (-0.125f - u_Origin.y - eps))) {
        texColor = mix(texColor, vec4(0.122f, 0.333f, 1.0f, 1.0f), 0.45f);
    }

    // Flood-fill light: v_world_coords + 0.5*normal lands on the center of
    // the air cell this face points at. +1.0 is the halo offset mapping
    // world->internal texel space (cell centers already sit at +0.5, and texel
    // centers are sampled at (i+0.5)/LIGHT_VOLUME_DIM). GL_LINEAR then
    // interpolates along the face plane for smooth gradients across merged quads.
    vec3 light_coord = (v_world_coords + v_face_normal * 0.5 - u_LightBase + 1.0) / LIGHT_VOLUME_DIM;
    // Two independent channels (r=sky, g=block, both already 0-1): keeping
    // them separate matters because GL_LINEAR interpolates raw texel values —
    // a packed byte would cross-contaminate the nibbles mid-gradient.
    // Level 0 is the exact per-cell light (implicit LOD would now pick mips).
    vec2 light_sample = textureLod(u_LightTex, light_coord, 0.0).rg;
    // Band-limit the light to the pixel footprint: once a block spans less
    // than ~2 pixels (distance, edge-on views) the per-block light pattern
    // (AO-like darkening toward solid cells, stepped seabeds seen through
    // water) aliases into moire. The volume is mipmapped with B = air
    // fraction (cells that can hold light, same predicate as propagation —
    // dark air counts, solid doesn't), so rg / b is the average light over
    // the *air* cells in the
    // footprint — solid cells don't drag it dark. u_LightFarAO adds a small
    // deliberate bias toward the raw (solid-inclusive) average. It is kept
    // small on purpose: the air fraction of a surface-straddling texel
    // reflects where the texel landed as much as any real occlusion, so
    // blending toward it darkens even flat unoccluded ground.
    // u_DebugFlags bit 32 disables this for A/B captures.
    // Footprint = geometric mean of the two screen axes (sqrt of footprint
    // area). The major axis alone explodes on surfaces seen edge-on even up
    // close (e.g. a cave ceiling at eye level), picking a mip that averages
    // light from outside the cave across the wall.
    // When light mipmaps are off (REPGAME_LIGHT_MIPMAPS=0 or the platform
    // default), the C++ side sets u_LightLodBias to -1000 so light_lod never
    // exceeds 0 and this whole branch is skipped — an RG8 non-mipmapped
    // volume would return level 0 with b=0 anyway (no visual change); the
    // bias just saves the extra 3D fetch.
    // The LOD is capped at LIGHT_LOD_MAX: coarse mips break twice over
    // because the volume is per-chunk. CHUNK_SIZE (62) is not a power of
    // two, so mip level L puts each chunk on a texel grid shifted by
    // (62k + 1) mod 2^L — identical across chunks only for L <= 1 — and
    // neighbouring chunks band-limit on differently aligned grids, landing
    // hard steps on chunk boundaries. The cap also bounds how much light a
    // coarse texel can smear into a sealed volume: with an honest air mask
    // a deep cave wall still evaluates to ~0 through level 4 and only leaks
    // at level 5, so the cap stays at or below that.
    float light_footprint = sqrt(length(dFdx(v_world_coords)) * length(dFdy(v_world_coords)));
    float light_lod = min(log2(max(light_footprint, 1e-4)) + u_LightLodBias, LIGHT_LOD_MAX);
    if(light_lod > 0.0 && (u_DebugFlags & 32) == 0) {
        vec3 light_mip = textureLod(u_LightTex, light_coord, light_lod).rgb;
        vec2 light_air = light_mip.rg / max(light_mip.b, 1.0 / 255.0);
        vec2 light_far = min(mix(light_air, light_mip.rg, u_LightFarAO), vec2(1.0));
        light_sample = mix(light_sample, light_far, clamp(light_lod, 0.0, 1.0));
    }
    // u_MinAmbient floors the result: cells with no skylight and no block
    // light (deep caves) still get a faint ambient instead of pure black.
    float scene_light = max(max(light_sample.r * u_Daylight, light_sample.g), u_MinAmbient);
    // u_DebugFlags bit 1: lighting off.
    if((u_DebugFlags & 1) != 0) {
        scene_light = 1.0;
    }
    // u_DebugFlags bit 16: per-face shading off.
    float face_light_used = (u_DebugFlags & 16) != 0 ? 1.0 : v_face_light;
    vec4 lightedColor = texColor * vec4(face_light_used * scene_light, face_light_used * scene_light, face_light_used * scene_light, u_ExtraAlpha);

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
    // u_DebugFlags bit 8: fog off.
    if((u_DebugFlags & 8) != 0) {
        colorFog = 0.0;
        alphaFog = 0.0;
    }

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
            // R carries colorFog so the water reflection overlay can apply
            // the same first-stage (sky average) blend as the surface.
            fogFactor = vec4(colorFog, 0.0f, 0.0f, alphaFog);
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
            // No framebuffer: the sky is a static equirect-textured sphere
            // centered on the camera, so the exact sky color behind this
            // fragment is computable here (inverse of sky_box.cpp's mapping:
            // y = -cos(phi), x/z = cos/sin(theta) * sin(phi)). Blend toward
            // it opaquely — alpha stays 1 so overlapping geometry (tree
            // canopies) can't accumulate transparency and darken, and there
            // is no sky-mismatch silhouette at the fog edge.
            if(!belowWater) {
                vec3 dir = normalize(v_world_coords - u_CameraPos);
                vec2 skyUV = vec2(fract(atan(dir.z, dir.x) / 6.2831853f),
                                  acos(clamp(-dir.y, -1.0f, 1.0f)) / 3.14159265f);
                // Equirect seam: u wraps 1->0 across the atan2 discontinuity,
                // so implicit LOD picks the coarsest mip for one pixel column.
                // Wrap the gradient deltas the short way around instead.
                vec2 skyUVdx = dFdx(skyUV);
                vec2 skyUVdy = dFdy(skyUV);
                skyUVdx.x -= floor(skyUVdx.x + 0.5f);
                skyUVdy.x -= floor(skyUVdy.x + 0.5f);
                // Same dimming as the sky pass: sky v_light is 1 (FACE_TOP).
                vec3 skyHere = textureGrad(u_SkyTexture, vec3(skyUV, 0.0), skyUVdx, skyUVdy).rgb * u_Daylight;
                // Match the sky pass's water tint (object_fragment): the dome
                // is blue-mixed while the camera is in water — wholly when
                // deep, below the waterline when straddling the surface (the
                // sky pass tests sky_y < -0.125; dir.y < 0 is the same horizon
                // plane to within a fraction of a degree).
                if(u_TintUnderWater == TINT_UNDER_WATER_OBJECT_ALWAYS ||
                   (u_TintUnderWater == TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL && dir.y < 0.0f)) {
                    skyHere = mix(skyHere, vec3(0.122f, 0.333f, 1.0f), 0.5f);
                }
                // Same effective rate as the framebuffer path: there the
                // alpha fade blends alphaFog over the sky attachment, then
                // the composite blends toward it again by sqrt(alphaFog).
                float skyFade = 1.0f - (1.0f - alphaFog) * (1.0f - sqrt(alphaFog));
                finalColor.rgb = mix(finalColor.rgb, skyHere, skyFade);
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
    // Underwater on tiers without the framebuffer composite: apply the blue
    // screen wash here instead (the FSQ composite handles it otherwise).
    if(u_Underwater != 0) {
        finalColor.rgb = mix(finalColor.rgb, vec3(0.04f, 0.16f, 0.55f), 0.45f);
    }
    color = finalColor;
    reflection = finalReflection;
}
