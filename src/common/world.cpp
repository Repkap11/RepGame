#include "common/world.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"
#include "common/multiplayer.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/screenshot.hpp"
#include "common/RepGame.hpp"
#include "common/tree_grow.hpp"
#include "common/light.hpp"
#include "common/fluid.hpp"
#include "common/block_update_events/FluidUpdateEvent.hpp"

#include <stdlib.h>
#include <string>

#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

MK_SHADER( object_vertex );
MK_SHADER( object_fragment );
MK_SHADER( debris_vertex );
MK_SHADER( heart_vertex );
MK_SHADER( primed_tnt_vertex );
MK_SHADER( block_object_vertex );
MK_SHADER( mob_vertex );
MK_SHADER( item_sprite_vertex );

MK_TEXTURE( mobs, 64, 64 * MOB_SKIN_COUNT, 64, 64 );

bool hasError( ) {
    if ( ( glGetError( ) ) != GL_NO_ERROR ) {
        return true;
    }
    return false;
}

void World::init( const glm::dvec3 &camera_pos, int width, int height, MapStorage &map_storage, int render_radius ) {
    // These are from CubeFace
    this->vbl_block.push_float( 3 );        // Coords
    this->vbl_block.push_float( 2 );        // Texture coords
    this->vbl_block.push_unsigned_int( 1 ); // Face type (top, sides, bottom)
    this->vbl_block.push_unsigned_int( 1 ); // Corner_shift

    // These are from BlockCoords
    this->vbl_coords.push_float( 3 );        // block 3d world coords
    this->vbl_coords.push_unsigned_int( 1 ); // Multiples (mesh)
    this->vbl_coords.push_unsigned_int( 3 ); // which texture
    this->vbl_coords.push_unsigned_int( 3 ); // packed lighting
    this->vbl_coords.push_unsigned_int( 3 ); // packed lightingMOB_ROTATION
    this->vbl_coords.push_unsigned_int( 1 ); // face_shift for face rotation
    this->vbl_coords.push_signed_byte( 3 );  // block shape scale (pixels)
    this->vbl_coords.push_signed_byte( 3 );  // block shape offset (pixels)
    this->vbl_coords.push_signed_byte( 3 );  // texture shape offset (pixels)
    this->vbl_coords.push_byte( 3 );         // fluid_h corner heights (padding for non-fluids)

    // These are from ParticleVertex
    this->vbl_object_vertex.push_float( 3 );        // Coords
    this->vbl_object_vertex.push_float( 2 );        // TxCoords
    this->vbl_object_vertex.push_unsigned_int( 1 ); // faceType

    // These are from ParticlePosition
    this->vbl_object_position.push_unsigned_int( 3 ); // which texture
    this->vbl_object_position.push_float( 4 );        // transform
    this->vbl_object_position.push_float( 4 );        // transform
    this->vbl_object_position.push_float( 4 );        // transform
    this->vbl_object_position.push_float( 4 );        // transform
    this->vbl_object_position.push_float( 2 );        // light (sky, block)

    // These are from DebrisInstance
    this->vbl_debris_instance.push_unsigned_int( 3 ); // which texture
    this->vbl_debris_instance.push_float( 3 );        // spawn
    this->vbl_debris_instance.push_float( 3 );        // velocity
    this->vbl_debris_instance.push_float( 4 );        // anim (spawn_time, life, size, seed)
    this->vbl_debris_instance.push_float( 2 );        // light (sky, block)
    this->vbl_debris_instance.push_float( 2 );        // texel (drawn-window texel center)

    // These are from PrimedTntInstance
    this->vbl_primed_tnt_instance.push_unsigned_int( 3 ); // which texture
    this->vbl_primed_tnt_instance.push_float( 4 );        // transform
    this->vbl_primed_tnt_instance.push_float( 4 );        // transform
    this->vbl_primed_tnt_instance.push_float( 4 );        // transform
    this->vbl_primed_tnt_instance.push_float( 4 );        // transform
    this->vbl_primed_tnt_instance.push_float( 2 );        // light (sky, block)
    this->vbl_primed_tnt_instance.push_float( 1 );        // flash

    // These are from BlockObjectInstance
    this->vbl_block_object_instance.push_unsigned_int( 3 ); // which texture
    this->vbl_block_object_instance.push_float( 4 );        // transform
    this->vbl_block_object_instance.push_float( 4 );        // transform
    this->vbl_block_object_instance.push_float( 4 );        // transform
    this->vbl_block_object_instance.push_float( 4 );        // transform
    this->vbl_block_object_instance.push_float( 2 );        // light (sky, block)
    this->vbl_block_object_instance.push_float( 3 );        // shape scale (pixels)
    this->vbl_block_object_instance.push_float( 3 );        // shape offset (pixels)
    this->vbl_block_object_instance.push_float( 3 );        // texture shape offset (pixels)

    // These are from MobPartInstance
    this->vbl_mob_instance.push_float( 4 ); // transform
    this->vbl_mob_instance.push_float( 4 ); // transform
    this->vbl_mob_instance.push_float( 4 ); // transform
    this->vbl_mob_instance.push_float( 4 ); // transform
    this->vbl_mob_instance.push_float( 2 ); // light (sky, block)
    this->vbl_mob_instance.push_float( 4 ); // uv_a (u, v, w, h in skin px)
    this->vbl_mob_instance.push_float( 4 ); // uv_b (d, skin_layer, mirror, flash)

    // These are from ItemSpriteInstance
    this->vbl_item_instance.push_unsigned_int( 1 ); // atlas layer
    this->vbl_item_instance.push_float( 4 );        // transform
    this->vbl_item_instance.push_float( 4 );        // transform
    this->vbl_item_instance.push_float( 4 );        // transform
    this->vbl_item_instance.push_float( 4 );        // transform
    this->vbl_item_instance.push_float( 2 );        // light (sky, block)

    this->chunkLoader.init( camera_pos, this->vbl_block, this->vbl_coords, map_storage, render_radius );


    this->object_shader.init( &object_vertex, &object_fragment );
    this->debris_shader.init( &debris_vertex, &object_fragment );
    this->heart_shader.init( &heart_vertex, &object_fragment );
    this->primed_tnt_shader.init( &primed_tnt_vertex, &object_fragment );
    this->block_object_shader.init( &block_object_vertex, &object_fragment );
    this->mob_shader.init( &mob_vertex, &object_fragment );
    this->item_sprite_shader.init( &item_sprite_vertex, &object_fragment );

    this->skyBox.init( this->vbl_object_vertex, this->vbl_object_position );
    this->multiplayer_avatars.init( &this->mobs );
    this->blockDebris.init( this->vbl_object_vertex, this->vbl_debris_instance );
    this->heartParticles.init( this->vbl_object_vertex, this->vbl_debris_instance );
    this->bodies.init( this->vbl_object_vertex, this->vbl_block_object_instance, this->vbl_primed_tnt_instance );
    this->items.init( this->vbl_object_vertex, this->vbl_block_object_instance, this->vbl_item_instance );
    // The sprite program alpha-tests the atlas at exact texels (no mip bleed).
    this->item_sprite_shader.set_uniform1i( "u_ExactTexel", 1 );
    this->mobs.init( this->vbl_object_vertex, this->vbl_mob_instance );
    this->mobs.init_storage( &map_storage );
    this->items.init_storage( &map_storage );
    this->mouseSelection.init( this->vbl_block, this->vbl_coords );

    // Mob skins: NEAREST like the block atlas but no mipmaps — face UV
    // islands are a few pixels wide and would bleed at distance.
    this->mobsTexture.init( texture_source_mobs, 0, false );

#if ( SUPPORTS_FRAME_BUFFER )
    this->frameBuffer.init( );
    this->frameBuffer.bind( );

    showErrors( );
    this->reflectionTexture.init_empty_color( 0 );
    this->blockTexture.init_empty_color( 0 );
    this->fogTexture.init_empty_color( 0 );
    this->skyColorTexture.init_empty_color( 0 );
    this->depthStencilTexture.init_empty_depth_stencil( 0 );
    this->depthTexture.init_empty_depth( 0 );
    showErrors( );

    this->blockTexture.change_size( width, height );
    showErrors( );
    this->reflectionTexture.change_size( width, height );
    showErrors( );
    this->fogTexture.change_size( width, height );
    showErrors( );
    this->skyColorTexture.change_size( width, height );
    showErrors( );
    this->depthStencilTexture.change_size( width, height );
    showErrors( );
    this->depthTexture.change_size( width, height );
    showErrors( );

    this->frameBuffer.attach_texture( this->blockTexture, 0 );
    this->frameBuffer.attach_texture( this->reflectionTexture, 1 );
    this->frameBuffer.attach_texture( this->fogTexture, 2 );
    this->frameBuffer.attach_texture( this->skyColorTexture, 3 );
    this->frameBuffer.attach_texture( this->depthStencilTexture, 0 ); // 0 is fake
    // A depth-only sibling framebuffer that gets a blit of the scene depth
    // each frame, so the water shader can ray-march it for reflections.
    this->depthFrameBuffer.init( );
    this->depthFrameBuffer.attach_texture( this->depthTexture, 0 ); // -> GL_DEPTH_ATTACHMENT
    this->fboWidth = width;
    this->fboHeight = height;
    showErrors( );
    this->fullScreenQuad.init( );
    showErrors( );
    FrameBuffer::bind_display( );
    showErrors( );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
    showErrors( );
#endif
}

