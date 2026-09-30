#version 400

precision highp float;

layout(location = 0) out vec4 color;

uniform float u_ExtraAlpha;
uniform int u_Blur;
uniform int u_FogBlend;
uniform int u_DiscardZeroAlpha;
uniform sampler2DMS u_FogTexture;
uniform sampler2DMS u_SkyColorTexture;

in vec2 TexCoords;

uniform sampler2DMS u_Texture;
uniform usampler2DMS u_Stencil;
uniform int u_TextureSamples;
uniform int u_IgnoreStencil;

// Water composite (X_HIGH) uniforms.
uniform sampler2DMS u_ReflectionTex;
uniform sampler2DMS u_DepthTexture;
uniform int u_WaterPass;
uniform int u_Ripple;
uniform float u_Time;
uniform mat4 u_MVP;
uniform mat4 u_InvMVP;
uniform float u_OriginY;
uniform vec2 u_OriginXZ;
uniform vec3 u_SunDir;
uniform vec3 u_CameraPos;

// Sea level water surface in absolute world Y: WATER_LEVEL(0) + WATER_HEIGHT - 1.
const float SEA_SURFACE_Y = -0.125;



vec4 textureMultisample(ivec2 coord) {
    vec4 colorMS = vec4(0.0);
    for(int i = 0; i < u_TextureSamples; i++) {
        colorMS += texelFetch(u_Texture, coord, i);
    }
    colorMS /= float(u_TextureSamples);
    return colorMS;
}

vec4 textureSample(ivec2 coord) {
    return texelFetch(u_Texture, coord, 0);
}

uint stecilSample(ivec2 coord) {
    return texelFetch(u_Stencil, coord, 0).r;
}

uint stencilMultisample(ivec2 coord) {
    float colorMS = 0.0;
    for(int i = 0; i < u_TextureSamples; i++) colorMS += float(texelFetch(u_Stencil, coord, i).r);
    colorMS /= float(u_TextureSamples);
    return uint(colorMS);
}

ivec2 tex_to_multisaple(vec2 texCoord) {
    ivec2 vpCoords = textureSize(u_Texture);

    ivec2 texCoordMS;
    texCoordMS.x = int(float(vpCoords.x) * texCoord.x);
    texCoordMS.y = int(float(vpCoords.y) * texCoord.y);
    return texCoordMS;
}

vec3 skyColorMultisample(ivec2 coord) {
    vec3 skyMS = vec3(0.0);
    for(int i = 0; i < u_TextureSamples; i++) {
        skyMS += texelFetch(u_SkyColorTexture, coord, i).rgb;
    }
    skyMS /= float(u_TextureSamples);
    return skyMS;
}

float fogFactorMultisample(ivec2 coord) {
    float fogMS = 0.0;
    for(int i = 0; i < u_TextureSamples; i++) {
        fogMS += texelFetch(u_FogTexture, coord, i).a;
    }
    fogMS /= float(u_TextureSamples);
    return fogMS;
}

vec4 reflectionMultisample(ivec2 coord) {
    vec4 reflMS = vec4(0.0);
    for(int i = 0; i < u_TextureSamples; i++) {
        reflMS += texelFetch(u_ReflectionTex, coord, i);
    }
    reflMS /= float(u_TextureSamples);
    return reflMS;
}

float depthMultisample(ivec2 coord) {
    float depthMS = 0.0;
    for(int i = 0; i < u_TextureSamples; i++) {
        depthMS += texelFetch(u_DepthTexture, coord, i).r;
    }
    depthMS /= float(u_TextureSamples);
    return depthMS;
}

// Animated pixel-space ripple in screen space. scale is the peak offset in
// pixels; two crossed sine frequencies keep it from looking like a slide.
vec2 rippleOffset(ivec2 coord, float scale) {
    float x = float(coord.x);
    float y = float(coord.y);
    return scale * vec2(sin(y * 0.041 + u_Time * 1.9) + sin((x + y) * 0.023 + u_Time * 1.3),
                        sin(x * 0.037 + u_Time * 1.6) + sin((x - y) * 0.029 + u_Time * 1.1));
}

