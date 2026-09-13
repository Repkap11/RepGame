#version 400

precision highp float;

layout(location = 0) out vec4 color;

uniform float u_ExtraAlpha;
uniform int u_Blur;
uniform int u_FogBlend;
uniform sampler2DArray u_SkyTexture;
uniform sampler2DMS u_FogTexture;
uniform mat4 u_InvMVPSky;

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

vec3 reconstructSky(vec2 texCoords) {
    vec2 ndc = texCoords * 2.0 - 1.0;
    vec4 skyPos = u_InvMVPSky * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(skyPos.xyz / skyPos.w);
    float phi = acos(clamp(-dir.y, -1.0, 1.0));
    float V = phi / 3.14159265;
    float theta = atan(dir.z, dir.x);
    float U = fract(theta / 6.28318531);
    return texture(u_SkyTexture, vec3(U, V, 0.0)).rgb;
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
                bool valid = u_IgnoreStencil != 0 || stencilCenter == stencil;
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
        float fogFactor = fogFactorMultisample(multiCoords);
        vec3 skyColor = reconstructSky(TexCoords);
        vec3 result = mix(finalColor.rgb, skyColor, fogFactor);
        color = vec4(result, 1.0);
        return;
    }

    if(finalColor.a == 0.0) {
        discard;
    }
    finalColor.a *= u_ExtraAlpha;
    color = finalColor;
}