void World::change_size( int width, int height ) {
    if constexpr ( SUPPORTS_FRAME_BUFFER ) {
        if ( !this->frameBuffer.ok( ) ) {
            pr_debug( "Frame buffer not ok" );
            showErrors( );
            exit( 1 );
        }
        this->blockTexture.change_size( width, height );
        showErrors( );
        this->reflectionTexture.change_size( width, height );
        showErrors( );
        this->fogTexture.change_size( width, height );
        showErrors( );
        this->skyColorTexture.change_size( width, height );
        showErrors( );
        this->depthStencilTexture.change_size( width, height );
        showErrors( );
        // The attachment refers to the texture object, not its storage, so a
        // plain change_size keeps it attached.
        this->depthTexture.change_size( width, height );
        showErrors( );
#if defined( REPGAME_GLES )
        // On GLES the multisample textures were deleted and regenerated by
        // change_size (immutable glTexStorage2DMultisample), so the FBOs must
        // be re-attached exactly like World::init does.
        this->frameBuffer.attach_texture( this->blockTexture, 0 );
        this->frameBuffer.attach_texture( this->reflectionTexture, 1 );
        this->frameBuffer.attach_texture( this->fogTexture, 2 );
        this->frameBuffer.attach_texture( this->skyColorTexture, 3 );
        this->frameBuffer.attach_texture( this->depthStencilTexture, 0 ); // 0 is fake
        this->depthFrameBuffer.attach_texture( this->depthTexture, 0 );   // -> GL_DEPTH_STENCIL_ATTACHMENT on GLES
        FrameBuffer::bind_display( );
#endif
        this->fboWidth = width;
        this->fboHeight = height;

        if ( !this->frameBuffer.ok( ) ) {
            pr_debug( "Frame buffer not ok" );
            showErrors( );
            exit( 1 );
        }
    }
}

void World::render( Multiplayer &multiplayer, const glm::dvec3 &camera_pos, const int limit_render, const glm::mat4 &rotation ) {
    // In multiplayer the item files live on the server: local reads/writes
    // stay off once the connect resolves (or latches after a drop).
    this->items.set_multiplayer_mode( multiplayer.multiplayer_mode( ) );
    // Same for mobs: local mobs_* files and the natural-spawn roll route
    // through CHUNK_ROLE/populated instead.
    this->mobs.set_multiplayer_mode( multiplayer.multiplayer_mode( ), &multiplayer.entities );
    // Bodies too: causer-simulated spawn announcements and mirrors.
    this->bodies.set_multiplayer_mode( multiplayer.multiplayer_mode( ), &multiplayer.entities );
    this->chunkLoader.render_chunks( multiplayer, camera_pos, limit_render );
    // Hand each just-finalized chunk's pending mobs to the manager (loaded
    // records or the natural-spawn roll); skipped chunks stay owned next pass.
    if ( !this->chunkLoader.just_finalized.empty( ) ) {
        for ( Chunk *chunk : this->chunkLoader.just_finalized ) {
            this->mobs.adopt_chunk( *this, *chunk );
            this->items.adopt_chunk( *chunk );
        }
        this->chunkLoader.just_finalized.clear( );
    }
}

