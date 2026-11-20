#version 300 es

// World-space sign text: font-atlas alpha test + the same distance-fog and
// underwater tint object_fragment applies, so sign ink fades out like the
// terrain it floats on. Writes all four framebuffer attachments for the
// FBO quality tiers (ignored on the plain path).

precision highp float;

uniform sampler2D u_Texture;
uniform float u_FogNear;
uniform float u_FogFar;
uniform float u_WaterFogNear;
uniform float u_WaterFogFar;
uniform vec3 u_CameraPos;
uniform vec3 u_SkyAvgColor;
// Same as chunk_fragment: fog target for the submerged leg of the view ray.
uniform vec3 u_WaterFogColor;
uniform vec3 u_Origin;
uniform int u_OpaqueFog;
uniform int u_Underwater;
// TINT_UNDER_WATER_OBJECT_* values from object_fragment.glsl.
uniform int u_TintUnderWater;

in vec2 v_TexCoord;
in vec4 v_Tint;
in vec3 v_world_coords;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 reflection;
layout(location = 2) out vec4 fogFactor;
layout(location = 3) out vec4 skyColor;

void main() {
    float alpha = texture(u_Texture, v_TexCoord).a;
    if (alpha == 0.0) {
        discard;
    }
    vec4 finalColor = vec4(v_Tint.rgb, v_Tint.a * alpha);

    // Same fog math as object_fragment (non-sky, forward path): color
    // blends toward the sky average over the first half of the fog range,
    // alpha fades over the second.
    float dist = distance(v_world_coords, u_CameraPos);
    // Same medium-split fog as chunk_fragment: air range on the
    // above-surface leg of the view ray, water range on the submerged leg.
    float surfY = -0.125f - u_Origin.y;
    float loY = min(u_CameraPos.y, v_world_coords.y);
    float hiY = max(u_CameraPos.y, v_world_coords.y);
    float waterPath = dist * clamp((clamp(surfY, loY, hiY) - loY) / max(hiY - loY, 0.00001f), 0.0f, 1.0f);
    // Same gate as chunk_fragment: a camera below the sea plane in air
    // (dry caves, the nether) isn't looking through water.
    bool cameraUnderWater = u_TintUnderWater != 0;
    if(!cameraUnderWater && u_CameraPos.y < surfY) {
        waterPath = 0.0f;
    }
    float airPath = dist - waterPath;
    float airFogLinear = clamp((airPath - u_FogNear) / (u_FogFar - u_FogNear), 0.0, 1.0);
    // Same two-way column as chunk_fragment: a dry camera sees light that
    // crossed the water down and back, so the leg counts double.
    float waterPathEff = (!cameraUnderWater && u_CameraPos.y >= surfY) ? waterPath * 2.0f : waterPath;
    float waterFogLinear = clamp((waterPathEff - u_WaterFogNear) / (u_WaterFogFar - u_WaterFogNear), 0.0, 1.0);
    float fogLinear = 1.0 - (1.0 - airFogLinear) * (1.0 - waterFogLinear);
    float waterShare = waterFogLinear / max(fogLinear, 0.001);
    // Same edge convergence as chunk_fragment: the murk target yields to
    // the sky over the second half of the air fog band.
    float airFade = clamp((airFogLinear - 0.3f) / 0.4f, 0.0f, 1.0f);
    waterShare *= 1.0f - airFade * airFade * (3.0f - 2.0f * airFade);
    bool belowWater = u_TintUnderWater != 0 && v_world_coords.y < (surfY - 0.00001f);
    // Same as chunk_fragment: a dry camera's water murk ramps over the
    // whole band; everything else saturates by half range.
    float colorHalf = (!cameraUnderWater && u_CameraPos.y >= surfY) ? mix(0.5f, 1.0f, waterShare) : 0.5f;
    float colorFog = clamp(fogLinear / colorHalf, 0.0, 1.0);
    colorFog = colorFog * colorFog * (3.0 - 2.0 * colorFog);
    float alphaFog = clamp((fogLinear - 0.5) / 0.5, 0.0, 1.0);
    alphaFog = alphaFog * alphaFog * (3.0 - 2.0 * alphaFog);
    finalColor.rgb = mix(finalColor.rgb, mix(u_SkyAvgColor, u_WaterFogColor, waterShare), colorFog);
    if (u_OpaqueFog == 1) {
        fogFactor = vec4(colorFog, 0.0, waterShare, alphaFog);
        finalColor.a = 1.0;
    } else {
        if (!belowWater) {
            finalColor.a *= (1.0 - alphaFog);
        }
        fogFactor = vec4(0.0);
    }
    reflection = vec4(0.0);
    skyColor = vec4(0.0);

    if (u_Underwater != 0) {
        finalColor.rgb = mix(finalColor.rgb, vec3(0.04f, 0.16f, 0.55f), 0.45f);
    }
    color = finalColor;
}
