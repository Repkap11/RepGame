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
// uniform sampler2DMS u_Stencil;
uniform int u_TextureSamples;
uniform int u_IgnoreStencil;

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

void main() {
    vec4 finalColor;
    ivec2 multiCoords = tex_to_multisaple(TexCoords);

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