void World::set_selected_block( const glm::ivec3 &selected, const bool shouldDraw ) {
    const BlockState blockState = get_loaded_block( selected );
    if ( blockState.id == LAST_BLOCK_ID ) {
        this->mouseSelection.hide( );
        return;
    }
    this->mouseSelection.set_block( *this, selected, shouldDraw, blockState );
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

    // Distance fog: blend terrain toward the sky color in a band ending at
    // the render edge. The band is a constant number of blocks (capped at
    // the old 38% of render distance so small render distances are
    // unchanged), so the haze stays thin at large render distances instead
    // of scaling up, while still reaching full opacity at the edge to hide
    // chunk pop-in.
    static const float fog_range_blocks = env_float( "REPGAME_FOG_RANGE", 96.0f );
    // The fog band ends at the ACTIVE render edge (runtime radius), not the
    // torus capacity, so pop-in stays hidden at small render distances.
    const float render_distance = static_cast<float>( this->chunkLoader.render_distance_blocks( ) );
    const float fog_far = render_distance * 0.98f;
    const float fog_near = fog_far - fminf( fog_range_blocks, render_distance * 0.38f );
    // Underwater murk: while the camera is in water, everything fogs over
    // this much shorter range — the medium is chosen by the camera, so even
    // blocks above the surface are cut off at the water view distance.
    const float water_fog_near = 8.0f;
    const float water_fog_far = 45.0f;
    // When underwater, the in-shader phase of the fog blend targets this
    // depth-graded water murk (light blue at the surface → navy when deep)
    // instead of the sky average; the composite phase then blends toward the
    // sky attachment, same mechanism as the above-ground fog.
    glm::vec3 fog_blend_color = this->skyBox.get_avg_color( );
    if ( headInWater ) {
        const float depthBelow = ( WATER_HEIGHT - 1.0f ) - y_height; // WATER_LEVEL(0) + WATER_HEIGHT - 1 = surface Y
        const float depthF = glm::clamp( depthBelow / 30.0f, 0.0f, 1.0f );
        fog_blend_color = glm::mix( glm::vec3( 0.07f, 0.20f, 0.68f ), glm::vec3( 0.015f, 0.05f, 0.22f ), depthF );
    }
    // The rendered sky is the sky texture dimmed by u_Daylight, so the fog
    // target must dim the same way or distant terrain blends toward a
    // day-bright color at night.
    fog_blend_color *= daylight;

    this->chunkLoader.shader.set_uniform1f( "u_FogNear", fog_near );
    this->chunkLoader.shader.set_uniform1f( "u_FogFar", fog_far );
    this->chunkLoader.shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    this->chunkLoader.shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    // Framebuffer tiers apply the underwater blue wash in the composite pass;
    // LOW renders forward, so the object/chunk shaders apply it in-shader
    // instead. Same uniform name on purpose: only one of the two is ever
    // active for a given draw.
    const int underwater_wash = ( headInWater && !useFrameBuffer && block_water_tint_type == TINT_UNDER_WATER_OBJECT_ALWAYS ) ? 1 : 0;
    this->chunkLoader.shader.set_uniform1i( "u_Underwater", underwater_wash );
    this->chunkLoader.shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->chunkLoader.shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
    this->chunkLoader.shader.set_uniform3f( "u_SkyAvgColor", fog_blend_color.r, fog_blend_color.g, fog_blend_color.b );
    // No-framebuffer fog blends toward the actual sky color (chunk_fragment
    // samples the equirect sky texture at the view direction).
    this->chunkLoader.shader.set_uniform1i_texture( "u_SkyTexture", this->skyBox.get_texture( ) );
    // Opaque fog stores the fog factor in a dedicated fog texture (attachment 2)
    // for post-process sky blending. Works in all framebuffer modes since the
    // fog texture is separate from the reflection texture. Underwater it blends
    // toward the sky attachment over the short water-fog range, so distant
    // terrain converges to the same "water background" as the surface/sky.
    const bool useFogBlend = useFrameBuffer;
    this->chunkLoader.shader.set_uniform1i( "u_OpaqueFog", useFogBlend ? 1 : 0 );
    // Daylight scales the sky-light nibble sampled from each chunk's 3D light
    // volume (chunk_fragment) and dims sky/mobs/debris (object_fragment).
    // u_MinAmbient is an absolute light floor so unlit caves are never black.
    this->chunkLoader.shader.set_uniform1f( "u_Daylight", daylight );
    this->chunkLoader.shader.set_uniform1f( "u_MinAmbient", min_ambient );

    this->object_shader.set_uniform1f( "u_FogNear", fog_near );
    this->object_shader.set_uniform1f( "u_FogFar", fog_far );
    this->object_shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    this->object_shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    this->object_shader.set_uniform1i( "u_Underwater", underwater_wash );
    this->object_shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->object_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
    this->object_shader.set_uniform3f( "u_SkyAvgColor", fog_blend_color.r, fog_blend_color.g, fog_blend_color.b );
    this->object_shader.set_uniform1i( "u_OpaqueFog", useFogBlend ? 1 : 0 );
    this->object_shader.set_uniform1f( "u_Daylight", daylight );
    this->object_shader.set_uniform1f( "u_MinAmbient", min_ambient );

    // Debris shares object_fragment.glsl, so it needs the same fog/water set.
    this->debris_shader.set_uniform1f( "u_FogNear", fog_near );
    this->debris_shader.set_uniform1f( "u_FogFar", fog_far );
    this->debris_shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    this->debris_shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    this->debris_shader.set_uniform1i( "u_Underwater", underwater_wash );
    this->debris_shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->debris_shader.set_uniform3f( "u_SkyAvgColor", fog_blend_color.r, fog_blend_color.g, fog_blend_color.b );
    this->debris_shader.set_uniform1i( "u_OpaqueFog", useFogBlend ? 1 : 0 );
    this->debris_shader.set_uniform1f( "u_Daylight", daylight );
    this->debris_shader.set_uniform1f( "u_MinAmbient", min_ambient );
    this->debris_shader.set_uniform1f( "u_Time", time_s );

    // Hearts share object_fragment.glsl too, plus the billboard basis
    // (camera right/up in world space — rows of the view rotation).
    this->heart_shader.set_uniform1f( "u_FogNear", fog_near );
    this->heart_shader.set_uniform1f( "u_FogFar", fog_far );
    this->heart_shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    this->heart_shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    this->heart_shader.set_uniform1i( "u_Underwater", underwater_wash );
    this->heart_shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->heart_shader.set_uniform3f( "u_SkyAvgColor", fog_blend_color.r, fog_blend_color.g, fog_blend_color.b );
    this->heart_shader.set_uniform1i( "u_OpaqueFog", useFogBlend ? 1 : 0 );
    this->heart_shader.set_uniform1f( "u_Daylight", daylight );
    this->heart_shader.set_uniform1f( "u_MinAmbient", min_ambient );
    this->heart_shader.set_uniform1f( "u_Time", time_s );
    const glm::vec3 cam_right( view_look[ 0 ][ 0 ], view_look[ 1 ][ 0 ], view_look[ 2 ][ 0 ] );
    const glm::vec3 cam_up( view_look[ 0 ][ 1 ], view_look[ 1 ][ 1 ], view_look[ 2 ][ 1 ] );
    this->heart_shader.set_uniform3f( "u_CamRight", cam_right.x, cam_right.y, cam_right.z );
    this->heart_shader.set_uniform3f( "u_CamUp", cam_up.x, cam_up.y, cam_up.z );

    // Primed TNT also shares object_fragment.glsl — same fog/water set.
    this->primed_tnt_shader.set_uniform1f( "u_FogNear", fog_near );
    this->primed_tnt_shader.set_uniform1f( "u_FogFar", fog_far );
    this->primed_tnt_shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    this->primed_tnt_shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    this->primed_tnt_shader.set_uniform1i( "u_Underwater", underwater_wash );
    this->primed_tnt_shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->primed_tnt_shader.set_uniform3f( "u_SkyAvgColor", fog_blend_color.r, fog_blend_color.g, fog_blend_color.b );
    this->primed_tnt_shader.set_uniform1i( "u_OpaqueFog", useFogBlend ? 1 : 0 );
    this->primed_tnt_shader.set_uniform1f( "u_Daylight", daylight );
    this->primed_tnt_shader.set_uniform1f( "u_MinAmbient", min_ambient );
    this->primed_tnt_shader.set_uniform1f( "u_Time", time_s );

    // Block objects (non-full-cube items + falling blocks) share
    // object_fragment.glsl too — same fog/water set, no u_Time.
    this->block_object_shader.set_uniform1f( "u_FogNear", fog_near );
    this->block_object_shader.set_uniform1f( "u_FogFar", fog_far );
    this->block_object_shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    this->block_object_shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    this->block_object_shader.set_uniform1i( "u_Underwater", underwater_wash );
    this->block_object_shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->block_object_shader.set_uniform3f( "u_SkyAvgColor", fog_blend_color.r, fog_blend_color.g, fog_blend_color.b );
    this->block_object_shader.set_uniform1i( "u_OpaqueFog", useFogBlend ? 1 : 0 );
    this->block_object_shader.set_uniform1f( "u_Daylight", daylight );
    this->block_object_shader.set_uniform1f( "u_MinAmbient", min_ambient );

    // Item sprites also share object_fragment.glsl — same fog/water set.
    this->item_sprite_shader.set_uniform1f( "u_FogNear", fog_near );
    this->item_sprite_shader.set_uniform1f( "u_FogFar", fog_far );
    this->item_sprite_shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    this->item_sprite_shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    this->item_sprite_shader.set_uniform1i( "u_Underwater", underwater_wash );
    this->item_sprite_shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->item_sprite_shader.set_uniform3f( "u_SkyAvgColor", fog_blend_color.r, fog_blend_color.g, fog_blend_color.b );
    this->item_sprite_shader.set_uniform1i( "u_OpaqueFog", useFogBlend ? 1 : 0 );
    this->item_sprite_shader.set_uniform1f( "u_Daylight", daylight );
    this->item_sprite_shader.set_uniform1f( "u_MinAmbient", min_ambient );

    // Mobs share object_fragment.glsl — same fog/water set.
    this->mob_shader.set_uniform1f( "u_FogNear", fog_near );
    this->mob_shader.set_uniform1f( "u_FogFar", fog_far );
    this->mob_shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    this->mob_shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    this->mob_shader.set_uniform1i( "u_Underwater", underwater_wash );
    this->mob_shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->mob_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
    this->mob_shader.set_uniform3f( "u_SkyAvgColor", fog_blend_color.r, fog_blend_color.g, fog_blend_color.b );
    this->mob_shader.set_uniform1i( "u_OpaqueFog", useFogBlend ? 1 : 0 );
    this->mob_shader.set_uniform1f( "u_Daylight", daylight );
    this->mob_shader.set_uniform1f( "u_MinAmbient", min_ambient );

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

    this->debris_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->debris_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->debris_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->debris_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->debris_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->debris_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->debris_shader.set_uniform1i( "u_IsSky", 0 );
    this->debris_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );

    this->primed_tnt_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->primed_tnt_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->primed_tnt_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->primed_tnt_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->primed_tnt_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->primed_tnt_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->primed_tnt_shader.set_uniform1i( "u_IsSky", 0 );
    this->primed_tnt_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );

    this->block_object_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->block_object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->block_object_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->block_object_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->block_object_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->block_object_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->block_object_shader.set_uniform1i( "u_IsSky", 0 );
    this->block_object_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );

    this->item_sprite_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->item_sprite_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->item_sprite_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->item_sprite_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->item_sprite_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->item_sprite_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->item_sprite_shader.set_uniform1i( "u_IsSky", 0 );
    this->item_sprite_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );

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
    this->heart_shader.set_uniform3f( "u_Origin", renderOriginF.x, renderOriginF.y, renderOriginF.z );
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
    this->blockDebris.draw( this->renderer, this->debris_shader, time_s );      // Break particles
    this->bodies.draw( this->renderer, this->block_object_shader, this->primed_tnt_shader ); // Falling blocks + ignited TNT
    this->items.draw( this->renderer, this->block_object_shader, this->item_sprite_shader ); // Dropped items
    this->mobs.draw( this->renderer, this->mob_shader );                        // Mob parts
    this->heartParticles.draw( this->renderer, this->heart_shader, time_s );    // Breeding hearts

    this->chunkLoader.calculate_cull( mvp, false, renderOrigin );
    this->chunkLoader.shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->chunkLoader.shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->chunkLoader.shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->chunkLoader.shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->chunkLoader.shader.set_uniform3f( "u_DebugScaleOffset", debug_block_scale, debug_block_scale, debug_block_scale );
    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
    this->chunkLoader.shader.set_uniform1f( "u_ReflectionDotSign", y_height < 0 ? -1.0f : 1.0f );
    // shader_set_uniform1f( &this->loadedChunks.shader, "u_ReflectionDotSign", 1.0f );
    // REPGAME_TRANSLUCENT_AFTER_WATER moves the translucent order out of the
    // main pass: After = its own draw after the water pass (below); Split =
    // per-fragment split at the sea-level water plane — the far side draws
    // right here (same position as the default order), the camera side after
    // water.
    const unsigned order_all = ( 1u << LAST_RENDER_ORDER ) - 1;
    const unsigned order_no_water = order_all & ~render_order_bit( RenderOrder_Water );
    const TranslucentWaterOrder water_order = translucent_water_order( );
    const float split_y = ( WATER_HEIGHT - 1.0f ) - renderOriginF.y;
    const bool camera_above = camera_pos_rebased.y >= split_y;
    this->chunkLoader.shader.set_uniform1f( "u_SplitY", split_y );
    this->chunkLoader.shader.set_uniform1f( "u_SplitSign", 0.0f );
    this->chunkLoader.draw( mvp, this->renderer, blocksTexture,
                            order_no_water & ~( water_order != TranslucentWaterOrder::Before ? render_order_bit( RenderOrder_Translucent ) : 0u ),
                            false, useFrameBuffer, renderOriginF, camera_pos_rebased ); // Blocks

    if ( water_order == TranslucentWaterOrder::Split ) {
        // Far side of the plane first: below the surface seen from above,
        // above it seen from below. u_SplitSign keeps only those fragments.
        this->chunkLoader.shader.set_uniform1f( "u_SplitSign", camera_above ? -1.0f : 1.0f );
        this->chunkLoader.draw( mvp, this->renderer, blocksTexture, render_order_bit( RenderOrder_Translucent ), false, useFrameBuffer, renderOriginF, camera_pos_rebased ); // Translucent, far side
        this->chunkLoader.shader.set_uniform1f( "u_SplitSign", 0.0f );
    }

    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", 0 );
    if ( draw_mouse_selection ) {
        this->mouseSelection.draw( this->renderer, mvp, renderOriginF );
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
    this->chunkLoader.shader.set_uniform1f( "u_Time", time_s );
    this->chunkLoader.shader.set_uniform1i( "u_FluidAnim", allowBlur ? 1 : 0 );
    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
    this->chunkLoader.draw( mvp, this->renderer, blocksTexture, render_order_bit( RenderOrder_Water ), false, useFrameBuffer, renderOriginF, camera_pos_rebased ); // Stencil water
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
        this->chunkLoader.draw( mvp, this->renderer, blocksTexture, render_order_bit( RenderOrder_Translucent ), false, useFrameBuffer, renderOriginF, camera_pos_rebased ); // Translucent after water
        this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", 0 );
        this->chunkLoader.shader.set_uniform1f( "u_SplitSign", 0.0f );
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
        float offset = 1.0 - WATER_HEIGHT;

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

        this->chunkLoader.shader.set_uniform1i( "u_DrawToReflection", true );
        this->chunkLoader.shader.set_uniform1f( "u_ExtraAlpha", 1.0f ); // this make reflections not solid...
        this->chunkLoader.shader.set_uniform1f( "u_ReflectionHeight", offset + renderOriginF.y );
        this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
        this->chunkLoader.calculate_cull( mvp_reflect, true, renderOrigin );
        this->chunkLoader.draw( mvp_reflect, this->renderer, blocksTexture, order_no_water, true, useFrameBuffer, renderOriginF, camera_pos_rebased ); // Reflected blocks

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
            this->fullScreenQuad.set_water_frame_uniforms( this->depthTexture, inv_mvp, camera_pos_rebased, renderOriginF );
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
                this->fullScreenQuad.draw_texture_fog( this->renderer, this->blockTexture, this->depthStencilTexture, this->fogTexture, this->skyColorTexture, 1.0, true, headInWater, 0, time_s, allowBlur );
            } else {
                this->fullScreenQuad.draw_texture( this->renderer, this->blockTexture, this->depthStencilTexture, 1.0, true, headInWater );
            }
            // Water FX overlay: fresnel-weighted reflection (planar for
            // sea-level fluids, screen-space marched for elevated ones),
            // blended over the scene just drawn.
            this->fullScreenQuad.draw_water( this->renderer, this->reflectionTexture, this->depthStencilTexture, this->fogTexture, this->skyColorTexture, this->blockTexture, this->depthTexture,
                                             y_height < 0 ? 0.4f : 0.6f, time_s, mvp, inv_mvp, renderOriginF, camera_pos_rebased, allowBlur, fog_blend_color );
            glStencilFunc( GL_NOTEQUAL, 1, 0xff ); // If the stencil value isn't 1 allow drawing.
            if ( useFogBlend ) {
                this->fullScreenQuad.draw_texture_fog( this->renderer, this->blockTexture, this->depthStencilTexture, this->fogTexture, this->skyColorTexture, 1.0, false, headInWater );
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
                                                   block_water_tint_type == TINT_UNDER_WATER_OBJECT_ALWAYS, fog_blend_color );
        }

        // Underwater keeps the simple uniform-strength reflection overlay;
        // above water, draw_water already composited the reflections.
        if ( usingReflections && headInWater ) {
            // X_HIGH wobbles the mirrored fetch, matching the land-side
            // rippled reflections; HIGH keeps it still. Kept faint so the
            // surface doesn't mirror away the world above.
            this->fullScreenQuad.draw_texture( this->renderer, this->reflectionTexture, this->depthStencilTexture, y_height < 0 ? 0.05 : 0.1, allowBlur, headInWater, 1, false, time_s, allowBlur );
        }
        glEnable( GL_DEPTH_TEST );
    }
    // chunk_loader_draw_chunks( &this->loadedChunks, mvp, &this->renderer, true, false ); // Water
    glDisable( GL_STENCIL_TEST );
}
void World::cleanup( MapStorage &map_storage ) {

    // Persist every owned chunk's mobs and dropped items before the loader
    // tears down.
    this->mobs.persist_all( );
    this->items.persist_all( );
    this->chunkLoader.cleanup( map_storage );
    this->skyBox.destroy( );
    this->multiplayer_avatars.cleanup( );
    this->blockDebris.cleanup( );
    this->bodies.cleanup( );
    this->items.cleanup( );
    this->mobs.cleanup( );
    this->heartParticles.cleanup( );
    this->mouseSelection.destroy( );
    this->debris_shader.destroy( );
    this->heart_shader.destroy( );
    this->primed_tnt_shader.destroy( );
    this->block_object_shader.destroy( );
    this->mob_shader.destroy( );
    this->mobsTexture.destroy( );

    this->vbl_block.destroy( );
    this->vbl_coords.destroy( );
    this->vbl_debris_instance.destroy( );
    this->vbl_mob_instance.destroy( );
    if constexpr ( SUPPORTS_FRAME_BUFFER ) {
        this->frameBuffer.destroy( );
        this->blockTexture.destroy( );
        this->reflectionTexture.destroy( );
        this->fogTexture.destroy( );
        this->skyColorTexture.destroy( );
        this->depthStencilTexture.destroy( );
        this->depthTexture.destroy( );
        this->depthFrameBuffer.destroy( );
        this->fullScreenQuad.destroy( );
    }
}