// World-space ripple: the phase comes from the surface's absolute XZ so the
// wave pattern is anchored in the world and doesn't swim when the camera
// moves. scale is still the peak pixel offset.
vec2 ripplePhaseOffset(vec2 worldXZ, float scale) {
    float x = worldXZ.x;
    float y = worldXZ.y;
    return scale * vec2(sin(y * 0.75 + u_Time * 1.9) + sin((x + y) * 0.42 + u_Time * 1.3),
                        sin(x * 0.68 + u_Time * 1.6) + sin((x - y) * 0.5 + u_Time * 1.1));
}

// Screen-space reflection for fluids whose surface is NOT at sea level: the
// planar reflection texture is mirrored about the sea plane only, so elevated
// ponds/streams/lava ray-march the already-rendered scene instead. Water
// pixels (stencil != 0) are skipped so the ray sees past other fluid surfaces.
vec3 ssrReflection(vec3 waterPos, vec3 rayDir) {
    vec3 reflDir = normalize(rayDir * vec3(1.0, -1.0, 1.0));
    vec2 tsize = vec2(textureSize(u_DepthTexture));
    const int MAX_STEPS = 80;
    const float STEP = 0.45;
    // Depth is hyperbolic (near=0.1, far=800): one march step changes rayD by
    // only ~4e-4 near geometry, so a fixed positive bias swallows every hit.
    // Allow a small under-shoot instead; the binary refine resolves the real
    // crossing point before sampling.
    const float HIT_EPS = 0.0008;
    vec3 pos = waterPos + reflDir * 0.2;
    vec2 lastUV = TexCoords;
    for(int i = 0; i < MAX_STEPS; i++) {
        pos += reflDir * STEP;
        vec4 clip = u_MVP * vec4(pos, 1.0);
        if(clip.w <= 0.0) {
            break;
        }
        vec3 nd = clip.xyz / clip.w;
        vec2 uv = nd.xy * 0.5 + 0.5;
        if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            break;
        }
        lastUV = uv;
        ivec2 ic = ivec2(uv * tsize);
        if(stecilSample(ic) != 0u) {
            continue;
        }
        float sceneD = depthMultisample(ic);
        float rayD = nd.z * 0.5 + 0.5;
        if(rayD > sceneD - HIT_EPS && sceneD < 0.9999) {
            // Binary refine between the last free point and the hit point.
            vec3 lo = pos - reflDir * STEP;
            vec3 hi = pos;
            for(int j = 0; j < 4; j++) {
                vec3 mid = (lo + hi) * 0.5;
                vec4 mc = u_MVP * vec4(mid, 1.0);
                vec3 mn = mc.xyz / mc.w;
                vec2 muv = mn.xy * 0.5 + 0.5;
                ivec2 mic = ivec2(clamp(muv, vec2(0.0), vec2(1.0)) * tsize);
                float md = depthMultisample(mic);
                if(mn.z * 0.5 + 0.5 > md) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            vec4 fc = u_MVP * vec4(hi, 1.0);
            vec2 fuv = (fc.xyz / fc.w).xy * 0.5 + 0.5;
            ivec2 fic = ivec2(clamp(fuv, vec2(0.0), vec2(1.0)) * tsize);
            return textureMultisample(fic).rgb;
        }
    }
    // Miss: the reflected ray left the frame (or hit nothing within range) —
    // use the rendered sky at the exit point, a good approximation of the sky
    // in the reflected direction.
    ivec2 sc = ivec2(clamp(lastUV, vec2(0.0), vec2(1.0)) * tsize);
    return skyColorMultisample(sc);
}

