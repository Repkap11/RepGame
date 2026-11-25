#include "common/world.hpp"
#include "common/chunk.hpp"
#include "common/dimension_profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/multiplayer.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/screenshot.hpp"
#include "common/Logging.hpp"
#include "common/tree_grow.hpp"
#include "common/light.hpp"
#include "common/fluid.hpp"
#include "common/imgui_overlay.hpp"
#include "common/block_update_events/FluidUpdateEvent.hpp"
#include "common/block_update_events/PistonEvent.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>


#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

struct WorldFrameEnv {
    float fog_near;
    float fog_far;
    float water_fog_near;
    float water_fog_far;
    int underwater;
    glm::vec3 camera_pos;
    glm::vec3 origin;
    glm::vec3 sky_avg_color;
    glm::vec3 water_fog_color;
    float sea_surface_y;
    int opaque_fog;
    float daylight;
    float min_ambient;
};

static void world_apply_frame_env( Shader &shader, const WorldFrameEnv &env ) {
    shader.set_uniform1f( "u_FogNear", env.fog_near );
    shader.set_uniform1f( "u_FogFar", env.fog_far );
    shader.set_uniform1f( "u_WaterFogNear", env.water_fog_near );
    shader.set_uniform1f( "u_WaterFogFar", env.water_fog_far );
    shader.set_uniform1i( "u_Underwater", env.underwater );
    shader.set_uniform3f( "u_CameraPos", env.camera_pos.x, env.camera_pos.y, env.camera_pos.z );
    shader.set_uniform3f( "u_Origin", env.origin.x, env.origin.y, env.origin.z );
    shader.set_uniform3f( "u_SkyAvgColor", env.sky_avg_color.r, env.sky_avg_color.g, env.sky_avg_color.b );
    shader.set_uniform3f( "u_WaterFogColor", env.water_fog_color.r, env.water_fog_color.g, env.water_fog_color.b );
    shader.set_uniform1f( "u_SeaSurfaceY", env.sea_surface_y );
    shader.set_uniform1i( "u_OpaqueFog", env.opaque_fog );
    shader.set_uniform1f( "u_Daylight", env.daylight );
    shader.set_uniform1f( "u_MinAmbient", env.min_ambient );
}