void World::set_chunk_debug_flags( const int flags ) {
    this->chunkLoader.shader.set_uniform1i( "u_DebugFlags", flags );
    // Vertex-side bits (e.g. 64 = face grow off) are consumed by the
    // ChunkLoader draw loop rather than the fragment shader.
    this->chunkLoader.debug_flags = flags;
}

#if ( SUPPORTS_FRAME_BUFFER )
void World::screenshot( const std::string &prefix ) const {
    take_screenshot( this->frameBuffer.id( ), prefix );
}
#else
void World::screenshot( const std::string &prefix ) const {
#if defined( REPGAME_WASM )
    // No offscreen FBO: read the default framebuffer (see screenshot.cpp).
    take_screenshot( 0, prefix );
#endif
}
#endif

int World::can_fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset ) const {
    const glm::vec3 chunk_with_offset = chunk.chunk_pos + offset;
    if ( const Chunk *fixupChunk_prt = this->chunkLoader.get_chunk( chunk_with_offset ) ) {
        const Chunk &fixupChunk = *fixupChunk_prt;
        if ( !fixupChunk.load_state ) {
            return 1;
        }
    }
    return 0;
}

void World::fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset, const glm::ivec3 &pos, const BlockState &blockState ) {
    // pr_debug( "Fixup Offset: %d %d %d", x, y, z );
    const glm::vec3 chunk_with_offset = chunk.chunk_pos + offset;
    if ( Chunk *fixupChunk_prt = this->chunkLoader.get_chunk( chunk_with_offset ) ) {
        Chunk &fixupChunk = *fixupChunk_prt;
        if ( fixupChunk.load_state ) {
            // This shouldn't happen because can_fixup_chunk should be called first.
            pr_debug( "Ekk, still loading. You'll probably get a lighting bug." );
            return;
        }
        fixupChunk.set_block( pos, blockState );
        fixupChunk.dirty = 1;
        fixupChunk.needs_repopulation = 1;
        this->chunkLoader.repop_must_enqueue( fixupChunk );
        if ( fixupChunk.is_empty_chunk && fixupChunk.cells ) {
            // The write materialized content into an all-air chunk — same
            // transition as the primary edit path (world.cpp's set_block
            // site): clear the empty flag and flag it drawable so the remesh
            // actually happens.
            fixupChunk.is_empty_chunk = false;
            fixupChunk.set_should_render( 1 );
        }
    }
}