// X_HIGH water composite: runs only on stencil==1 (fluid) pixels over the
// already-composited scene. Adds a rippled, fresnel-weighted reflection and a
// sun glint; output alpha blends it over the scene beneath.
void waterMain(ivec2 multiCoords) {
    vec2 ndc = TexCoords * 2.0 - 1.0;

    // View ray in the rebased world frame (matches v_world_coords space).
    vec4 w0 = u_InvMVP * vec4(ndc, -1.0, 1.0);
    vec4 w1 = u_InvMVP * vec4(ndc, 1.0, 1.0);
    vec3 rayDir = normalize(w1.xyz / w1.w - w0.xyz / w0.w);

    // Fresnel against the horizontal surface; abs() covers underwater views too.
    // The base/exponent are tuned up from physical values — real water's 0.02
    // Schlick F0 reads as fully transparent at this game's scale.
    float fresnel = 0.18 + 0.82 * pow(1.0 - clamp(abs(rayDir.y), 0.0, 1.0), 3.0);

    // The water pass writes depth, so the stored depth here is the fluid
    // surface itself. Unproject it to recover the surface point + its height.
    float surfDepth = depthMultisample(multiCoords);
    vec4 wp = u_InvMVP * vec4(ndc, surfDepth * 2.0 - 1.0, 1.0);
    vec3 waterPos = wp.xyz / wp.w;
    float waterY = waterPos.y + u_OriginY;

    // Ripple attenuates with surface distance so far water doesn't shimmer.
    float surfDist = length(waterPos - u_CameraPos);
    ivec2 rip = ivec2(ripplePhaseOffset(waterPos.xz + u_OriginXZ, 7.0 / (1.0 + surfDist * 0.12)));

    vec3 reflColor;
    float reflAlpha;
    if(abs(waterY - SEA_SURFACE_Y) < 0.06) {
        // Sea-level fluid: the mirrored-world pass is exact.
        vec4 r = reflectionMultisample(multiCoords + rip);
        reflColor = r.rgb;
        reflAlpha = r.a;
    } else {
        reflColor = ssrReflection(waterPos, rayDir);
        reflAlpha = 1.0;
    }

    // Fade the reflection out near the fog edge (same factor as the old path).
    float fogF = fogFactorMultisample(multiCoords);
    float weight = reflAlpha * fresnel * u_ExtraAlpha * (1.0 - fogF);

    // Sun glint along the reflected view direction.
    vec3 reflDir = normalize(rayDir * vec3(1.0, -1.0, 1.0));
    float spec = pow(clamp(dot(reflDir, u_SunDir), 0.0, 1.0), 600.0) * (1.0 - fogF);

    color = vec4(reflColor + vec3(spec), weight);
}

