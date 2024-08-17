#include <math.h>

#include "common/world.hpp"
#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"

#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

MK_SHADER( object_vertex );
MK_SHADER( object_fragment );

bool hasError( ) {
    int errCode;
    if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {
        return true;
    }
    return false;
}

void World::init( const glm::vec3 &camera_pos, int width, int height ) {
    // These are from CubeFace
    this->vbl_block.init( );
    this->vbl_block.push_float( 3 );        // Coords
    this->vbl_block.push_float( 2 );        // Texture coords
    this->vbl_block.push_unsigned_int( 1 ); // Face type (top, sides, bottom)
    this->vbl_block.push_unsigned_int( 1 ); // Corner_shift

    // These are from BlockCoords
    this->vbl_coords.init( );
    this->vbl_coords.push_float( 3 );        // block 3d world coords
    this->vbl_coords.push_unsigned_int( 1 ); // Multiples (mesh)
    this->vbl_coords.push_unsigned_int( 3 ); // which texture
    this->vbl_coords.push_unsigned_int( 3 ); // packed lighting
    this->vbl_coords.push_unsigned_int( 3 ); // packed lightingMOB_ROTATION
    this->vbl_coords.push_unsigned_int( 1 ); // face_shift for face rotation
    this->vbl_coords.push_float( 3 );        // block shape scale
    this->vbl_coords.push_float( 3 );        // block shape offset
    this->vbl_coords.push_float( 3 );        // texture shape offset

    // These are from ParticleVertex
    this->vbl_object_vertex.init( );
    this->vbl_object_vertex.push_float( 3 );        // Coords
    this->vbl_object_vertex.push_float( 2 );        // TxCoords
    this->vbl_object_vertex.push_unsigned_int( 1 ); // faceType

    // These are from ParticlePosition
    this->vbl_object_position.init( );
    this->vbl_object_position.push_unsigned_int( 3 ); // which texture
    this->vbl_object_position.push_float( 4 );        // transform
    this->vbl_object_position.push_float( 4 );        // transform
    this->vbl_object_position.push_float( 4 );        // transform
    this->vbl_object_position.push_float( 4 );        // transform

    this->chunkLoader.init( camera_pos, this->vbl_block, this->vbl_coords );

    float *random_rotation_blocks = block_definitions_get_random_rotations( );

    this->chunkLoader.shader.set_uniform1fv( "u_RandomRotationBlocks", random_rotation_blocks, MAX_ROTATABLE_BLOCK );

    this->object_shader.init( &object_vertex, &object_fragment );

    this->skyBox.init( this->vbl_object_vertex, this->vbl_object_position );
    this->multiplayer_avatars.init( this->vbl_object_vertex, this->vbl_object_position );
    this->mouseSelection.init( this->vbl_block, this->vbl_coords );

#if ( SUPPORTS_FRAME_BUFFER )
    this->frameBuffer.init( );
    this->frameBuffer.bind( );

    showErrors( );
    this->reflectionTexture.init_empty_color( 0 );
    this->blockTexture.init_empty_color( 0 );
    this->depthStencilTexture.init_empty_depth_stencil( 0 );
    showErrors( );

    this->blockTexture.change_size( width, height );
    showErrors( );
    this->reflectionTexture.change_size( width, height );
    showErrors( );
    this->depthStencilTexture.change_size( width, height );
    showErrors( );

    this->frameBuffer.attach_texture( this->blockTexture, 0 );
    this->frameBuffer.attach_texture( this->reflectionTexture, 1 );
    this->frameBuffer.attach_texture( this->depthStencilTexture, 0 ); // 0 is fake
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
    if ( SUPPORTS_FRAME_BUFFER ) {
        if ( !this->frameBuffer.ok( ) ) {
            pr_debug( "Frame buffer not ok" );
            showErrors( );
            exit( 1 );
        }
        this->blockTexture.change_size( width, height );
        showErrors( );
        this->reflectionTexture.change_size( width, height );
        showErrors( );
        this->depthStencilTexture.change_size( width, height );
        showErrors( );

        if ( !this->frameBuffer.ok( ) ) {
            pr_debug( "Frame buffer not ok" );
            showErrors( );
            exit( 1 );
        }
    }
}