Chunk *World::get_loaded_chunk( const glm::ivec3 &block_pos ) const {
    constexpr glm::vec3 chunk_size = glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / chunk_size );
    return this->chunkLoader.get_chunk( chunk_pos );
}

BlockState World::get_block_from_chunk( const Chunk &chunk, const glm::ivec3 &block_pos ) {
    constexpr glm::ivec3 chunk_size = glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    const glm::ivec3 diff = block_pos - ( chunk.chunk_pos * chunk_size );
    return chunk.get_block( diff );
}

BlockState World::get_loaded_block( const glm::ivec3 &block_pos ) const {
    if ( const Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        const Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return BLOCK_STATE_LAST_BLOCK_ID;
        }
        const BlockState blockState = get_block_from_chunk( chunk, block_pos );
        return blockState;
    }
    return BLOCK_STATE_LAST_BLOCK_ID;
}

const ChestContents *World::get_chest_contents( const glm::ivec3 &block_pos ) const {
    if ( const Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        const Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const auto it = chunk.containers.chests.find( Chunk::get_index_from_coords( diff ) );
        return it == chunk.containers.chests.end( ) ? nullptr : &it->second;
    }
    return nullptr;
}

ChestContents *World::get_or_create_chest_contents( const glm::ivec3 &block_pos ) {
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        if ( !block_is_chest( get_block_from_chunk( chunk, block_pos ).id ) ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const int index = Chunk::get_index_from_coords( diff );
        chunk.containers.chests_loaded = 1;
        return &chunk.containers.chests[ index ]; // default-constructs an empty chest
    }
    return nullptr;
}