void main() {
    vec4 finalColor;
    ivec2 multiCoords = tex_to_multisaple(TexCoords);

    if(u_WaterPass != 0) {
        waterMain(multiCoords);
        return;
    }

    // Refraction wobble for the water-region scene draw: the whole pixel
    // (color + stencil + fog lookups) shifts coherently. The ripple phase is
    // anchored to the surface's world position and fades with distance.
    if(u_Ripple != 0) {
        float surfDepth = depthMultisample(multiCoords);
        vec4 wp = u_InvMVP * vec4(TexCoords * 2.0 - 1.0, surfDepth * 2.0 - 1.0, 1.0);
        vec3 wpos = wp.xyz / wp.w;
        float dist = length(wpos - u_CameraPos);
        multiCoords += ivec2(ripplePhaseOffset(wpos.xz + u_OriginXZ, 5.0 / (1.0 + dist * 0.12)));
    }

    if(u_Blur != 0) {
        uint stencilCenter = stecilSample(multiCoords);

        int blurSize = 1;
        float numValid = 0u;
        for(int i = -blurSize; i < blurSize + 1; i += 1) {
            for(int j = -blurSize; j < blurSize + 1; j += 1) {
                ivec2 offset = ivec2(i, j);
                ivec2 pixelCoords = multiCoords + offset;
                uint stencil = stecilSample(pixelCoords);
                vec4 textureColor = textureSample(pixelCoords);
                bool valid = (u_IgnoreStencil != 0 || stencilCenter == stencil) && textureColor.a > 0.0;
                if(valid) {
                    float weight = (offset.x == 0 && offset.y == 0) ? 1.0 : 1.0 / (offset.x * offset.x + offset.y * offset.y);
                    // float weight = 1.0f;
                    numValid += weight;
                    finalColor += weight * textureColor;
                }
            }
        }
        finalColor /= numValid;
    } else {
        finalColor = textureMultisample(multiCoords);
    }

    if(u_FogBlend != 0) {
        // Fog compositing: blend the opaque terrain/water (RGB) with the actual
        // sky using the fog factor stored in the fog texture's alpha channel.
        // The color texture's alpha is used for normal blending (opaque=1,
        // water=natural alpha) and is not the fog factor.
        // Discard pixels with alpha=0 (e.g. non-water pixels in the reflection
        // texture) so only valid content is composited.
        if(u_DiscardZeroAlpha != 0 && finalColor.a == 0.0) {
            discard;
        }
        // For the main terrain compositing, the fog factor comes from the
        // dedicated fog texture (water surface distance). For the reflection
        // compositing, the fog factor is the max of the water surface distance
        // (from the fog texture) and the reflected geometry's distance (from
        // the reflection texture's alpha). This accounts for the full light
        // path (object -> water -> eye) and hides pop-in at the render edge:
        // if either the water or the reflected chunk is far, the reflection
        // is fully fogged out.
        float waterFog = fogFactorMultisample(multiCoords);
        float reflectedFog = 1.0 - finalColor.a;
        float fogFactor = (u_DiscardZeroAlpha != 0) ? max(waterFog, reflectedFog) : waterFog;
        vec3 skyColor = skyColorMultisample(multiCoords);
        // Blend toward the actual sky color faster than the alpha fades, so
        // the terrain color matches the sky before it becomes fully
        // transparent. This hides the silhouette edge of mountains at the
        // render boundary where the horizon fog color (used in the chunk
        // shader) doesn't match the actual sky at that elevation.
        float colorBlend = clamp(pow(fogFactor, 0.5), 0.0, 1.0);
        vec3 result = mix(finalColor.rgb, skyColor, colorBlend);
        // Main terrain compositing (u_DiscardZeroAlpha==0) outputs alpha=1.0
        // since the FBO already has the complete rendered image. Reflection
        // compositing (u_DiscardZeroAlpha==1) is semi-transparent so the
        // water/terrain beneath shows through. The reflection's alpha also
        // decreases with the fog factor, so reflections (including the
        // reflected sky) fade out entirely as the water surface approaches
        // the fog edge — without this, the reflected sky would stay visible
        // at a constant opacity since its RGB is already the sky color and
        // blending it toward the sky color is a no-op.
        float outAlpha = (u_DiscardZeroAlpha != 0) ? finalColor.a * u_ExtraAlpha * (1.0 - fogFactor) : 1.0;
        color = vec4(result, outAlpha);
        return;
    }

    if(u_DiscardZeroAlpha != 0) {
        // Reflection compositing: preserve semi-transparency for reflections
        // blended over the terrain/water beneath.
        if(finalColor.a == 0.0) {
            discard;
        }
        color = vec4(finalColor.rgb, finalColor.a * u_ExtraAlpha);
    } else {
        // Main terrain compositing (non-fog-blend path, e.g. underwater):
        // the FBO already contains the complete composited image (sky + terrain
        // with fog blended in-shader), so output opaque to avoid darkening
        // from the screen clear color where the FBO alpha is < 1.
        color = vec4(finalColor.rgb, 1.0);
    }
}