void World::render( const glm::vec3 &camera_pos, int limit_render, const glm::mat4 &rotation ) {
    this->chunkLoader.render_chunks( camera_pos, limit_render );
}

void World::set_selected_block( const glm::ivec3 &selected, int shouldDraw ) {
    BlockState blockState = get_loaded_block( selected );
    if ( blockState.id == LAST_BLOCK_ID ) {
        return;
    }
    this->mouseSelection.set_block( selected, shouldDraw, blockState );
}

#define WATER_THRESHOLD_P 0.02
#define WATER_THRESHOLD_N -0.01
void World::draw( const Texture &blocksTexture, const glm::mat4 &mvp, const glm::mat4 &mvp_reflect, const glm::mat4 &mvp_sky, const glm::mat4 &mvp_sky_reflect, int debug, int draw_mouse_selection, float y_height, bool headInWater,
                  WorldDrawQuality worldDrawQuality ) {

    const bool useFrameBuffer = SUPPORTS_FRAME_BUFFER && ( worldDrawQuality >= WorldDrawQuality::MEDIUM );
    const bool usingReflections = useFrameBuffer && ( worldDrawQuality >= WorldDrawQuality::HIGH );
    const bool allowBlur = usingReflections && ( worldDrawQuality >= WorldDrawQuality::X_HIGH );
    int object_water_tint_type;
    int block_water_tint_type;
    double out_int_part;
    float water_diff = modf( y_height, &out_int_part ) - ( WATER_HEIGHT );
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

    if ( useFrameBuffer ) {
        this->frameBuffer.bind( );
        // glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
        // glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
        // glClearDepth
        showErrors( );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
        showErrors( );
        GLenum bufs[ 2 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        // glDrawBuffers( 1, bufs );
        glDrawBuffers( 2, bufs );
        showErrors( );
    } else {
        if ( SUPPORTS_FRAME_BUFFER ) {
            FrameBuffer::bind_display( );
        }
    }

    this->object_shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->object_shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->object_shader.set_uniform1i( "u_TintUnderWater", object_water_tint_type );
    this->object_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->object_shader.set_uniform_mat4f( "u_MVP", mvp );
    this->multiplayer_avatars.draw( this->renderer, this->object_shader ); // Mobs
    this->object_shader.set_uniform_mat4f( "u_MVP", mvp_sky );
    this->skyBox.draw( this->renderer, this->object_shader ); // Sky
    glEnable( GL_DEPTH_TEST );

    this->chunkLoader.calculate_cull( mvp, false );
    this->chunkLoader.shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
    this->chunkLoader.shader.set_uniform1i( "u_DrawToReflection", 0 );
    this->chunkLoader.shader.set_uniform1f( "u_ReflectionHeight", 0 );
    this->chunkLoader.shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->chunkLoader.shader.set_uniform3f( "u_DebugScaleOffset", debug_block_scale, debug_block_scale, debug_block_scale );
    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
    this->chunkLoader.shader.set_uniform1f( "u_ReflectionDotSign", y_height < 0 ? -1.0f : 1.0f );
    // shader_set_uniform1f( &this->loadedChunks.shader, "u_ReflectionDotSign", 1.0f );
    this->chunkLoader.draw( mvp, this->renderer, blocksTexture, false, false ); // Blocks

    this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", 0 );
    if ( draw_mouse_selection ) {
        this->mouseSelection.draw( this->renderer, this->chunkLoader.shader );
    }

    glEnable( GL_STENCIL_TEST );
    glStencilFunc( GL_ALWAYS, 1, 0xff );
    glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );

    this->chunkLoader.draw( mvp, this->renderer, blocksTexture, true, false ); // Stencil water
    glStencilFunc( GL_EQUAL, 1, 0xff );
    glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

    if ( usingReflections ) {

        glCullFace( GL_FRONT );
        glClear( GL_DEPTH_BUFFER_BIT );
        float offset = 1.0 - WATER_HEIGHT;
        this->chunkLoader.shader.set_uniform1i( "u_DrawToReflection", true );
        this->chunkLoader.shader.set_uniform1f( "u_ExtraAlpha", 1.0f ); // this make reflections not solid...
        this->chunkLoader.shader.set_uniform1f( "u_ReflectionHeight", offset );
        this->chunkLoader.shader.set_uniform1i( "u_TintUnderWater", block_water_tint_type );
        this->chunkLoader.calculate_cull( mvp_reflect, true );
        this->chunkLoader.draw( mvp_reflect, this->renderer, blocksTexture, false, true ); // Reflected blocks

        this->object_shader.set_uniform1i_texture( "u_Texture", blocksTexture );
        this->object_shader.set_uniform1f( "u_ReflectionHeight", offset );
        this->object_shader.set_uniform1f( "u_ExtraAlpha", 1.0f );
        this->object_shader.set_uniform1i( "u_DrawToReflection", 1 );
        this->object_shader.set_uniform_mat4f( "u_MVP", mvp_reflect );
        this->multiplayer_avatars.draw( this->renderer, this->object_shader ); // Reflected mobs

        this->object_shader.set_uniform_mat4f( "u_MVP", mvp_sky_reflect );
        this->skyBox.draw( this->renderer, this->object_shader ); // Reflected sky

        this->chunkLoader.shader.set_uniform1f( "u_ReflectionHeight", 0 );
        this->object_shader.set_uniform1f( "u_ReflectionHeight", 0 );

        glCullFace( GL_BACK );
    }
    glDisable( GL_STENCIL_TEST );

    if ( useFrameBuffer ) {
        FrameBuffer::bind_display( );
        showErrors( );
        glDisable( GL_DEPTH_TEST );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

        // glStencilFunc -> pass or discard
        // glStencilOp -> action to do on the scencil buffer.

        glEnable( GL_STENCIL_TEST );
        glStencilFunc( GL_ALWAYS, 1, 0xFF );               // all fragments should pass the stencil test because we don't care.
        glStencilOp( GL_REPLACE, GL_REPLACE, GL_REPLACE ); // Any drawing impacts the stencil buffer and writes a 1.
        this->fullScreenQuad.draw_texture( this->renderer, this->reflectionTexture, this->depthStencilTexture, y_height < 0 ? 0.1 : 0.2, allowBlur, headInWater );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

        glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP ); // Don't change the stencil buffer while we're using it.
        glStencilFunc( GL_EQUAL, 1, 0xff );       // If the stencil value is 1, allow drawing.
        this->fullScreenQuad.draw_texture( this->renderer, this->blockTexture, this->depthStencilTexture, 1.0, allowBlur, headInWater );
        glStencilFunc( GL_NOTEQUAL, 1, 0xff ); // If the stencil value isn't 1 allow drawing.
        this->fullScreenQuad.draw_texture( this->renderer, this->blockTexture, this->depthStencilTexture, 1.0, false, headInWater );
        glDisable( GL_STENCIL_TEST );

        if ( usingReflections ) {
            this->fullScreenQuad.draw_texture( this->renderer, this->reflectionTexture, this->depthStencilTexture, y_height < 0 ? 0.1 : 0.2, allowBlur, headInWater );
        }
        glEnable( GL_DEPTH_TEST );
    }
    // chunk_loader_draw_chunks( &this->loadedChunks, mvp, &this->renderer, true, false ); // Water
    glDisable( GL_STENCIL_TEST );
}
void World::cleanup( ) {

    this->chunkLoader.cleanup( );
    this->skyBox.destroy( );
    this->multiplayer_avatars.cleanup( );

    this->vbl_block.destroy( );
    this->vbl_coords.destroy( );
    if ( SUPPORTS_FRAME_BUFFER ) {
        this->frameBuffer.destroy( );
        this->blockTexture.destroy( );
        this->reflectionTexture.destroy( );
        this->depthStencilTexture.destroy( );
        this->fullScreenQuad.destroy( );
    }
}

