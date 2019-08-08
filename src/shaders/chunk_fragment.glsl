#version 300 es

precision highp float;
precision lowp sampler2DArray;

in vec2 v_TexCoordBlock;
flat in float v_blockID;
in float v_corner_lighting;
flat in int v_shouldDiscardNoLight;
in vec3 v_pos_world;
in vec3 v_normal;

uniform float u_shouldDiscardAlpha;
uniform sampler2DArray u_Texture;
uniform sampler2DArray u_Metallic;
uniform vec3 u_LightPosition;
uniform vec3 u_CameraPos;

layout( location = 0 ) out vec4 color;

const float PI = 3.14159265359;

float DistributionGGX( vec3 N, vec3 H, float roughness );
float GeometrySchlickGGX( float NdotV, float roughness );
float GeometrySmith( vec3 N, vec3 V, vec3 L, float roughness );
vec3 fresnelSchlick( float cosTheta, vec3 F0 );

void main( ) {
    if ( v_shouldDiscardNoLight == 1 ) {
        discard;
    }
    vec4 texColor = texture( u_Texture, vec3( v_TexCoordBlock, v_blockID ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    if ( u_shouldDiscardAlpha == 1.0f && texColor.a < 0.7 ) {
        discard;
    }

    float corner_light = v_corner_lighting;
    vec4 orig_color = texColor * vec4( corner_light, corner_light, corner_light, 1 );

    vec3 albedo = orig_color.xyz;
    float metallic = texture( u_Metallic, vec3( v_TexCoordBlock, v_blockID ) ).x;
    float roughness = 0.5;
    float ao = 0.5;

    vec3 N = v_normal;
    vec3 V = normalize( u_CameraPos - v_pos_world );
    vec3 F0 = vec3( 0.04 );
    F0 = mix( F0, albedo, metallic );
    vec3 lightColor = vec3( 1, 1, 1 );

    // reflectance equation
    vec3 Lo = vec3( 0.0 );
    { // Per light
        // calculate per-light radiance
        vec3 L = normalize( u_LightPosition - v_pos_world );
        vec3 H = normalize( V + L );
        float distance = length( u_LightPosition - v_pos_world );
        float attenuation = 1.0 / ( distance * distance );
        vec3 radiance = lightColor * attenuation;

        // cook-torrance brdf
        float NDF = DistributionGGX( N, H, roughness );
        float G = GeometrySmith( N, V, L, roughness );
        vec3 F = fresnelSchlick( max( dot( H, V ), 0.0 ), F0 );

        vec3 kS = F;
        vec3 kD = vec3( 1.0 ) - kS;
        kD *= 1.0 - metallic;

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max( dot( N, V ), 0.0 ) * max( dot( N, L ), 0.0 );
        vec3 specular = numerator / max( denominator, 0.001 );

        // add to outgoing radiance Lo
        float NdotL = max( dot( N, L ), 0.0 );
        Lo = ( kD * albedo / PI + specular ) * radiance * NdotL;
    }

    vec3 ambient = vec3( 0.03 ) * albedo * ao;
    vec3 new_color = ambient + Lo;

    new_color = new_color / ( new_color + vec3( 1.0 ) );
    new_color = pow( new_color, vec3( 1.0 / 2.2 ) );
    color = vec4( new_color, orig_color.a );
}

vec3 fresnelSchlick( float cosTheta, vec3 F0 ) {
    return F0 + ( 1.0 - F0 ) * pow( 1.0 - cosTheta, 5.0 );
}

float DistributionGGX( vec3 N, vec3 H, float roughness ) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max( dot( N, H ), 0.0 );
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = ( NdotH2 * ( a2 - 1.0 ) + 1.0 );
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX( float NdotV, float roughness ) {
    float r = ( roughness + 1.0 );
    float k = ( r * r ) / 8.0;

    float num = NdotV;
    float denom = NdotV * ( 1.0 - k ) + k;

    return num / denom;
}
float GeometrySmith( vec3 N, vec3 V, vec3 L, float roughness ) {
    float NdotV = max( dot( N, V ), 0.0 );
    float NdotL = max( dot( N, L ), 0.0 );
    float ggx2 = GeometrySchlickGGX( NdotV, roughness );
    float ggx1 = GeometrySchlickGGX( NdotL, roughness );

    return ggx1 * ggx2;
}