bool World::set_chest_slot( const glm::ivec3 &block_pos, int slot, const InventorySlot &value ) {
    if ( slot < 0 || slot >= CHEST_SLOTS ) {
        return false;
    }
    ChestContents *contents = this->get_or_create_chest_contents( block_pos );
    if ( contents == nullptr ) {
        return false;
    }
    contents->slots[ slot ] = value;
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        chunk_prt->dirty = 1;
    }
    return true;
}

void World::set_chest_contents( const glm::ivec3 &block_pos, const ChestContents &contents ) {
    ChestContents *dst = this->get_or_create_chest_contents( block_pos );
    if ( dst == nullptr ) {
        return;
    }
    *dst = contents;
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        chunk_prt->dirty = 1;
    }
}

const FurnaceContents *World::get_furnace_contents( const glm::ivec3 &block_pos ) const {
    if ( const Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        const Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const auto it = chunk.containers.furnaces.find( Chunk::get_index_from_coords( diff ) );
        return it == chunk.containers.furnaces.end( ) ? nullptr : &it->second;
    }
    return nullptr;
}

FurnaceContents *World::get_or_create_furnace_contents( const glm::ivec3 &block_pos ) {
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        if ( !block_is_furnace( get_block_from_chunk( chunk, block_pos ).id ) ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const int index = Chunk::get_index_from_coords( diff );
        chunk.containers.furnaces_loaded = 1;
        return &chunk.containers.furnaces[ index ]; // default-constructs an empty furnace
    }
    return nullptr;
}

bool World::set_furnace_slot( const glm::ivec3 &block_pos, int slot, const InventorySlot &value ) {
    if ( slot < 0 || slot >= FURNACE_INVENTORY_SLOTS ) {
        return false;
    }
    FurnaceContents *contents = this->get_or_create_furnace_contents( block_pos );
    if ( contents == nullptr ) {
        return false;
    }
    contents->slots[ slot ] = value;
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        chunk_prt->dirty = 1;
    }
    return true;
}

void World::set_furnace_contents( const glm::ivec3 &block_pos, const FurnaceContents &contents ) {
    FurnaceContents *dst = this->get_or_create_furnace_contents( block_pos );
    if ( dst == nullptr ) {
        return;
    }
    *dst = contents;
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        chunk_prt->dirty = 1;
    }
}

// --- Kind-keyed container API (CONTAINER_* wire family) --------------------
// One switch per call site: each arm routes to the kind's typed accessor and
// converts the blob via container_to/from_blob. New kinds add an arm here.

bool World::get_container_contents( NetContainerKind kind, const glm::ivec3 &block_pos, ContainerContents &out ) const {
    if ( net_container_kind_def( static_cast<uint8_t>( kind ) ) == nullptr ) {
        return false;
    }
    switch ( kind ) {
        case NetContainerKind::CHEST: {
            const ChestContents *c = this->get_chest_contents( block_pos );
            if ( c == nullptr ) {
                return false;
            }
            out = container_to_blob( *c );
            return true;
        }
        case NetContainerKind::FURNACE: {
            const FurnaceContents *f = this->get_furnace_contents( block_pos );
            if ( f == nullptr ) {
                return false;
            }
            out = container_to_blob( *f );
            return true;
        }
    }
    return false;
}

void World::set_container_contents( NetContainerKind kind, const glm::ivec3 &block_pos, const ContainerContents &contents ) {
    if ( net_container_kind_def( static_cast<uint8_t>( kind ) ) == nullptr ) {
        return;
    }
    switch ( kind ) {
        case NetContainerKind::CHEST: {
            ChestContents c;
            container_from_blob( contents, c );
            this->set_chest_contents( block_pos, c );
            return;
        }
        case NetContainerKind::FURNACE: {
            FurnaceContents f;
            container_from_blob( contents, f );
            this->set_furnace_contents( block_pos, f );
            return;
        }
    }
}

bool World::set_container_slot( NetContainerKind kind, const glm::ivec3 &block_pos, int slot, const InventorySlot &value ) {
    if ( net_container_kind_def( static_cast<uint8_t>( kind ) ) == nullptr ) {
        return false;
    }
    switch ( kind ) {
        case NetContainerKind::CHEST:
            return this->set_chest_slot( block_pos, slot, value );
        case NetContainerKind::FURNACE:
            return this->set_furnace_slot( block_pos, slot, value );
    }
    return false;
}

void World::tick_furnaces( ) {
    // Scan every chunk slot: furnaces are rare, so the per-slot check is a
    // map-empty test on chunks that have any furnace data at all.
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkLoader.chunkArray[ i ];
        if ( chunk.load_state || chunk.containers.furnaces.empty( ) ) {
            continue;
        }
        const glm::ivec3 chunk_origin = chunk.chunk_pos * CHUNK_SIZE_I;
        for ( auto &pair : chunk.containers.furnaces ) {
            const int index = pair.first;
            const glm::ivec3 internal_pos( ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1,
                                           ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1,
                                           ( index % CHUNK_SIZE_INTERNAL_Z ) - 1 );
            const glm::ivec3 block_pos = chunk_origin + internal_pos;
            // The block may have been replaced without going through
            // Chunk::set_block (e.g. an unloaded-chunk diff): drop stale
            // contents rather than ticking a phantom furnace.
            if ( !block_is_furnace( chunk.state_at( index ).id ) ) {
                pair.second.clear( );
                continue;
            }
            bool changed = false;
            const bool lit_changed = furnace_tick( pair.second, &changed );
            if ( changed ) {
                chunk.dirty = 1;
            }
            if ( lit_changed ) {
                BlockState state = chunk.state_at( index );
                state.id = state.id == FURNACE ? LIT_FURNACE : FURNACE;
                // display_id drives the mesh/light lookup — keep it canonical.
                state.display_id = state.id;
                this->set_loaded_block( block_pos, state );
            }
        }
    }
}

void World::spawn_block_debris( const glm::ivec3 &block_pos, const BlockState &prev_state, const BlockID new_id, const int num_particles ) {
    if ( prev_state.id == AIR || prev_state.id == LAST_BLOCK_ID || prev_state.id == new_id ) {
        return;
    }
    const Block *prev_block = block_definition_get_definition( prev_state.id );
    if ( prev_block->renderOrder == RenderOrder_Water ) {
        return;
    }
    // Particles live <1s and stay near their cell, so a single light sample at
    // the (now-air) block center is baked into every instance.
    this->blockDebris.spawn_block_break( block_pos, prev_state,
                                         this->chunkLoader.light_sample( glm::vec3( block_pos ) + 0.5f ), num_particles );
}


// void World::overlay_blocks( const glm::ivec3 *block_poses, BlockState *blockStates, int numBlocks ) {
//     const glm::ivec3 CHUNK_SIZE = glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
//     for ( int bi = 0; bi < numBlocks; bi++ ) {
//         BlockState &blockState = blockStates[ bi ];
//         const glm::ivec3 &block_pos = block_poses[ bi ];
//         glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / glm::vec3( CHUNK_SIZE ) );
//         Chunk *chunk_prt = this->chunkLoader.get_chunk( chunk_pos );
//         if ( chunk_prt ) {
//             Chunk &chunk = *chunk_prt;
//             glm::ivec3 diff = block_pos - chunk_pos * CHUNK_SIZE;
//             // pr_debug( "Orig Offset: %d %d %d", diff_x, diff_y, diff_z );