int World::can_fixup_chunk( Chunk &chunk, const glm::ivec3 &offset ) {
    glm::vec3 chunk_with_offset = chunk.chunk_pos + offset;
    Chunk *fixupChunk_prt = this->chunkLoader.get_chunk( chunk_with_offset );
    if ( fixupChunk_prt ) {
        Chunk &fixupChunk = *fixupChunk_prt;
        if ( !fixupChunk.is_loading ) {
            return 1;
        }
    }
    return 0;
}

void World::fixup_chunk( Chunk &chunk, const glm::ivec3 &offset, const glm::ivec3 &pos, BlockState blockState ) {
    // pr_debug( "Fixup Offset: %d %d %d", x, y, z );
    glm::vec3 chunk_with_offset = chunk.chunk_pos + offset;
    Chunk *fixupChunk_prt = this->chunkLoader.get_chunk( chunk_with_offset );
    if ( fixupChunk_prt ) {
        Chunk &fixupChunk = *fixupChunk_prt;
        if ( fixupChunk.is_loading ) {
            // This shouldn't happen because can_fixup_chunk should be called first.
            pr_debug( "Ekk, still loading. You'll probably get a lighting bug." );
            return;
        }
        fixupChunk.set_block( pos, blockState );
        fixupChunk.dirty = 1;
        fixupChunk.needs_repopulation = 1;
    }
}

