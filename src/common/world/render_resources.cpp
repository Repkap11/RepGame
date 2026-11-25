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


MK_SHADER( object_vertex );
MK_SHADER( object_fragment );
MK_SHADER( debris_vertex );
MK_SHADER( heart_vertex );
MK_SHADER( primed_tnt_vertex );
MK_SHADER( block_object_vertex );
MK_SHADER( mob_vertex );
MK_SHADER( item_sprite_vertex );

MK_TEXTURE( mobs, 128, 64 * MOB_SKIN_COUNT, 128, 64 );

void World::init( const glm::dvec3 &camera_pos, int width, int height, MapStorage &map_storage, int render_radius ) {
    this->map_storage = &map_storage;
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
    this->bodies.init( this->vbl_object_vertex, this->vbl_primed_tnt_instance );
    this->body_batch.init( this->chunkLoader.solid.vb_block, this->chunkLoader.water.vb_block, this->vbl_block, this->vbl_coords );
    this->chunkLoader.body_batch = &this->body_batch;
    this->items.init( this->vbl_object_vertex, this->vbl_block_object_instance, this->vbl_item_instance );
    // Arrows share the extruded item-sprite mesh + shader.
    this->arrows.init( this->vbl_object_vertex, this->vbl_item_instance );
    // The sprite program alpha-tests the atlas at exact texels (no mip bleed).
    this->item_sprite_shader.set_uniform1i( "u_ExactTexel", 1 );
    this->mobs.init( this->vbl_object_vertex, this->vbl_mob_instance, this->vbl_block_object_instance, this->vbl_item_instance );
    this->mobs.init_storage( &map_storage );
    this->items.init_storage( &map_storage );
    // Vehicles share the mob model pipeline (part instances + skin).
    this->vehicles.init( this->vbl_object_vertex, this->vbl_mob_instance );
    this->vehicles.init_storage( &map_storage );
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
            repgame_fatal( );
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
            repgame_fatal( );
        }
    }
}

void World::render( Multiplayer &multiplayer, const glm::dvec3 &camera_pos, const int limit_render, const glm::mat4 &rotation ) {
    // In multiplayer the item files live on the server: local reads/writes
    // stay off once the connect resolves (or latches after a drop).
    this->items.set_multiplayer_mode( multiplayer.multiplayer_mode( ), &multiplayer.entities );
    // Same for mobs: local mobs_* files and the natural-spawn roll route
    // through CHUNK_ROLE/populated instead.
    this->mobs.set_multiplayer_mode( multiplayer.multiplayer_mode( ), &multiplayer.entities );
    // Bodies too: causer-simulated spawn announcements and mirrors.
    this->bodies.set_multiplayer_mode( multiplayer.multiplayer_mode( ), &multiplayer.entities );
    // Arrows follow the same causer-simulated model.
    this->arrows.set_multiplayer_mode( multiplayer.multiplayer_mode( ), &multiplayer.entities );
    // Vehicles: owner-simulated like items; local carts_* IO stays off.
    this->vehicles.set_multiplayer_mode( multiplayer.multiplayer_mode( ), &multiplayer.entities );
    // Chunk + container files too: in multiplayer all of that state lives
    // on the server, so local chunk_*/chest_*/furnace_* IO stays off.
    if ( this->map_storage != nullptr ) {
        this->map_storage->set_chunk_io_enabled( !multiplayer.multiplayer_mode( ) );
    }
    this->chunkLoader.render_chunks( multiplayer, camera_pos, limit_render );
    // Hand each just-finalized chunk's pending mobs to the manager (loaded
    // records or the natural-spawn roll); skipped chunks stay owned next pass.
    if ( !this->chunkLoader.just_finalized.empty( ) ) {
        for ( Chunk *chunk : this->chunkLoader.just_finalized ) {
            this->mobs.adopt_chunk( *this, *chunk );
            this->items.adopt_chunk( *chunk );
            this->vehicles.adopt_chunk( *chunk );
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

void World::init_sign_text( const FontRenderer &font ) {
    this->worldText.init( font );
}

void World::cleanup( MapStorage &map_storage ) {

    // Persist every owned chunk's mobs and dropped items before the loader
    // tears down.
    this->mobs.persist_all( );
    this->items.persist_all( );
    this->vehicles.persist_all( );
    this->chunkLoader.cleanup( map_storage );
    this->skyBox.destroy( );
    this->multiplayer_avatars.cleanup( );
    this->blockDebris.cleanup( );
    this->bodies.cleanup( );
    this->body_batch.destroy( );
    this->items.cleanup( );
    this->arrows.cleanup( );
    this->vehicles.cleanup( );
    this->mobs.cleanup( );
    this->heartParticles.cleanup( );
    this->mouseSelection.destroy( );
    this->worldText.cleanup( );
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


bool World::avatar_position_of( const int player_id, glm::dvec3 &feet ) const {
    return this->multiplayer_avatars.position_of( player_id, feet );
}

uint64_t World::avatar_riding_vehicle_of_uid( const uint32_t uid ) const {
    return this->multiplayer_avatars.riding_vehicle_of_uid( uid );
}