//             for ( int i = -1; i < 2; i++ ) {
//                 const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
//                 if ( !needs_update_x ) {
//                     continue;
//                 }
//                 for ( int j = -1; j < 2; j++ ) {
//                     const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
//                     if ( !needs_update_y ) {
//                         continue;
//                     }
//                     for ( int k = -1; k < 2; k++ ) {
//                         const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
//                         if ( !needs_update_z ) {
//                             continue;
//                         }
//                         // pr_debug( "Chunk Dir: %d %d %d:%d", i, j, k, needs_update );
//                         // pr_debug( "                    Needs Updates: %d %d %d:%d", needs_update_x, needs_update_y, needs_update_z, needs_update );
//                         // pr_debug( "                                New Offset: %d %d %d:%d", new_i, new_j, new_k, needs_update );

//                         glm::ivec3 new_pos = glm::ivec3( i * needs_update_x, j * needs_update_y, k * needs_update_z );
//                         glm::ivec3 offset = glm::ivec3( i, j, k );
//                         glm::ivec3 pos = diff - CHUNK_SIZE * new_pos;
//                         fixup_chunk( chunk, offset, pos, blockState );
//                     }
//                 }
//             }
//             chunk.set_block( diff, blockState );
//             chunk.dirty = 1;
//             chunk.needs_repopulation = 1;

//         } else {
//             // This just means mouse is not pointing at a block
//             // pr_debug( "Could not find the pointed to chunk" );
//         }
//     }
// }
void World::set_loaded_block( const glm::ivec3 &block_pos, BlockState blockState ) {
    glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );

    Chunk *chunk_prt = this->chunkLoader.get_chunk( chunk_pos );
    if ( chunk_prt == nullptr || chunk_prt->load_state ) {
        // Parked/loading slot — there may be no block storage to write
        // (parking releases it). Multiplayer edits re-arrive as diffs on the
        // next load ONLY for other clients: the sender's own BLOCK_UPDATE is
        // never relayed back, so queue the write the same way a received
        // update would be — it lands at the next load's finalize.
        if ( this->multiplayer != nullptr ) {
            NetChunkDiffResultPayload diff;
            diff.chunk_x = chunk_pos.x;
            diff.chunk_y = chunk_pos.y;
            diff.chunk_z = chunk_pos.z;
            NetChunkDiffEntry entry;
            entry.blocks_index = static_cast<uint32_t>( Chunk::get_index_from_coords( block_pos - chunk_pos * CHUNK_SIZE_I ) );
            entry.blockState = blockState;
            diff.diffs.push_back( entry );
            diff.num_diffs = 1;
            this->multiplayer->queue_pending_diff( diff );
        }
        return;
    }
    {
        Chunk &chunk = *chunk_prt;
        glm::ivec3 diff = block_pos - chunk_pos * CHUNK_SIZE_I;
        // pr_debug( "Orig Offset: %d %d %d", diff_x, diff_y, diff_z );

        for ( int i = -1; i < 2; i++ ) {
            const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
            if ( !needs_update_x ) {
                continue;
            }
            for ( int j = -1; j < 2; j++ ) {
                const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
                if ( !needs_update_y ) {
                    continue;
                }
                for ( int k = -1; k < 2; k++ ) {
                    const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
                    if ( !needs_update_z ) {
                        continue;
                    }
                    glm::ivec3 offset = glm::ivec3( i, j, k );
                    if ( !can_fixup_chunk( chunk, offset ) ) {
                        pr_debug( "Ekk, can't fixup block, so not placing" );
                        return;
                    }
                }
            }
        }
        for ( int i = -1; i < 2; i++ ) {
            const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
            if ( !needs_update_x ) {
                continue;
            }
            for ( int j = -1; j < 2; j++ ) {
                const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
                if ( !needs_update_y ) {
                    continue;
                }
                for ( int k = -1; k < 2; k++ ) {
                    const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
                    if ( !needs_update_z ) {
                        continue;
                    }
                    if ( i == 0 && j == 0 && k == 0 ) {
                        // The (0,0,0) offset is this same cell — the
                        // chunk.set_block below writes it. Writing it here too
                        // would apply set_block's waterlogging normalization
                        // twice (e.g. breaking a waterlogged plant normalizes
                        // AIR->WATER, then a second write into that WATER
                        // would store raw AIR or restore the plant).
                        continue;
                    }

                    // pr_debug( "Chunk Dir: %d %d %d:%d", i, j, k, needs_update );
                    // pr_debug( "                    Needs Updates: %d %d %d:%d", needs_update_x, needs_update_y, needs_update_z, needs_update );
                    // pr_debug( "                                New Offset: %d %d %d:%d", new_i, new_j, new_k, needs_update );

                    glm::ivec3 new_pos = glm::ivec3( i * needs_update_x, j * needs_update_y, k * needs_update_z );
                    glm::ivec3 offset = glm::ivec3( i, j, k );
                    glm::ivec3 pos = diff - CHUNK_SIZE_I * new_pos;
                    fixup_chunk( chunk, offset, pos, blockState );
                }
            }
        }
        chunk.set_block( diff, blockState );
        chunk.dirty = 1;
        chunk.sidecars.player_edited = 1;
        chunk.needs_repopulation = 1;
        this->chunkLoader.repop_must_enqueue( chunk );
        chunk.is_empty_chunk = false;
        // An empty chunk has should_render == 0 (program_terrain only sets it
        // when a layer has instances), so it's excluded from the drawable list
        // the remeshing loop walks. Set it here so the chunk enters the list
        // and gets remeshed this frame. Safe: Chunk::draw also guards on
        // num_instances != 0, so no stale geometry draws before remeshing.
        chunk.set_should_render( 1 );

        // Flood-fill light: queue a recheck for this cell on the lighting
        // thread (inline when it isn't running). No-op while the chunk has
        // no light volume.
        this->chunkLoader.light_submit_recheck( block_pos );
    }
}

// Grass lifecycle, driven by the source cell (MC GrassBlock.randomTick): a
// grass-family cell covered by an opaque block dies back to dirt; an
// uncovered one may spread to random dirt targets in reach — ±1
// horizontally, 3 down / 1 up — provided the cell above the target is also
// open. Snowy grass spreads plain grass (the snow doesn't propagate);
// mycelium spreads mycelium.
#define GRASS_SPREAD_ATTEMPTS 4
// Fraction of grass random ticks that try to spread — the same roll the
// sapling/crop check uses (~3.5 min mean per grass cell).
#define GRASS_SPREAD_CHANCE_DEN 3
// MC's spread reach: ±1 horizontally, 3 down / 1 up.
#define GRASS_SPREAD_SIDE 1
#define GRASS_SPREAD_DOWN 3
#define GRASS_SPREAD_UP 1

// True when the cell above pos is loaded and light-opaque (casts_shadow —
// the same predicate the light engine's opacity test uses). Cover kills
// grass and disqualifies a spread target; torches, leaves and partial
// blocks shade but don't cover. An unloaded cell counts as open (the same
// permissive rule leaf decay uses).
bool World::grass_covered( const glm::ivec3 &pos ) const {
    const BlockState above = this->get_loaded_block( pos + glm::ivec3( 0, 1, 0 ) );
    return above.id != LAST_BLOCK_ID && block_definition_get_definition( above.id )->casts_shadow;
}