Chunk *World::get_loaded_chunk( const glm::ivec3 &block_pos ) {
    glm::vec3 chunk_size = glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / chunk_size );
    return this->chunkLoader.get_chunk( chunk_pos );
}

BlockState World::get_block_from_chunk( const Chunk &chunk, const glm::ivec3 &block_pos ) {
    glm::ivec3 chunk_size = glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    glm::ivec3 diff = block_pos - ( chunk.chunk_pos * chunk_size );
    return chunk.get_block( diff );
}

BlockState World::get_loaded_block( const glm::ivec3 &block_pos ) {
    Chunk *chunk_prt = this->get_loaded_chunk( block_pos );
    if ( chunk_prt ) {
        Chunk &chunk = *chunk_prt;
        if ( chunk.is_loading ) {
            return BLOCK_STATE_LAST_BLOCK_ID;
        }
        BlockState blockState = get_block_from_chunk( chunk, block_pos );
        return blockState;
    }
    return BLOCK_STATE_LAST_BLOCK_ID;
}

void World::set_loaded_block( const glm::ivec3 &block_pos, BlockState blockState ) {
    glm::ivec3 chunk_size = glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / glm::vec3( chunk_size ) );

    Chunk *chunk_prt = this->chunkLoader.get_chunk( chunk_pos );
    if ( chunk_prt ) {
        Chunk &chunk = *chunk_prt;
        glm::ivec3 diff = block_pos - chunk_pos * chunk_size;
        // pr_debug( "Orig Offset: %d %d %d", diff_x, diff_y, diff_z );

        for ( int i = -1; i < 2; i++ ) {
            for ( int j = -1; j < 2; j++ ) {
                for ( int k = -1; k < 2; k++ ) {
                    int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0; //
                    int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0; //
                    int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
                    int needs_update = ( needs_update_x && needs_update_y && needs_update_z ) && !( i == 0 && j == 0 && k == 0 );
                    if ( needs_update ) {
                        glm::ivec3 offset = glm::ivec3( i, j, k );
                        if ( !can_fixup_chunk( chunk, offset ) ) {
                            pr_debug( "Ekk, can't fixup block, so not placing" );
                            return;
                        }
                    }
                }
            }
        }
        for ( int i = -1; i < 2; i++ ) {
            for ( int j = -1; j < 2; j++ ) {
                for ( int k = -1; k < 2; k++ ) {

                    int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0; //
                    int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0; //
                    int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;

                    int new_i = i * needs_update_x;
                    int new_j = j * needs_update_y;
                    int new_k = k * needs_update_z;

                    int needs_update = ( needs_update_x && needs_update_y && needs_update_z ) && !( i == 0 && j == 0 && k == 0 );
                    // pr_debug( "Chunk Dir: %d %d %d:%d", i, j, k, needs_update );
                    // pr_debug( "                    Needs Updates: %d %d %d:%d", needs_update_x, needs_update_y, needs_update_z, needs_update );
                    // pr_debug( "                                New Offset: %d %d %d:%d", new_i, new_j, new_k, needs_update );

                    if ( needs_update ) {
                        glm::ivec3 new_pos = glm::ivec3( new_i, new_j, new_k );
                        glm::ivec3 offset = glm::ivec3( i, j, k );
                        glm::ivec3 pos = diff - glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) * new_pos;
                        fixup_chunk( chunk, offset, pos, blockState );
                    }
                }
            }
        }
        chunk.set_block( diff, blockState );
        chunk.dirty = 1;
        chunk.needs_repopulation = 1;

    } else {
        // This just means mouse is not pointing at a block
        // pr_debug( "Could not find the pointed to chunk" );
    }
}