#define WATER_THRESHOLD_P ( 0.02 )
#define WATER_THRESHOLD_N ( -0.01 )
void World::draw( const Texture &blocksTexture, const glm::mat4 &mvp, const glm::mat4 &inv_mvp, const glm::mat4 &view_look, const glm::mat4 &mvp_reflect, const glm::mat4 &mvp_sky, const glm::mat4 &mvp_sky_reflect, const int debug, const int draw_mouse_selection, const float y_height,
                  const bool headInWater, WorldDrawQuality worldDrawQuality, const glm::dvec3 &camera_pos, const glm::ivec3 &renderOrigin, const float time_s, const float daylight, const float min_ambient, const float alpha ) {
    PROFILE_SCOPE( "world_draw" );

    const glm::vec3 renderOriginF = glm::vec3( renderOrigin );
    // Camera and block positions are rebased by u_Origin in the vertex shaders,
    // so u_CameraPos must also be relative to the origin for fog distance.
    // Subtract in double so the rebased position keeps full precision.
    const glm::vec3 camera_pos_rebased = glm::vec3( camera_pos - glm::dvec3( renderOrigin ) );

    const bool useFrameBuffer = SUPPORTS_FRAME_BUFFER && ( worldDrawQuality >= WorldDrawQuality::MEDIUM );
    const bool usingReflections = useFrameBuffer && ( worldDrawQuality >= WorldDrawQuality::HIGH );
    const bool allowBlur = usingReflections && ( worldDrawQuality >= WorldDrawQuality::X_HIGH );
    int object_water_tint_type;
    int block_water_tint_type;
    float out_int_part;
    const float water_diff = modff( y_height, &out_int_part ) - ( WATER_HEIGHT );
    if ( headInWater ) {
        if ( water_diff > WATER_THRESHOLD_N && water_diff < WATER_THRESHOLD_P ) {
            object_water_tint_type = TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL;
            block_water_tint_type = TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL;
        } else if ( water_diff < WATER_THRESHOLD_N ) {
            object_water_tint_type = TINT_UNDER_WATER_OBJECT_ALWAYS;
            block_water_tint_type = TINT_UNDER_WATER_OBJECT_ALWAYS;
        } else {
            object_water_tint_type = TINT_UNDER_WATER_OBJECT_NEVER;
            block_water_tint_type = TINT_UNDER_WATER_OBJECT_NEVER;
        }
    } else {
        object_water_tint_type = TINT_UNDER_WATER_OBJECT_NEVER;
        block_water_tint_type = TINT_UNDER_WATER_OBJECT_NEVER;
    }
    float debug_block_scale;
    if ( debug ) {
        debug_block_scale = BLOCK_SCALE_OFFSET;
    } else {
        debug_block_scale = 0.0f;
    }

    // The camera's dimension drives the environment: sky panorama, fog tint
    // and fog range all come from its profile (nether = no sky, close
    // ember-haze fog).
    const DimensionId camera_dim = dimension_of_pos( camera_pos );
    const DimensionProfile *dim_prof = dimension_profile( camera_dim );
    this->skyBox.set_dimension( camera_dim );
    // The dimension's inherent ambient floor stacks on the player's Cave
    // Brightness setting (nether caverns are never pitch black regardless of
    // that slider); both are floors, so the higher one wins.
    const float scene_min_ambient = glm::max( min_ambient, dim_prof->min_ambient );

    // Distance fog: blend terrain toward the sky color in a band ending at
    // the render edge. The band is a constant number of blocks (capped at
    // the old 38% of render distance so small render distances are
    // unchanged), so the haze stays thin at large render distances instead
    // of scaling up, while still reaching full opacity at the edge to hide
    // chunk pop-in.
    static const float env_fog_range = env_float( "REPGAME_FOG_RANGE", 96.0f );
    const float fog_range_blocks = camera_dim == DIM_OVERWORLD || camera_dim == DIM_REPHEIM ? env_fog_range : dim_prof->fog_range;
    // The fog band ends at the ACTIVE render edge (runtime radius), not the
    // torus capacity, so pop-in stays hidden at small render distances.
    const float render_distance = static_cast<float>( this->chunkLoader.render_distance_blocks( ) );
    const float fog_far = render_distance * 0.98f;
    const float fog_near = fog_far - fminf( fog_range_blocks, render_distance * 0.38f );
    // Underwater murk: the portion of each view ray below the sea-level
    // plane fogs over this much shorter range. Evaluated per fragment in
    // the shaders, so the seabed murks over when viewed from above and
    // above-surface terrain stays visible from underwater.
    const ImGuiDebugVars &debug_vars = imgui_overlay_get_imgui_debug_vars( );
    const bool water_fog_active = debug_vars.water_fog || headInWater;
    const float water_fog_near = water_fog_active ? 8.0f : 1000000.0f;
    const float water_fog_far = water_fog_active ? 90.0f : 2000000.0f;
    // The fog blend's in-shader phase targets the sky average; the
    // submerged share of each ray targets the water murk instead (per
    // fragment via waterShare, and per pixel in the composite through the
    // fog attachment's B channel).
    glm::vec3 fog_blend_color = this->skyBox.get_avg_color( );
    if ( !dim_prof->skybox ) {
        // No skybox: fog blends toward the dimension's fixed haze instead of
        // a sky average (the End keys off skybox rather than has_sky: it has
        // no skylight, but its noise sky is the correct fog target).
        fog_blend_color = glm::vec3( dim_prof->fog_r, dim_prof->fog_g, dim_prof->fog_b );
    }
    // The rendered sky is the sky texture dimmed by u_Daylight, so the fog
    // target must dim the same way or distant terrain blends toward a
    // day-bright color at night. Skyless dimensions have a constant haze
    // instead — the day/night cycle doesn't exist underground.
    if ( dim_prof->has_sky ) {
        fog_blend_color *= daylight;
    }
    // The dimension's sea surface in absolute world y: the mirrored
    // reflection pass, the submerged-leg fog split, and the water
    // composite's planar-vs-SSR pick all key off this plane.
    const float sea_surface_y = static_cast<float>( dim_sea_level_block_y( camera_dim ) ) + WATER_HEIGHT - 1.0f;
    // Water murk target: depth-graded by the camera's depth below the
    // surface (light blue at the surface → navy when deep), dimmed by
    // daylight like the sky target.
    const float depthBelow = sea_surface_y - y_height; // sea level + WATER_HEIGHT - 1 = surface Y
    const float depthF = glm::clamp( depthBelow / 30.0f, 0.0f, 1.0f );
    glm::vec3 water_murk = glm::mix( glm::vec3( 0.045f, 0.13f, 0.46f ), glm::vec3( 0.008f, 0.03f, 0.14f ), depthF );
    if ( dim_prof->has_sky ) {
        water_murk *= daylight;
    }

    this->chunkLoader.shader.set_uniform1f( "u_WaterOpacityBoost", debug_vars.water_opacity_boost );
    this->chunkLoader.shader.set_uniform1f( "u_WaterDarken", debug_vars.water_darken );
    this->chunkLoader.shader.set_uniform1f( "u_WaterDistanceStart", debug_vars.water_distance_start );
    this->chunkLoader.shader.set_uniform1f( "u_WaterDistanceEnd", debug_vars.water_distance_end );
    this->chunkLoader.shader.set_uniform1f( "u_WaterDistanceDarken", debug_vars.water_distance_darken );
    this->chunkLoader.shader.set_uniform1f( "u_WaterDistanceOpacity", debug_vars.water_distance_opacity );
    // Framebuffer tiers apply the underwater blue wash in the composite pass;
    // LOW renders forward, so the object/chunk shaders apply it in-shader
    // instead. Same uniform name on purpose: only one of the two is ever
    // active for a given draw.
    const int underwater_wash = ( headInWater && !useFrameBuffer && block_water_tint_type == TINT_UNDER_WATER_OBJECT_ALWAYS ) ? 1 : 0;
    // Opaque fog stores the fog factor in a dedicated fog texture (attachment 2)
    // for post-process sky blending. Works in all framebuffer modes since the
    // fog texture is separate from the reflection texture. Underwater it blends
    // toward the sky attachment over the short water-fog range, so distant
    // terrain converges to the same "water background" as the surface/sky.
    const bool useFogBlend = useFrameBuffer;
    // Absolute world y of the dimension's sea surface — the fog/murk split
    // and the water-side tint rebase it by u_Origin in the shader.
    // Daylight scales the sky-light nibble sampled from each chunk's 3D light
    // volume (chunk_fragment) and dims sky/mobs/debris (object_fragment).
    // u_MinAmbient is an absolute light floor so unlit caves are never black
    // (user setting and the dimension profile's floor combined above).
    const WorldFrameEnv frame_env = { fog_near, fog_far, water_fog_near, water_fog_far, underwater_wash, camera_pos_rebased, renderOriginF, fog_blend_color, water_murk, sea_surface_y, useFogBlend ? 1 : 0, daylight, scene_min_ambient };
    world_apply_frame_env( this->chunkLoader.shader, frame_env );
    // No-framebuffer fog blends toward the actual sky color (chunk_fragment
    // samples the equirect sky texture at the view direction).
    this->chunkLoader.shader.set_uniform1i_texture( "u_SkyTexture", this->skyBox.get_texture( ) );
    // End portal/gateway screen-space starfield textures (chunk_fragment).
    this->chunkLoader.shader.set_uniform1i_texture( "u_EndNoise", this->skyBox.get_end_noise_texture( ) );
    this->chunkLoader.shader.set_uniform1i_texture( "u_EndStars", this->skyBox.get_end_stars_texture( ) );
    // In the End the sky is cube-mapped noise: the no-framebuffer fog blend
    // resamples it the same way the object sky pass does.
    this->chunkLoader.shader.set_uniform1i( "u_EndSky", camera_dim == DIM_END ? 1 : 0 );

    world_apply_frame_env( this->object_shader, frame_env );

    // Debris shares object_fragment.glsl, so it needs the same fog/water set.
    world_apply_frame_env( this->debris_shader, frame_env );
    this->debris_shader.set_uniform1f( "u_Time", time_s );

    // Hearts share object_fragment.glsl too, plus the billboard basis
    // (camera right/up in world space — rows of the view rotation).
    world_apply_frame_env( this->heart_shader, frame_env );
    this->heart_shader.set_uniform1f( "u_Time", time_s );
    const glm::vec3 cam_right( view_look[ 0 ][ 0 ], view_look[ 1 ][ 0 ], view_look[ 2 ][ 0 ] );
    const glm::vec3 cam_up( view_look[ 0 ][ 1 ], view_look[ 1 ][ 1 ], view_look[ 2 ][ 1 ] );
    // View-space +z points backward, so camera forward is -row2 (the look
    // vector view_look was built from). Drives the blended painter-order
    // sorts in ChunkLoader::draw; the reflect pass below reuses it — the
    // normal view's order is close enough for the mirrored world.
    const glm::vec3 cam_forward( -view_look[ 0 ][ 2 ], -view_look[ 1 ][ 2 ], -view_look[ 2 ][ 2 ] );
    this->heart_shader.set_uniform3f( "u_CamRight", cam_right.x, cam_right.y, cam_right.z );
    this->heart_shader.set_uniform3f( "u_CamUp", cam_up.x, cam_up.y, cam_up.z );

    // Primed TNT also shares object_fragment.glsl — same fog/water set, no
    // u_Time (the fuse blink is a per-instance CPU attribute).
    world_apply_frame_env( this->primed_tnt_shader, frame_env );

    // Block objects (non-full-cube items + falling blocks) share
    // object_fragment.glsl too — same fog/water set, no u_Time.
    world_apply_frame_env( this->block_object_shader, frame_env );

    // Item sprites also share object_fragment.glsl — same fog/water set.
    world_apply_frame_env( this->item_sprite_shader, frame_env );

    // Mobs share object_fragment.glsl — same fog/water set.
    world_apply_frame_env( this->mob_shader, frame_env );

    if ( useFrameBuffer ) {
        this->frameBuffer.bind( );
        // Clear each attachment separately:
        // - Color (0): alpha=1 so the FBO is opaque for compositing.
        // - Reflection (1): alpha=0 so non-water pixels are discarded during
        //   reflection compositing (only water reflections should show).
        // - Fog (2): alpha=1 so unwritten pixels show full sky in fog compositing.
        GLfloat clearColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
        GLfloat clearReflection[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
        glClearBufferfv( GL_COLOR, 0, clearColor );
        glClearBufferfv( GL_COLOR, 1, clearReflection );
        glClearBufferfv( GL_COLOR, 2, clearColor );
        glClearBufferfv( GL_COLOR, 3, clearColor );
        glClearBufferfi( GL_DEPTH_STENCIL, 0, 1.0f, 0 );
        showErrors( );
        GLenum bufs[ 4 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
        glDrawBuffers( 4, bufs );
        showErrors( );
    } else {
        if constexpr ( SUPPORTS_FRAME_BUFFER ) {
            FrameBuffer::bind_display( );
        }
    }

    // Per-attachment blend control: color attachment uses normal alpha blending
    // (opaque terrain overwrites sky, water blends with terrain behind it).
    // Reflection attachment uses replace mode so the fog factor stored in its
    // alpha is written directly without being alpha-blended (which would
    // corrupt it to alphaFog^2).
#if ( SUPPORTS_FRAME_BUFFER )
    if ( useFrameBuffer ) {
        glBlendFuncSeparatei( 0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        glBlendFuncSeparatei( 1, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        glBlendFuncSeparatei( 2, GL_ONE, GL_ZERO, GL_ONE, GL_ZERO );
        glBlendFuncSeparatei( 3, GL_ONE, GL_ZERO, GL_ONE, GL_ZERO );
    }
#endif

    this->object_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->object_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->object_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->object_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->object_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->object_shader.set_uniform1i( "u_IsSky", 0 );
    // Read only by the sky pass: cube-mapped constant noise instead of the
    // day-lit panorama when the camera is in the End.
    this->object_shader.set_uniform1i( "u_EndSky", camera_dim == DIM_END ? 1 : 0 );

    this->debris_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->debris_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->debris_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->debris_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->debris_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->debris_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->debris_shader.set_uniform1i( "u_IsSky", 0 );

    this->primed_tnt_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->primed_tnt_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->primed_tnt_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->primed_tnt_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->primed_tnt_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->primed_tnt_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->primed_tnt_shader.set_uniform1i( "u_IsSky", 0 );

    this->block_object_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->block_object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->block_object_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->block_object_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->block_object_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->block_object_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->block_object_shader.set_uniform1i( "u_IsSky", 0 );

    this->item_sprite_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->item_sprite_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->item_sprite_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->item_sprite_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->item_sprite_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->item_sprite_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->item_sprite_shader.set_uniform1i( "u_IsSky", 0 );

    this->mob_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->mob_shader.set_uniform1i_texture( "u_Texture", this->mobsTexture );
    this->mob_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->mob_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->mob_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->mob_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->mob_shader.set_uniform1i( "u_IsSky", 0 );

    this->heart_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->heart_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->heart_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->heart_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->heart_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->heart_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->heart_shader.set_uniform1i( "u_IsSky", 0 );
#if ( SUPPORTS_FRAME_BUFFER )
    if ( useFrameBuffer ) {
        // Enable sky color attachment writing for the sky pass only.
        glColorMaski( 3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    }
#endif
    this->object_shader.set_uniform_mat4f( "u_MVP", mvp_sky );
    this->object_shader.set_uniform1i( "u_IsSky", 1 );
    // Sky uses mvp_sky (no view translation), so it must NOT be rebased.
    this->object_shader.set_uniform3f( "u_Origin", 0.0f, 0.0f, 0.0f );
    // The sky is the backdrop: draw it first with depth testing off so it
    // leaves no depth and can never clip terrain, whatever the render
    // distance. Everything drawn after (mobs, debris, terrain) overwrites it.
    glDisable( GL_DEPTH_TEST );
    this->skyBox.draw( this->renderer, this->object_shader ); // Sky
    glEnable( GL_DEPTH_TEST );
#if ( SUPPORTS_FRAME_BUFFER )
    if ( useFrameBuffer ) {
        // Mask out sky color attachment for everything else so the sky color
        // written by the sky pass is preserved (they would overwrite it with black).
        glColorMaski( 3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
    }
#endif
    this->object_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->object_shader.set_uniform1i( "u_IsSky", 0 );
    this->object_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
    // skyBox.draw bound its own texture to u_Texture — restore the block
    // atlas or the avatars sample sky texels.
    this->object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );

    // Refresh baked-in light samples before drawing. Avatars get no position
    // updates while standing still; debris spawns while the broken cell's
    // recheck is still queued — per-frame resampling covers both. Runs on
    // the render thread like the light probes.
    this->blockDebris.update_lighting( this->chunkLoader );
    // Rebuild every mob part transform with the tick-interpolated pose.
    this->mobs.update_render( alpha, this->chunkLoader, &this->heartParticles, time_s );
    // Rebuild the block-body transforms (falling blocks + primed TNT).
    this->bodies.update_render( alpha, this->chunkLoader );
    // Rebuild the dropped-item transforms (cubes + extruded sprites).
    this->items.update_render( alpha, this->chunkLoader );
    // Rebuild the arrow transforms (velocity-aligned extruded sprites; fireballs billboard).
    this->arrows.update_render( alpha, this->chunkLoader, view_look );
    // Rebuild the minecart part transforms (mob model cubes).
    this->vehicles.update_render( alpha, this->chunkLoader );
    this->blockDebris.draw( this->renderer, this->debris_shader, time_s );      // Break particles
    this->bodies.draw( this->renderer, this->block_object_shader, this->primed_tnt_shader ); // Falling blocks + ignited TNT
    this->items.draw( this->renderer, this->block_object_shader, this->item_sprite_shader ); // Dropped items
    this->arrows.draw( this->renderer, this->item_sprite_shader );              // Arrows in flight/stuck
    this->mobs.draw( this->renderer, this->mob_shader );                        // Mob parts
    this->vehicles.draw( this->renderer, this->mob_shader );                       // Vehicles (mob skin/model)
    this->mobs.draw_held( this->renderer, this->block_object_shader, this->item_sprite_shader ); // Avatar held items
    this->heartParticles.draw( this->renderer, this->heart_shader, time_s );    // Breeding hearts

    this->chunkLoader.calculate_cull( mvp, false, renderOrigin );
    this->chunkLoader.shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->chunkLoader.shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->chunkLoader.shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->chunkLoader.shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    // Flipbook animation (anim_layer in chunk_fragment) runs in every pass —
    // the fluid shading below only gates the extra X_HIGH effects.
    this->chunkLoader.shader.set_uniform1f( "u_Time", time_s );
    this->chunkLoader.shader.set_uniform3f( "u_DebugScaleOffset", debug_block_scale, debug_block_scale, debug_block_scale );
    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
    this->chunkLoader.shader.set_uniform1f( "u_ReflectionDotSign", y_height < sea_surface_y ? -1.0f : 1.0f );
    // shader_set_uniform1f( &this->loadedChunks.shader, "u_ReflectionDotSign", 1.0f );
    // REPGAME_TRANSLUCENT_AFTER_WATER moves the translucent order out of the
    // main pass: After = its own draw after the water pass (below); Split =
    // per-fragment split at the sea-level water plane — the far side draws
    // right here (same position as the default order), the camera side after
    // water.
    const unsigned order_all = ( 1u << LAST_RENDER_ORDER ) - 1;
    // Order 0 (Transparent) is the fluid-boundary film bucket: it draws in
    // its own call right after the water pass below, never in these masks.
    const unsigned order_no_water = order_all & ~render_order_bit( RenderOrder_Water ) & ~render_order_bit( RenderOrder_Transparent );
    const TranslucentWaterOrder water_order = translucent_water_order( );
    const float split_y = sea_surface_y - renderOriginF.y;
    const bool camera_above = camera_pos_rebased.y >= split_y;
    this->chunkLoader.shader.set_uniform1f( "u_SplitY", split_y );
    this->chunkLoader.shader.set_uniform1f( "u_SplitSign", 0.0f );
    this->chunkLoader.draw( mvp, this->renderer, blocksTexture,
                            order_no_water & ~( water_order != TranslucentWaterOrder::Before ? render_order_bit( RenderOrder_Translucent ) : 0u ),
                            false, useFrameBuffer, renderOriginF, camera_pos_rebased, cam_forward ); // Blocks

    if ( water_order == TranslucentWaterOrder::Split ) {
        // Far side of the plane first: below the surface seen from above,
        // above it seen from below. u_SplitSign keeps only those fragments.
        this->chunkLoader.shader.set_uniform1f( "u_SplitSign", camera_above ? -1.0f : 1.0f );
        this->chunkLoader.draw( mvp, this->renderer, blocksTexture, render_order_bit( RenderOrder_Translucent ), false, useFrameBuffer, renderOriginF, camera_pos_rebased, cam_forward ); // Translucent, far side
        this->chunkLoader.shader.set_uniform1f( "u_SplitSign", 0.0f );
    }

    // Sign front text + remote-player nametags: batched glyph quads. After
    // the opaque/glass block pass so depth culls text behind terrain; before
    // the selection outline and the water passes.
    {
        const glm::ivec3 camera_block = glm::ivec3( glm::floor( camera_pos ) );
        std::vector<WorldSign> signs;
        signs.reserve( 32 );
        this->collect_signs_near( camera_block, SIGN_TEXT_DRAW_RADIUS, signs );
        this->worldText.begin( );
        for ( const WorldSign &sign : signs ) {
            // Ink is pure black, like Minecraft signs — it stays readable
            // wherever the board behind it is lit.
            const float tint[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
            const glm::vec3 pos_rebased = glm::vec3( sign.pos - renderOrigin );
            this->worldText.append_sign( pos_rebased, sign.state, *sign.text, tint );
        }
        // Nametags yaw-billboard over each live avatar's head at the eased
        // render pose (the same interp the mob model draws with), dimmed
        // while the player sneaks. Past ~64 blocks the plate is unreadable,
        // so they're skipped instead of shimmering in the fog.
        if ( this->multiplayer != nullptr ) {
            constexpr float NAMETAG_MAX_DIST = 64.0f;
            constexpr float NAMETAG_LIFT = 0.45f;   // gap above the hitbox top
            constexpr float NAMETAG_HEIGHT = 0.24f; // cap+descender band, blocks
            glm::vec3 tag_fwd( cam_forward.x, 0.0f, cam_forward.z );
            if ( glm::length( tag_fwd ) < 1.0e-3f ) {
                // Looking straight up/down: fall back to the camera's
                // horizontal right so the basis can't degenerate.
                tag_fwd = glm::vec3( -cam_right.x, 0.0f, -cam_right.z );
            }
            tag_fwd = glm::normalize( tag_fwd );
            const glm::vec3 tag_right = glm::normalize( glm::cross( tag_fwd, glm::vec3( 0.0f, 1.0f, 0.0f ) ) );
            for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
                const uint32_t uid = this->multiplayer_avatars.uid_of( fd );
                if ( uid == 0 ) {
                    continue;
                }
                glm::dvec3 head;
                float sneak = 0.0f;
                if ( !this->mobs.remote_head_pos( uid, alpha, head, sneak ) ) {
                    continue;
                }
                const glm::dvec3 delta = head - camera_pos;
                if ( glm::dot( delta, delta ) > NAMETAG_MAX_DIST * NAMETAG_MAX_DIST ) {
                    continue;
                }
                char fallback[ 24 ];
                const std::string *name = this->multiplayer->name_of( fd );
                const char *text = nullptr;
                int len = 0;
                if ( name != nullptr ) {
                    text = name->c_str( );
                    len = static_cast<int>( name->size( ) );
                } else {
                    len = snprintf( fallback, sizeof( fallback ), "Player%d", fd );
                    text = fallback;
                }
                const glm::vec3 anchor = glm::vec3( head - glm::dvec3( renderOrigin ) ) + glm::vec3( 0.0f, NAMETAG_LIFT, 0.0f );
                this->worldText.append_billboard( anchor, tag_right, tag_fwd, text, len, NAMETAG_HEIGHT, 1.0f - 0.7f * sneak );
            }
        }
        this->worldText.flush( this->renderer, mvp, camera_pos_rebased, renderOriginF, fog_near, fog_far, water_fog_near, water_fog_far,
                               fog_blend_color, water_murk, underwater_wash, useFogBlend ? 1 : 0, block_water_tint_type, sea_surface_y );
    }

    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", 0 );
    if ( draw_mouse_selection ) {
        // First of two outline draws (the second is after the translucent
        // pass below). Depth-test but no depth write: the outline box is
        // pulled toward the camera, and the water/translucent passes draw
        // after this — outline-band fragments would punch see-through holes
        // in them if they left depth. Water composites over this draw, so
        // for a selection behind the surface this is the water-dimmed
        // version the second draw's depth test can't provide.
        glDepthMask( GL_FALSE );
        this->mouseSelection.draw( this->renderer, mvp, renderOriginF );
        glDepthMask( GL_TRUE );
    }

    // Per-attachment blend is already set correctly: color uses normal alpha
    // blending (water blends with terrain behind it), reflection uses replace
    // (fog factor written directly). No need to restore here.

    // The stencil buffer separates water from terrain for the reflection
    // compositing pass. On platforms without reflections (Android/WASM),
    // skip the stencil setup — the water still draws to the color buffer,
    // but without the per-fragment stencil write overhead.
    if ( usingReflections ) {
        glEnable( GL_STENCIL_TEST );
        glStencilFunc( GL_ALWAYS, 1, 0xff );
        glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
    }

    // X_HIGH: animated fluid shading (scrolling UVs + fresnel alpha). Only the
    // water pass contains RenderOrder_Water fragments, so this only affects
    // water/lava top faces. u_TintUnderWater must be restored: the fog
    // attachment uses replace blending, so the translucent surface fragment
    // overwrites the terrain's fog factor — with air-range fog here, distant
    // terrain stays visible through water while submerged.
    this->chunkLoader.shader.set_uniform1i( "u_FluidAnim", allowBlur ? 1 : 0 );
    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
    this->chunkLoader.draw( mvp, this->renderer, blocksTexture, render_order_bit( RenderOrder_Water ), false, useFrameBuffer, renderOriginF, camera_pos_rebased, cam_forward ); // Stencil water

    // Fluid-boundary films (order-0 bucket, chunk_mesh.cpp): the faces a
    // fluid emits against a translucent block — kept out of the water pass
    // because the fluid index buffer draws both windings and a quad at the
    // boundary would render its mirror on the neighbour side, plus its
    // depth write would eat the coplanar glass face. They draw here
    // instead — single-sided (chunk_render.cpp's film index table), after
    // water and before the translucent pass, so the glass still blends
    // over them like anything else seen through a pane. No depth write:
    // the glass face shares the boundary plane and must still pass the
    // depth test. LEQUAL covers the legacy blend mode, where the glass
    // already wrote depth in the main pass; nothing else ever lands at
    // that plane, so LESS and LEQUAL are equivalent everywhere else.
    glDepthMask( GL_FALSE );
    glDepthFunc( GL_LEQUAL );
    this->chunkLoader.draw( mvp, this->renderer, blocksTexture, render_order_bit( RenderOrder_Transparent ), false, useFrameBuffer, renderOriginF, camera_pos_rebased, cam_forward ); // Water-boundary films
    glDepthFunc( GL_LESS );
    glDepthMask( GL_TRUE );
    this->chunkLoader.shader.set_uniform1i( "u_FluidAnim", 0 );
    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", 0 );

    if ( water_order != TranslucentWaterOrder::Before ) {
        // Glass over water: the translucent pass draws on top of the water
        // surface instead of under it. Stencil op is still KEEP/KEEP/REPLACE
        // from the water pass — stop glass stamping stencil=1 (the water
        // marker for the reflection composite); the code below sets
        // EQUAL/KEEP anyway. Glass writes no depth in alpha/multiply modes,
        // so the depth snapshot right after is unaffected. Split mode clips
        // this draw to the camera's side of the sea-level plane.
        if ( usingReflections ) {
            glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
        }
        if ( water_order == TranslucentWaterOrder::Split ) {
            this->chunkLoader.shader.set_uniform1f( "u_SplitSign", camera_above ? 1.0f : -1.0f );
        }
        this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
        this->chunkLoader.draw( mvp, this->renderer, blocksTexture, render_order_bit( RenderOrder_Translucent ), false, useFrameBuffer, renderOriginF, camera_pos_rebased, cam_forward ); // Translucent after water
        this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", 0 );
        this->chunkLoader.shader.set_uniform1f( "u_SplitSign", 0.0f );
    }



    if ( draw_mouse_selection ) {
        // Second outline draw, composited over water + translucent so the
        // glass tint can't wash out the band. Glass writes no depth, so it
        // never occludes this draw; water does, so band pixels behind the
        // surface keep the dimmed first draw instead. No depth write — the
        // SSR depth snapshot right below must not see the outline box.
        // Stencil writes stay off so the band can't mark water pixels for
        // the reflection composite (Before order left op=REPLACE).
        glDepthMask( GL_FALSE );
        glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
        this->mouseSelection.draw( this->renderer, mvp, renderOriginF );
        glDepthMask( GL_TRUE );
    }

    if ( usingReflections ) {
#if ( SUPPORTS_FRAME_BUFFER )
        // Snapshot the scene depth for the water shader's screen-space
        // reflections (and the underwater reflection wobble). Must happen
        // here: the reflection pass below clears the depth buffer and
        // rewrites it with mirrored-world depths.
#if defined( REPGAME_GLES )
        // GLES forbids multisample depth blits; both textures are
        // GL_DEPTH24_STENCIL8 multisample with identical storage, so copy the
        // image directly (no framebuffer involvement).
        glCopyImageSubData( this->depthStencilTexture.m_RendererId, GL_TEXTURE_2D_MULTISAMPLE, 0, 0, 0, 0, this->depthTexture.m_RendererId, GL_TEXTURE_2D_MULTISAMPLE, 0, 0, 0, 0, this->fboWidth, this->fboHeight, 1 );
        this->frameBuffer.bind( );
        showErrors( );
#else
        glBindFramebuffer( GL_READ_FRAMEBUFFER, this->frameBuffer.id( ) );
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, this->depthFrameBuffer.id( ) );
        glBlitFramebuffer( 0, 0, this->fboWidth, this->fboHeight, 0, 0, this->fboWidth, this->fboHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST );
        this->frameBuffer.bind( );
        showErrors( );
#endif
#endif

        glStencilFunc( GL_EQUAL, 1, 0xff );
        glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

        glCullFace( GL_FRONT );
        glClear( GL_DEPTH_BUFFER_BIT );
        // Don't overwrite the fog factors or sky color written during the
        // normal pass. Mask out the fog attachment (index 2) and sky color
        // attachment (index 3) during the reflection pass.
#if ( SUPPORTS_FRAME_BUFFER )
        glColorMaski( 2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
        glColorMaski( 3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
#endif
        // Clip-plane offset: v_planarDot = worldY + u_ReflectionHeight, so
        // -sea_surface_y keeps the discard on the camera's side of the
        // dimension's sea surface (WATER_HEIGHT - 1 in the overworld).
        float offset = -sea_surface_y;

        // Draw the reflected sky FIRST, so reflected terrain blends with the
        // sky in the reflection texture (matching the normal pass order).
        // Otherwise distant reflected terrain blends with the black background.
        this->object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
        this->object_shader.set_uniform1f( "u_ReflectionHeight", offset );
        this->object_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
        this->object_shader.set_uniform1i( "u_DrawToReflection", 1 );
        this->object_shader.set_uniform_mat4f( "u_MVP", mvp_sky_reflect );
        this->object_shader.set_uniform1i( "u_IsSky", 1 );
        // Reflected sky: no view translation, so u_Origin = 0 and u_ReflectionHeight
        // stays in absolute (unrebased) space. Drawn without depth testing for
        // the same reason as the main sky pass: it's the backdrop reflected
        // terrain draws over (the depth buffer was just cleared above).
        this->object_shader.set_uniform3f( "u_Origin", 0.0f, 0.0f, 0.0f );
        glDisable( GL_DEPTH_TEST );
        this->skyBox.draw( this->renderer, this->object_shader ); // Reflected sky
        glEnable( GL_DEPTH_TEST );

        this->object_shader.set_uniform_mat4f( "u_MVP", mvp_reflect );
        this->object_shader.set_uniform1i( "u_IsSky", 0 );
        // Reflected mobs: rebased like normal mobs. u_ReflectionHeight must be
        // rebased so v_planarDot = (worldY - origin.y) + (offset + origin.y) = worldY + offset.
        this->object_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
        // Same as the main pass: the reflected sky draw left its texture bound.
        this->object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
        this->object_shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->mob_shader.set_uniform1i_texture( "u_Texture", this->mobsTexture );
        this->mob_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
        this->mob_shader.set_uniform1i( "u_DrawToReflection", 1 );
        this->mob_shader.set_uniform1i( "u_IsSky", 0 );
        this->mob_shader.set_uniform_mat4f( "u_MVP", mvp_reflect );
        this->mob_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
        this->mob_shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->mobs.draw( this->renderer, this->mob_shader ); // Reflected mob parts

        this->debris_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
        this->debris_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
        this->debris_shader.set_uniform1i( "u_DrawToReflection", 1 );
        this->debris_shader.set_uniform1i( "u_IsSky", 0 );
        this->debris_shader.set_uniform_mat4f( "u_MVP", mvp_reflect );
        this->debris_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
        this->debris_shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->blockDebris.draw( this->renderer, this->debris_shader, time_s ); // Reflected debris

        this->heart_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
        this->heart_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
        this->heart_shader.set_uniform1i( "u_DrawToReflection", 1 );
        this->heart_shader.set_uniform1i( "u_IsSky", 0 );
        this->heart_shader.set_uniform_mat4f( "u_MVP", mvp_reflect );
        this->heart_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
        this->heart_shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->heartParticles.draw( this->renderer, this->heart_shader, time_s ); // Reflected hearts

        this->primed_tnt_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
        this->primed_tnt_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
        this->primed_tnt_shader.set_uniform1i( "u_DrawToReflection", 1 );
        this->primed_tnt_shader.set_uniform1i( "u_IsSky", 0 );
        this->primed_tnt_shader.set_uniform_mat4f( "u_MVP", mvp_reflect );
        this->primed_tnt_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
        this->primed_tnt_shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->primed_tnt_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
        this->block_object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
        this->block_object_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
        this->block_object_shader.set_uniform1i( "u_DrawToReflection", 1 );
        this->block_object_shader.set_uniform1i( "u_IsSky", 0 );
        this->block_object_shader.set_uniform_mat4f( "u_MVP", mvp_reflect );
        this->block_object_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
        this->block_object_shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->block_object_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
        this->item_sprite_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
        this->item_sprite_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
        this->item_sprite_shader.set_uniform1i( "u_DrawToReflection", 1 );
        this->item_sprite_shader.set_uniform1i( "u_IsSky", 0 );
        this->item_sprite_shader.set_uniform_mat4f( "u_MVP", mvp_reflect );
        this->item_sprite_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
        this->item_sprite_shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->item_sprite_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
        this->bodies.draw( this->renderer, this->block_object_shader, this->primed_tnt_shader ); // Reflected bodies
        this->items.draw( this->renderer, this->block_object_shader, this->item_sprite_shader ); // Reflected items
        this->arrows.draw( this->renderer, this->item_sprite_shader );              // Reflected arrows
        this->mobs.draw_held( this->renderer, this->block_object_shader, this->item_sprite_shader ); // Reflected held items

        this->chunkLoader.shader.set_uniform1i( "u_DrawToReflection", true );
        this->chunkLoader.shader.set_uniform1f( "u_ExtraAlpha", 1.0f ); // this make reflections not solid...
        this->chunkLoader.shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
        this->chunkLoader.calculate_cull( mvp_reflect, true, renderOrigin );
        this->chunkLoader.draw( mvp_reflect, this->renderer, blocksTexture, order_no_water, true, useFrameBuffer, renderOriginF, camera_pos_rebased, cam_forward ); // Reflected blocks

        this->chunkLoader.shader.set_uniform1f( "u_ReflectionHeight", 0 );
        this->object_shader.set_uniform1f( "u_ReflectionHeight", 0 );
        this->mob_shader.set_uniform1f( "u_ReflectionHeight", 0 );

        // Restore fog and sky color attachment writing.
#if ( SUPPORTS_FRAME_BUFFER )
        glColorMaski( 2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glColorMaski( 3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
#endif

        glCullFace( GL_BACK );
    }
    glDisable( GL_STENCIL_TEST );

    // Restore normal alpha blending for the compositing pass.
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    if ( useFrameBuffer ) {
        FrameBuffer::bind_display( );
        showErrors( );
        glDisable( GL_DEPTH_TEST );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

        // Fog compositing blends the opaque terrain (with fog factor in the
        // fog texture alpha) with the actual sky in post-processing.
        // The sky color is sampled from a dedicated attachment that captured
        // the actual rendered sky before terrain was drawn, so it matches the
        // skybox exactly (no reconstruction math needed).

        if ( usingReflections ) {
            // Bind the SSR/water frame state once: the refraction wobble in the
            // fog-composite draw, the water overlay, and the underwater
            // reflection wobble all unproject with it.
            this->fullScreenQuad.set_water_frame_uniforms( this->depthTexture, inv_mvp, camera_pos_rebased, renderOriginF, sea_surface_y );
        }

        if ( usingReflections && !headInWater ) {
            // glStencilFunc -> pass or discard
            // glStencilOp -> action to do on the scencil buffer.

            glEnable( GL_STENCIL_TEST );
            glStencilFunc( GL_ALWAYS, 1, 0xFF );               // All fragments should pass the stencil buffer. It's empty anyway.
            glStencilOp( GL_REPLACE, GL_REPLACE, GL_REPLACE ); // Any drawing impacts the stencil buffer and writes a 1
            // Mark the default framebuffer's stencil wherever the reflection
            // texture has content (= fluid pixels). Color writes are masked —
            // the shading result of this draw is not needed.
            glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
            this->fullScreenQuad.draw_texture( this->renderer, this->reflectionTexture, this->depthStencilTexture, 1.0, false, false, 1 );
            glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

            glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP ); // Don't change the stencil buffer while we're using it.
            glStencilFunc( GL_EQUAL, 1, 0xff );       // If the stencil value is 1, allow drawing.
            if ( useFogBlend ) {
                // Use fog blend for water areas too, so the terrain/water
                // fades into the sky and outputs opaque alpha (no blending
                // with the black background cleared above). The ripple flag
                // wobbles the fetch, so submerged terrain shimmers.
                this->fullScreenQuad.draw_texture_fog( this->renderer, this->blockTexture, this->depthStencilTexture, this->fogTexture, this->skyColorTexture, 1.0, true, headInWater, 0, time_s, allowBlur,
                                                      false, water_murk );
            } else {
                this->fullScreenQuad.draw_texture( this->renderer, this->blockTexture, this->depthStencilTexture, 1.0, true, headInWater );
            }
            // Water FX overlay: fresnel-weighted reflection (planar for
            // sea-level fluids, screen-space marched for elevated ones),
            // blended over the scene just drawn.
            this->fullScreenQuad.draw_water( this->renderer, this->reflectionTexture, this->depthStencilTexture, this->fogTexture, this->skyColorTexture, this->blockTexture, this->depthTexture,
                                             y_height < sea_surface_y ? 0.4f : 0.6f, time_s, mvp, inv_mvp, renderOriginF, camera_pos_rebased, allowBlur, fog_blend_color );
            glStencilFunc( GL_NOTEQUAL, 1, 0xff ); // If the stencil value isn't 1 allow drawing.
            if ( useFogBlend ) {
                this->fullScreenQuad.draw_texture_fog( this->renderer, this->blockTexture, this->depthStencilTexture, this->fogTexture, this->skyColorTexture, 1.0, false, headInWater, 0, 0.0f, false,
                                                      false, water_murk );
            } else {
                this->fullScreenQuad.draw_texture( this->renderer, this->blockTexture, this->depthStencilTexture, 1.0, false, headInWater );
            }
            glDisable( GL_STENCIL_TEST );
        } else {
            // u_Underwater (blue wash) only when fully submerged — the
            // straddle band keeps the above-water half of the view clean.
            // No blur: submerged terrain is viewed *inside* the medium, so it
            // shouldn't smear — murk comes from the fog blend instead.
            this->fullScreenQuad.draw_texture_fog( this->renderer, this->blockTexture, this->depthStencilTexture, this->fogTexture, this->skyColorTexture, 1.0, false, headInWater, 0, 0.0f, false,
                                                   block_water_tint_type == TINT_UNDER_WATER_OBJECT_ALWAYS, water_murk );
        }

        // Underwater keeps the simple uniform-strength reflection overlay;
        // above water, draw_water already composited the reflections.
        if ( usingReflections && headInWater ) {
            // X_HIGH wobbles the mirrored fetch, matching the land-side
            // rippled reflections; HIGH keeps it still. Kept faint so the
            // surface doesn't mirror away the world above.
            this->fullScreenQuad.draw_texture( this->renderer, this->reflectionTexture, this->depthStencilTexture, y_height < sea_surface_y ? 0.05 : 0.1, allowBlur, headInWater, 1, false, time_s, allowBlur );
        }
        glEnable( GL_DEPTH_TEST );
    }
    // chunk_loader_draw_chunks( &this->loadedChunks, mvp, &this->renderer, true, false ); // Water
    glDisable( GL_STENCIL_TEST );
}