void World::grass_random_tick( RepGameState &state, const glm::ivec3 &world_pos, const BlockState &cell ) {
    if ( grass_covered( world_pos ) ) {
        // Covered grass dies to dirt — same natural write + sync as crops.
        BlockState next = cell;
        next.id = DIRT;
        next.display_id = DIRT;
        next.rotation = BLOCK_ROTATE_0;
        this->set_loaded_block_natural( world_pos, next );
        state.multiplayer.set_block( world_pos, next );
        return;
    }
    this->random_tick_state ^= this->random_tick_state << 13;
    this->random_tick_state ^= this->random_tick_state >> 17;
    this->random_tick_state ^= this->random_tick_state << 5;
    if ( this->random_tick_state % GRASS_SPREAD_CHANCE_DEN != 0 ) {
        return;
    }
    const BlockID spread_id = cell.id == SNOWY_GRASS ? GRASS : cell.id;
    for ( int attempt = 0; attempt < GRASS_SPREAD_ATTEMPTS; attempt++ ) {
        this->random_tick_state ^= this->random_tick_state << 13;
        this->random_tick_state ^= this->random_tick_state >> 17;
        this->random_tick_state ^= this->random_tick_state << 5;
        const glm::ivec3 target = world_pos + glm::ivec3( static_cast<int>( this->random_tick_state % ( 2 * GRASS_SPREAD_SIDE + 1 ) ) - GRASS_SPREAD_SIDE,
                                                         static_cast<int>( ( this->random_tick_state >> 8 ) % ( GRASS_SPREAD_DOWN + GRASS_SPREAD_UP + 1 ) ) - GRASS_SPREAD_DOWN,
                                                         static_cast<int>( ( this->random_tick_state >> 16 ) % ( 2 * GRASS_SPREAD_SIDE + 1 ) ) - GRASS_SPREAD_SIDE );
        // get_loaded_block rather than chunk.get_block: dy -3..+1 can reach
        // past the chunk's ±1 halo; LAST_BLOCK_ID targets are skipped.
        const BlockState target_state = this->get_loaded_block( target );
        if ( target_state.id != DIRT || grass_covered( target ) ) {
            continue;
        }
        BlockState next = target_state;
        next.id = spread_id;
        next.display_id = spread_id;
        next.rotation = BLOCK_ROTATE_0;
        this->set_loaded_block_natural( target, next );
        state.multiplayer.set_block( target, next );
    }
}

void World::set_loaded_block_natural( const glm::ivec3 &block_pos, BlockState blockState ) {
    Chunk *chunk = this->get_loaded_chunk( block_pos );
    const uint8_t was_edited = chunk != nullptr ? chunk->sidecars.player_edited : 0;
    this->set_loaded_block( block_pos, blockState );
    if ( chunk != nullptr ) {
        chunk->sidecars.player_edited = was_edited;
    }
}

// Chunks within this many of the player get random ticks.
#define RANDOM_TICK_RADIUS_XZ 2
#define RANDOM_TICK_RADIUS_Y 1
// Cells sampled per live chunk per game tick: ~1 tick per cell every
// (62*62*62 / 24) / 144 s ≈ 69 s.
#define RANDOM_TICKS_PER_CHUNK 24
// Fraction of sapling random ticks that grow: ~3.5 min mean under 9+ light.
#define SAPLING_GROW_CHANCE_NUM 1
#define SAPLING_GROW_CHANCE_DEN 3
// Fraction of crop random ticks that advance a stage: same cadence as the
// sapling check (each sampled cell ticks once per ~69 s, so a wheat field
// matures in ~28 min — near MC's unirrigated rate).
#define CROP_GROW_CHANCE_DEN 3
// Minimum sky or block light level (of 15) for a sapling or crop to grow.
#define SAPLING_GROW_MIN_LIGHT ( 9.0f / 15.0f )

void World::process_random_ticks( RepGameState &state ) {
    const glm::ivec3 center_chunk( static_cast<int>( floor( state.camera.pos.x / CHUNK_SIZE_X ) ),
                                   static_cast<int>( floor( state.camera.pos.y / CHUNK_SIZE_Y ) ),
                                   static_cast<int>( floor( state.camera.pos.z / CHUNK_SIZE_Z ) ) );
    const glm::ivec3 chunk_size( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkLoader.chunkArray[ i ];
        if ( chunk.load_state != CHUNK_LIVE ) {
            continue;
        }
        const glm::ivec3 d = chunk.chunk_pos - center_chunk;
        if ( abs( d.x ) > RANDOM_TICK_RADIUS_XZ || abs( d.z ) > RANDOM_TICK_RADIUS_XZ || abs( d.y ) > RANDOM_TICK_RADIUS_Y ) {
            continue;
        }
        const glm::ivec3 chunk_origin = chunk.chunk_pos * chunk_size;
        for ( int s = 0; s < RANDOM_TICKS_PER_CHUNK; s++ ) {
            this->random_tick_state ^= this->random_tick_state << 13;
            this->random_tick_state ^= this->random_tick_state >> 17;
            this->random_tick_state ^= this->random_tick_state << 5;
            const glm::ivec3 local( this->random_tick_state % CHUNK_SIZE_X,
                                    ( this->random_tick_state >> 8 ) % CHUNK_SIZE_Y,
                                    ( this->random_tick_state >> 16 ) % CHUNK_SIZE_Z );
            const BlockState cell = chunk.get_block( local );
            TreeSpec spec;
            const glm::ivec3 world_pos = chunk_origin + local;
            // Flowing fluid outlives its update events when a chunk unloads
            // (the queue isn't persisted): a random tick re-arms it. Sources
            // are stable so only flow levels need the rescue; the scheduler
            // dedupes, making this cheap.
            if ( fluid_level( cell ) > 0 ) {
                fluid_schedule_update( state, world_pos );
            }
            const Block *cell_def = cell.id == LAST_BLOCK_ID ? nullptr : block_definition_get_definition( cell.id );
            const bool is_crop_stage = cell_def != nullptr && cell_def->crop_next != LAST_BLOCK_ID;
            if ( !is_crop_stage && !tree_spec_for_sapling( cell.id, world_pos, &spec ) ) {
                // Grass-family cells tick from the source: covered grass
                // dies to dirt, open grass may spread to nearby dirt.
                if ( cell.id == GRASS || cell.id == SNOWY_GRASS || cell.id == MYCELIUM ) {
                    this->grass_random_tick( state, world_pos, cell );
                }
                continue;
            }
            const glm::vec2 light = this->chunkLoader.light_sample( glm::vec3( world_pos ) + 0.5f );
            if ( light.x < SAPLING_GROW_MIN_LIGHT && light.y < SAPLING_GROW_MIN_LIGHT ) {
                continue;
            }
            this->random_tick_state ^= this->random_tick_state << 13;
            this->random_tick_state ^= this->random_tick_state >> 17;
            this->random_tick_state ^= this->random_tick_state << 5;
            if ( is_crop_stage ) {
                // Crop growth advances the stage chain in place — a plain
                // write (no debris, no support change), synced like the tree
                // grow below. Not a player edit: the pen rule stays out.
                if ( this->random_tick_state % CROP_GROW_CHANCE_DEN != 0 ) {
                    continue;
                }
                BlockState next = cell;
                next.id = cell_def->crop_next;
                next.display_id = cell_def->crop_next;
                next.rotation = BLOCK_ROTATE_0;
                this->set_loaded_block_natural( world_pos, next );
                state.multiplayer.set_block( world_pos, next );
                continue;
            }
            if ( this->random_tick_state % SAPLING_GROW_CHANCE_DEN >= SAPLING_GROW_CHANCE_NUM ) {
                continue;
            }
            tree_grow_try( state, world_pos, true );
        }
    }
}