// bool world_do_random_tick2( Chunk *chunk, const glm::vec3 &pos, BlockState *blockState ) {
//     blockState->id = BlockID::DIRT;
//     blockState->display_id = blockState->id;
//     blockState->rotation = 0;
//     blockState->current_redstone_power = 0;
//     return true;
// }

#define OFFSETS_LEN_ADJ 6
static const glm::ivec3 offsets_adj[ OFFSETS_LEN_ADJ ] = {
    glm::ivec3( -1, 0, 0 ), //
    glm::ivec3( 1, 0, 0 ),  //
    glm::ivec3( 0, -1, 0 ), //
    glm::ivec3( 0, 1, 0 ),  //
    glm::ivec3( 0, 0, -1 ), //
    glm::ivec3( 0, 0, 1 ),  //
};

#define OFFSETS_LEN_DIAG ( ( 3 * 3 * 3 ) - 1 )
static const glm::ivec3 offsets_diag[ OFFSETS_LEN_DIAG ] = {
    glm::ivec3( -1, -1, -1 ), //
    glm::ivec3( -1, -1, 0 ),  //
    glm::ivec3( -1, -1, 1 ),  //
    glm::ivec3( -1, 0, -1 ),  //
    glm::ivec3( -1, 0, 0 ),   //
    glm::ivec3( -1, 0, 1 ),   //
    glm::ivec3( -1, 1, -1 ),  //
    glm::ivec3( -1, 1, 0 ),   //
    glm::ivec3( -1, 1, 1 ),   //

    glm::ivec3( 0, -1, -1 ), //
    glm::ivec3( 0, -1, 0 ),  //
    glm::ivec3( 0, -1, 1 ),  //
    glm::ivec3( 0, 0, -1 ),  //
    // glm::ivec3( 0, 0, 0 ),   //
    glm::ivec3( 0, 0, 1 ),  //
    glm::ivec3( 0, 1, -1 ), //
    glm::ivec3( 0, 1, 0 ),  //
    glm::ivec3( 0, 1, 1 ),  //

    glm::ivec3( 1, -1, -1 ), //
    glm::ivec3( 1, -1, 0 ),  //
    glm::ivec3( 1, -1, 1 ),  //
    glm::ivec3( 1, 0, -1 ),  //
    glm::ivec3( 1, 0, 0 ),   //
    glm::ivec3( 1, 0, 1 ),   //
    glm::ivec3( 1, 1, -1 ),  //
    glm::ivec3( 1, 1, 0 ),   //
    glm::ivec3( 1, 1, 1 ),   //

};

