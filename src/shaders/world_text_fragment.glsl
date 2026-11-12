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
    bool cameraUnderWater = u_TintUnderWater != 0;
    bool belowWater = u_TintUnderWater == 2 ||
                      (u_TintUnderWater == 1 && v_world_coords.y < (-0.125f - u_Origin.y - 0.00001f));
    float fogNear = cameraUnderWater ? u_WaterFogNear : u_FogNear;
    float fogFar = cameraUnderWater ? u_WaterFogFar : u_FogFar;
    float fogLinear = clamp((dist - fogNear) / (fogFar - fogNear), 0.0, 1.0);
    float colorFog = clamp(fogLinear / 0.5, 0.0, 1.0);
    colorFog = colorFog * colorFog * (3.0 - 2.0 * colorFog);
    float alphaFog = clamp((fogLinear - 0.5) / 0.5, 0.0, 1.0);
    alphaFog = alphaFog * alphaFog * (3.0 - 2.0 * alphaFog);
    finalColor.rgb = mix(finalColor.rgb, u_SkyAvgColor, colorFog);
    if (u_OpaqueFog == 1) {
        fogFactor = vec4(colorFog, 0.0, 0.0, alphaFog);
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