#define OFFSETS_GRASS ( ( 3 * 3 * 3 ) - 1 )
static const glm::ivec3 offsets_grass[ OFFSETS_GRASS ] = {
    // glm::ivec3( -1, -1, -1 ), //
    glm::ivec3( -1, -1, 0 ), //
    // glm::ivec3( -1, -1, 1 ),  //
    // glm::ivec3( -1, 0, -1 ),  //
    glm::ivec3( -1, 0, 0 ), //
    // glm::ivec3( -1, 0, 1 ),   //
    // glm::ivec3( -1, 1, -1 ),  //
    glm::ivec3( -1, 1, 0 ), //
    // glm::ivec3( -1, 1, 1 ),   //

    glm::ivec3( 0, -1, -1 ), //
    glm::ivec3( 0, -1, 0 ),  //
    glm::ivec3( 0, -1, 1 ),  //
    glm::ivec3( 0, 0, -1 ),  //
    // glm::ivec3( 0, 0, 0 ),   //
    glm::ivec3( 0, 0, 1 ),  //
    glm::ivec3( 0, 1, -1 ), //
    glm::ivec3( 0, 1, 0 ),  //
    glm::ivec3( 0, 1, 1 ),  //

    // glm::ivec3( 1, -1, -1 ), //
    glm::ivec3( 1, -1, 0 ), //
    // glm::ivec3( 1, -1, 1 ),  //
    // glm::ivec3( 1, 0, -1 ),  //
    glm::ivec3( 1, 0, 0 ), //
    // glm::ivec3( 1, 0, 1 ),   //
    // glm::ivec3( 1, 1, -1 ),  //
    glm::ivec3( 1, 1, 0 ), //
    // glm::ivec3( 1, 1, 1 ),   //

};

bool World::any_neighbor_adj_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id ) {
    for ( int i = 0; i < OFFSETS_LEN_ADJ; i++ ) {
        const glm::ivec3 &offset = offsets_adj[ i ];
        glm::ivec3 pos_offset = glm::ivec3( pos.x + offset.x, pos.y + offset.y, pos.z + offset.z );
        BlockState blockState = chunk.get_block( pos_offset );
        if ( blockState.id == id ) {
            return true;
        }
    }
    return false;
}

bool world_any_neighbor_diag_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id ) {

    for ( int i = 0; i < OFFSETS_LEN_DIAG; i++ ) {
        const glm::ivec3 &offset = offsets_diag[ i ];
        glm::ivec3 pos_offset = glm::ivec3( pos.x + offset.x, pos.y + offset.y, pos.z + offset.z );
        BlockState blockState = chunk.get_block( pos_offset );
        if ( blockState.id == id ) {
            return true;
        }
    }
    return false;
}

bool world_any_neighbor_grass_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id ) {
    for ( int i = 0; i < OFFSETS_GRASS; i++ ) {
        const glm::ivec3 &offset = offsets_grass[ i ];
        glm::ivec3 pos_offset = glm::ivec3( pos.x + offset.x, pos.y + offset.y, pos.z + offset.z );
        BlockState blockState = chunk.get_block( pos_offset );
        if ( blockState.id == id ) {
            return true;
        }
    }
    return false;
}

bool World::do_random_tick_on_block( Chunk &chunk, const glm::vec3 &pos, BlockState &blockState ) {
    // pr_debug( "Ticking state" );
    BlockID id = blockState.id;
    if ( id == BlockID::AIR ) {
        return false;
    }
    // pr_debug( "Ticking id:%d", id );

    BlockState stateUp = chunk.get_block( glm::vec3( pos.x + 0, pos.y + 1, pos.z + 0 ) );
    // BlockState stateDown = chunk_get_block( chunk, glm::vec3( pos.x + 0, pos.y - 1, pos.z + 0 ) );
    // BlockState stateLeft = chunk_get_block( chunk, glm::vec3( pos.x + 1, pos.y + 0, pos.z + 0 ) );
    // BlockState stateRight = chunk_get_block( chunk, glm::vec3( pos.x - 1, pos.y + 0, pos.z + 0 ) );
    // BlockState stateFront = chunk_get_block( chunk, glm::vec3( pos.x + 0, pos.y + 0, pos.z + 1 ) );
    // BlockState stateBack = chunk_get_block( chunk, glm::vec3( pos.x + 0, pos.y + 0, pos.z - 1 ) );

    Block *blockUp = block_definition_get_definition( stateUp.id );
    // Block *blockDown = block_definition_get_definition( stateDown.id );
    // Block *blockLeft = block_definition_get_definition( stateLeft.id );
    // Block *blockRight = block_definition_get_definition( stateRight.id );
    // Block *blockFront = block_definition_get_definition( stateFront.id );
    // Block *blockBack = block_definition_get_definition( stateBack.id );

    if ( id == BlockID::GRASS ) {
        if ( blockUp->renderOrder == RenderOrder_Opaque ) {
            blockState.id = BlockID::DIRT;
            blockState.display_id = blockState.id;
            return true;
        }
    }
    if ( id == BlockID::DIRT ) {
        if ( blockUp->renderOrder != RenderOrder_Opaque && world_any_neighbor_grass_id( chunk, pos, BlockID::GRASS ) ) {
            blockState.id = BlockID::GRASS;
            blockState.display_id = blockState.id;
            return true;
        }
    }
    if ( id == BlockID::DIRT ) {
        if ( blockUp->renderOrder != RenderOrder_Opaque && world_any_neighbor_grass_id( chunk, pos, BlockID::PODZEL ) ) {
            blockState.id = BlockID::PODZEL;
            blockState.display_id = blockState.id;
            return true;
        }
    }
    return false;
}

int counter = 0;
bool World::process_random_ticks_on_chunk( Chunk &chunk ) {
    bool anyBlockStateChanged = false;
    int index = CHUNK_BLOCK_DRAW_START + ( rand( ) % ( CHUNK_BLOCK_DRAW_STOP - CHUNK_BLOCK_DRAW_START + 1 ) );
    int x, y, z;
    int drawn_block = Chunk::get_coords_from_index( index, x, y, z );
    if ( drawn_block ) { // TODO re-try if this isn't a valid block coord
                         // pr_debug( "Ticking index:%d", index );
        glm::ivec3 pos = glm::ivec3( x, y, z );
        BlockState blockState = chunk.get_block( pos );
        bool changedState = do_random_tick_on_block( chunk, pos, blockState );
        // bool changedState = false;
        if ( changedState ) {
            glm::ivec3 chunk_size = glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );

            glm::ivec3 chunk_pos = chunk.chunk_pos * chunk_size;
            glm::ivec3 world_pos = pos + chunk_pos;
            set_loaded_block( world_pos, blockState );
            anyBlockStateChanged = true;
        }
    }
    return anyBlockStateChanged;
}

void World::process_random_ticks( ) {
    pr_debug( "Tick" );
    srand( counter );
    counter += 1;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        // TODO randomly pick chunks.
        Chunk &chunk = this->chunkLoader.chunkArray[ i ];
        if ( !chunk.is_loading ) {
            bool anyStateChanged = false;
            anyStateChanged |= process_random_ticks_on_chunk( chunk );
            if ( anyStateChanged ) {
                chunk.dirty = 1;
                chunk.needs_repopulation = 1;
            }
        }
    }
}
