// Render-thread draw path: occlusion/translucency env config, the
// drawable-list rebuild, frustum + hardware-occlusion culling, and the
// per-order instanced draw pass. Everything runs on the render thread
// inside World::render — split out of chunk_loader.cpp, which keeps the
// streaming/update pipeline.
#include <string.h>
#include <strings.h>
#include <algorithm>

#include "common/Platforms.hpp" // GL headers + platform macros
#include "common/Logging.hpp"
#include "common/chunk_loader.hpp"
#include "common/block_instance_batch.hpp"
#include "common/utils/env_config.hpp"
#include "common/constants.hpp"

// Hardware occlusion culling via per-chunk AABB queries. On by default on
// all platforms — the query API is GLES3/WebGL2 core — with
// REPGAME_OCCLUSION=0 as a kill switch where env vars exist. Mobile
// tile-based GPUs may stall on query boundaries; if that regresses, this
// needs a per-platform default again.
static int occlusion_enabled( ) {
    static int enabled = -1;
    if ( enabled < 0 ) {
        enabled = env_bool( "REPGAME_OCCLUSION", true ) ? 1 : 0;
    }
    return enabled;
}

// REPGAME_TRANSLUCENT_BLEND: how the RenderOrder_Translucent pass (stained
// glass, ice, honey, slime...) composites. See docs/rendering.md.
typedef enum { TranslucentBlend_Legacy, TranslucentBlend_Alpha, TranslucentBlend_Multiply } TranslucentBlend;
static TranslucentBlend translucent_blend_mode( ) {
    static const TranslucentBlend mode = [] {
        const char *v = env_get( "REPGAME_TRANSLUCENT_BLEND" );
        if ( v != nullptr && strcmp( v, "legacy" ) == 0 ) {
            return TranslucentBlend_Legacy;
        }
        if ( v != nullptr && strcmp( v, "multiply" ) == 0 ) {
            return TranslucentBlend_Multiply;
        }
        return TranslucentBlend_Alpha;
    }( );
    return mode;
}

// REPGAME_TRANSLUCENT_AFTER_WATER: world.cpp splits the translucent pass
// per-fragment at the sea-level water plane ("split", the default), draws it
// in its own call after the water pass ("1"/After), or with the main Blocks
// call before water ("0"/Before). legacy keeps the original pass order.
TranslucentWaterOrder translucent_water_order( ) {
    static const TranslucentWaterOrder order = [] {
        const char *v = env_get( "REPGAME_TRANSLUCENT_AFTER_WATER" );
        TranslucentWaterOrder parsed = TranslucentWaterOrder::Split;
        if ( v != nullptr && v[ 0 ] != '\0' ) {
            if ( strcmp( v, "0" ) == 0 || strcasecmp( v, "false" ) == 0 || strcasecmp( v, "off" ) == 0 ) {
                parsed = TranslucentWaterOrder::Before;
            } else if ( strcmp( v, "1" ) == 0 || strcasecmp( v, "true" ) == 0 || strcasecmp( v, "on" ) == 0 ) {
                parsed = TranslucentWaterOrder::After;
            } else if ( strcmp( v, "split" ) != 0 ) {
                pr_debug( "REPGAME_TRANSLUCENT_AFTER_WATER: unknown value '%s' (want 0|1|split)", v );
            }
        }
        return translucent_blend_mode( ) == TranslucentBlend_Legacy ? TranslucentWaterOrder::Before : parsed;
    }( );
    return order;
}

// How far the camera may drift (blocks) before the full drawable list is
// re-sorted. Opaque/Flowers ordering only affects early-z and occlusion
// efficiency, so it may lag the camera; the blended subset list re-sorts on
// any move at all because its order is the composited colour.
static constexpr float CHUNK_SORT_RESORT_DIST = 2.0f;

// Compact one draw list in place after slot removals, preserving the order
// of the surviving entries — including the sorted prefix, so *_sorted_n only
// drops by how many kept entries were already sorted. Returns the new sorted
// prefix length; *n is updated to the surviving count.
int ChunkLoader::compact_slot_list( Chunk **list, int *n, int sorted_n, uint8_t bit ) {
    int w = 0;
    int new_sorted_n = 0;
    for ( int i = 0; i < *n; i++ ) {
        Chunk *chunk = list[ i ];
        const int slot = static_cast<int>( chunk - this->chunkArray );
        if ( this->slot_listed[ slot ] & bit ) {
            if ( i < sorted_n ) {
                new_sorted_n++;
            }
            list[ w++ ] = chunk;
        }
    }
    *n = w;
    return new_sorted_n;
}

void ChunkLoader::rebuild_drawable_list( ) {
    // slot_want[i] is what slot i wants in the draw lists (maintained live by
    // Chunk::refresh_slot_flags on every contributing field write); slot_
    // listed[i] is what the lists currently hold. This loop diffs the byte
    // arrays only — the big Chunk records are never touched — so it costs a
    // fraction of the old per-slot probe. Removals are compacted out of the
    // lists in place; newcomers append to the unsorted tail past *_sorted_n,
    // and draw() merge-sorts the tail into the surviving sorted prefix.
    bool drawable_removed = false;
    bool blended_removed = false;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        const uint8_t want = this->slot_want[ i ];
        const uint8_t diff = want ^ this->slot_listed[ i ];
        if ( diff == 0 ) {
            continue;
        }
        this->slot_listed[ i ] = want;
        if ( diff & SLOT_DRAWABLE ) {
            if ( want & SLOT_DRAWABLE ) {
                this->drawable_chunks[ this->num_drawable++ ] = &this->chunkArray[ i ];
            } else {
                drawable_removed = true;
            }
        }
        if ( diff & SLOT_BLENDED ) {
            if ( want & SLOT_BLENDED ) {
                this->blended_chunks[ this->num_blended++ ] = &this->chunkArray[ i ];
            } else {
                blended_removed = true;
            }
        }
    }
    if ( drawable_removed ) {
        this->drawable_sorted_n = this->compact_slot_list( this->drawable_chunks, &this->num_drawable, this->drawable_sorted_n, SLOT_DRAWABLE );
    }
    if ( blended_removed ) {
        this->blended_sorted_n = this->compact_slot_list( this->blended_chunks, &this->num_blended, this->blended_sorted_n, SLOT_BLENDED );
    }
#ifdef REPGAME_DEBUG
    // Safety net for the mirror: cheap byte diff above assumes slot_want can
    // never drift from the truth fields. Recompute it from scratch every so
    // often and assert on the first divergence.
    static int verify_countdown = 0;
    if ( --verify_countdown <= 0 ) {
        verify_countdown = 60;
        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            const Chunk &chunk = this->chunkArray[ i ];
            uint8_t truth = 0;
            if ( chunk.load_state == CHUNK_LIVE && chunk.should_render ) {
                truth |= SLOT_DRAWABLE;
                if ( chunk.layers != nullptr && //
                     ( chunk.layers[ RenderOrder_Translucent ].num_instances != 0 || //
                       chunk.layers[ RenderOrder_Water ].num_instances != 0 ) ) {
                    truth |= SLOT_BLENDED;
                }
            }
            if ( this->slot_want[ i ] != truth ) {
                pr_debug( "slot_want mismatch slot=%d pos=(%d,%d,%d) want=%u truth=%u", i,
                          chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z, this->slot_want[ i ], truth );
                assert( this->slot_want[ i ] == truth );
            }
        }
    }
#endif
}
// Bounding-sphere radius for frustum culling: a chunk's mesh is confined to
// its own [0, CHUNK_SIZE) volume, so half the box diagonal plus one block of
// slack bounds every drawn vertex.
static constexpr float CHUNK_CULL_RADIUS = ( CHUNK_SIZE_X + 1 ) * 0.5f * 1.73205080757f; // sqrt(3)

void ChunkLoader::calculate_cull( const glm::mat4 &mvp, const bool saveAsReflection, const glm::ivec3 &renderOrigin ) const {
    // Extract the 6 frustum planes from the MVP rows (Gribb-Hartmann): a point
    // p is inside the clip volume iff (row3 +- row_i) . p >= 0 for each axis i.
    // GLM is column-major, so row i is (mvp[0][i], mvp[1][i], mvp[2][i], mvp[3][i]).
    // Testing the center's NDC coords against 1+r/w (the old approach) treats
    // the bounding sphere as isotropic in NDC, but perspective scales x by
    // f/aspect and y by f — with aspect < ~0.84 (narrow windows) the margin
    // under-estimates the sphere's x extent and visible chunks were culled.
    const glm::vec4 row_x( mvp[ 0 ][ 0 ], mvp[ 1 ][ 0 ], mvp[ 2 ][ 0 ], mvp[ 3 ][ 0 ] );
    const glm::vec4 row_y( mvp[ 0 ][ 1 ], mvp[ 1 ][ 1 ], mvp[ 2 ][ 1 ], mvp[ 3 ][ 1 ] );
    const glm::vec4 row_z( mvp[ 0 ][ 2 ], mvp[ 1 ][ 2 ], mvp[ 2 ][ 2 ], mvp[ 3 ][ 2 ] );
    const glm::vec4 row_w( mvp[ 0 ][ 3 ], mvp[ 1 ][ 3 ], mvp[ 2 ][ 3 ], mvp[ 3 ][ 3 ] );
    const glm::vec4 planes[ 6 ] = { row_w + row_x, row_w - row_x, //
                                    row_w + row_y, row_w - row_y, //
                                    row_w + row_z, row_w - row_z };
    // The plane normals aren't unit length; pre-scale the sphere radius by
    // |n.xyz| so the inner loop is a single dot-product compare per plane.
    float scaled_radius[ 6 ];
    for ( int p = 0; p < 6; p++ ) {
        scaled_radius[ p ] = CHUNK_CULL_RADIUS * glm::length( glm::vec3( planes[ p ] ) );
    }
    for ( int i = 0; i < this->num_drawable; i++ ) {
        int final_is_visible = 1;
        int full_inside = CULL_NON_VISIBLE ? 1 : 0;
        Chunk *chunk = this->drawable_chunks[ i ];
        if constexpr ( CULL_NON_VISIBLE ) {
            const glm::vec4 chunk_coords = glm::vec4( chunk->chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 - renderOrigin, 1 );
            for ( int p = 0; p < 6; p++ ) {
                const float d = glm::dot( chunk_coords, planes[ p ] );
                if ( d < -scaled_radius[ p ] ) {
                    final_is_visible = 0;
                }
                if ( d < scaled_radius[ p ] ) {
                    full_inside = 0;
                }
            }
        }
        if ( saveAsReflection ) {
            chunk->cached_cull_reflect = !final_is_visible;
        } else {
            // A probe whose box sat fully inside the frustum tested every
            // face, so its verdict survives rotations at a fixed eye
            // position; a straddler was only probed on its on-screen part.
            chunk->occl.in_full = full_inside;
            // Re-entering the frustum invalidates the stale occl verdict:
            // while the chunk was culled the camera may have moved, so a
            // still-hidden chunk leaves a hole until the fresh probe lands.
            // Draw it instead — overdraw at worst — and let this frame's
            // probe re-hide it if it's still occluded.
            if ( chunk->cached_cull_normal && final_is_visible ) {
                chunk->occl.reset_streak( );
            }
            chunk->cached_cull_normal = !final_is_visible;
        }
    }
}

int shouldInc = 0;
int showRotation = 0;
void ChunkLoader::draw( const glm::mat4 &mvp, const Renderer &renderer, const Texture &texture, unsigned order_mask, bool draw_reflect, bool use_frame_buffer, const glm::vec3 &render_origin, const glm::vec3 &camera_pos, const glm::vec3 &view_dir ) {
    this->shader.set_uniform_mat4f( "u_MVP", mvp );
    {
        GLint vp[ 4 ];
        glGetIntegerv( GL_VIEWPORT, vp );
        this->shader.set_uniform2f( "u_ViewportSize", (float)vp[ 2 ], (float)vp[ 3 ] );
    }
    // Screen-space corner grow (chunk_vertex.glsl), tunable via env.
    static const float face_grow_px = env_float( "REPGAME_FACE_GROW_PX", CHUNK_FACE_GROW_PX );
    // All per-chunk light volumes share one reserved texture unit; each chunk
    // binds its 3D texture + u_LightBase right before drawing (Chunk::draw).
    this->shader.set_uniform1i( "u_LightTex", light_texture_unit( ) );
    glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
    // glGetUniformLocation once per draw instead of once per chunk — the
    // per-chunk set_uniform3f it replaced cost ~6000 lookups/frame.
    const int light_base_loc = this->shader.get_uniform_location( "u_LightBase" );

    // Front-to-back ordering: depth-writing layers draw nearest-first so
    // early-z kills occluded fragments (and, later, so occluders are in the
    // depth buffer before occlusion queries test farther chunks). Two sorted
    // lists serve that: drawable_chunks for the depth-writing orders (their
    // order only costs early-z efficiency when stale, so it may lag the
    // camera up to CHUNK_SORT_RESORT_DIST blocks), and the much smaller
    // blended_chunks for Water/Translucent — the blended translucent pass
    // writes no depth, so overlapping glass from different chunks composites
    // in exactly list order and must re-sort on any camera move at all.
    // Both sort by distance to the camera (not the chunk-snapped render
    // origin — that corner produces masses of exact ties and jumps 16
    // blocks at chunk boundaries) with a lexicographic chunk_pos tie-break,
    // a strict total order that never reshuffles with input order; exact
    // for the reflected pass too — the mirror is an isometry.
    // rebuild_drawable_list (render_chunks, per frame) maintains the lists
    // incrementally off the slot_want byte mirror: entries [0,*_sorted_n)
    // survive in the previous sorted order and newcomers sit in the tail —
    // a resort then re-sorts the prefix for the current camera, sorts the
    // (few, usually far) newcomers, and merges the two halves over
    // precomputed keys. Without newcomers the full list only re-sorts when
    // the camera moved past CHUNK_SORT_RESORT_DIST, and its prefix then
    // insertion-sorts (std::sort past CHUNK_SIZE_X of movement — teleports
    // aren't nearly-sorted). The other draw calls in the same frame
    // (4 orders, plus a second VR eye) then no-op. Sort keys use the rebased
    // camera; the resort gates track the camera in absolute coords so a
    // render-origin snap doesn't read as movement.
    const glm::vec3 cam_abs = render_origin + camera_pos;
    {
        const glm::vec3 d = cam_abs - this->last_full_sort_cam;
        const float moved2 = glm::dot( d, d );
        if ( this->drawable_sorted_n < this->num_drawable || //
             moved2 > CHUNK_SORT_RESORT_DIST * CHUNK_SORT_RESORT_DIST ) {
            sort_chunks_by_distance( this->drawable_chunks, this->num_drawable, this->drawable_sorted_n, render_origin, camera_pos, nullptr,
                                     moved2 > static_cast<float>( CHUNK_SIZE_X ) * CHUNK_SIZE_X, this->sort_scratch );
            this->drawable_sorted_n = this->num_drawable;
            this->last_full_sort_cam = cam_abs;
        }
    }
    {
        const glm::vec3 d = cam_abs - this->last_blended_sort_cam;
        const float moved2 = glm::dot( d, d );
        const float dir_dot = glm::dot( view_dir, this->last_blended_sort_dir );
        // Painter order is keyed on view depth, so it must also re-sort on
        // rotation — the incremental list sort is cheap enough for every
        // frame while looking around.
        if ( this->blended_sorted_n < this->num_blended || cam_abs != this->last_blended_sort_cam || view_dir != this->last_blended_sort_dir ) {
            // full_prefix on a teleport or a big swing: the insertion path
            // assumes near-sorted, which a ~60+ degree rotation scrambles.
            sort_chunks_by_distance( this->blended_chunks, this->num_blended, this->blended_sorted_n, render_origin, camera_pos, &view_dir,
                                     moved2 > static_cast<float>( CHUNK_SIZE_X ) * CHUNK_SIZE_X || dir_dot < 0.5f, this->sort_scratch );
            this->blended_sorted_n = this->num_blended;
            this->last_blended_sort_cam = cam_abs;
            this->last_blended_sort_dir = view_dir;
        }
    }

    // A chunk whose box contains the camera can never be occlusion-tested:
    // every in-view box face then sits BEHIND the chunk's own content, so
    // the probe self-fails even though the terrain is plainly visible
    // (looking straight down = the whole screen is the containing chunk).
    // That oscillates draw/hide every other frame. The margin covers the
    // near plane; the gate also un-hides instantly when the camera walks
    // into a hidden chunk (noclip into a sealed pocket draws correctly).
    const auto cam_in_box = [ &camera_pos, &render_origin ]( const Chunk &chunk ) {
        const glm::vec3 e = camera_pos - glm::vec3( chunk.chunk_pos * CHUNK_SIZE_I ) + render_origin;
        return e.x > -1.0f && e.x < CHUNK_SIZE_X + 1.0f && e.y > -1.0f && e.y < CHUNK_SIZE_Y + 1.0f && e.z > -1.0f && e.z < CHUNK_SIZE_Z + 1.0f;
    };

    // Occlusion verdicts are position-relative: a probe only proves coverage
    // against the depth buffer seen from the eye position it ran under.
    // occl_view_gen bumps when the camera moves >0.3 blocks, invalidating
    // every verdict; occl_rot_gen bumps on a >0.02 rotation-column change
    // (or alongside a position bump) and only invalidates verdicts earned
    // while the chunk wasn't fully inside the frustum — a full-box probe
    // stays valid under any rotation from the same eye position. See
    // chunk_occlusion.hpp for how ChunkOcclState applies the two.
    long view_gen = 0;
    long rot_gen = 0;
    if ( occlusion_enabled( ) && !draw_reflect ) {
        // Non-probe passes (the second stereo eye) reuse the current
        // generations without comparing/bumping them against their own mvp.
        if ( this->occl_probe_pass ) {
            const glm::vec3 d = cam_abs - this->occl_last_cam;
            bool pos_changed = glm::dot( d, d ) > 0.3f * 0.3f;
            bool rot_changed = pos_changed;
            for ( int c = 0; c < 3 && !rot_changed; c++ ) {
                for ( int r = 0; r < 4 && !rot_changed; r++ ) {
                    rot_changed = fabsf( mvp[ c ][ r ] - this->occl_last_rot_mvp[ c ][ r ] ) > 0.02f;
                }
            }
            if ( pos_changed ) {
                this->occl_last_cam = cam_abs;
                this->occl_view_gen++;
            }
            if ( rot_changed ) {
                this->occl_last_rot_mvp = mvp;
                this->occl_rot_gen++;
            }
        }
        view_gen = this->occl_view_gen;
        rot_gen = this->occl_rot_gen;
    }

    // pr_debug( "Drawing %d chunks", this->numLoadedChunks );
    // Order 0 (RenderOrder_Transparent) is reachable only through an
    // explicit mask bit: meshed blocks never emit it, but the fluid
    // boundary films do — world.cpp draws that bucket after Translucent.
    for ( int renderOrder = LAST_RENDER_ORDER - 1; renderOrder >= 0; renderOrder-- ) {
        if ( order_mask & render_order_bit( static_cast<RenderOrder>( renderOrder ) ) ) {
            shader.set_uniform1f( "u_shouldDiscardAlpha", renderOrder != RenderOrder_Water && renderOrder != RenderOrder_Translucent && renderOrder != RenderOrder_Transparent );
            // Screen-space face grow, opaque only: water/translucent/flowers
            // are blended, so overlapping grown edges would blend twice and
            // show as dark dashed lines. Debug-flag bit 64 disables it.
            shader.set_uniform1f( "u_FaceGrowPx", ( renderOrder == RenderOrder_Opaque && ( this->debug_flags & 64 ) == 0 ) ? face_grow_px : 0.0f );
            // Set texture wrap mode once per render order instead of per chunk draw call.
            // glTexParameteri edits the texture bound on the *active* unit, so
            // re-bind the atlas first — the per-chunk loop leaves the light
            // unit active from the previous order.
            texture.bind( );
            if ( renderOrder == RenderOrder_Flowers ) {
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            } else {
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_S, GL_REPEAT );
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_T, GL_REPEAT );
            }
            // Select the shared light unit once per pass — the glTexParameteri
            // calls above must run first, then per-chunk draws only need a
            // glBindTexture (Chunk::draw).
            glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
            // Occlusion probes run only in the normal pass (the reflection
            // clears depth and rebuilds it from a mirrored view — its
            // visibility set is different, so it draws everything the
            // frustum keeps). The occl hide gate still applies to every
            // order here and to the stencil-water pass: a chunk whose whole
            // box is occluded can't show water or flowers either.
            const bool occl = occlusion_enabled( ) && !draw_reflect;
            // Blended layers walk the sorted list backwards (back-to-front).
            const bool back_to_front = renderOrder == RenderOrder_Water || renderOrder == RenderOrder_Translucent ||
                                       renderOrder == RenderOrder_Transparent;
            // Translucent pass (REPGAME_TRANSLUCENT_BLEND): draw with depth
            // writes off so a pane drawn first can't punch order-dependent
            // holes in panes/water behind it; the depth TEST stays on so
            // opaque terrain still occludes it. Multiply mode additionally
            // switches to colour-filter blending, which is order-independent.
            // Both restore right after this order's chunk loop. Water keeps
            // writing depth (the X_HIGH composite reconstructs the surface
            // from the post-water depth snapshot).
            const TranslucentBlend tblend = translucent_blend_mode( );
            const bool translucent_pass = renderOrder == RenderOrder_Translucent && tblend != TranslucentBlend_Legacy;
            if ( translucent_pass ) {
                glDepthMask( GL_FALSE );
                if ( tblend == TranslucentBlend_Multiply ) {
#if ( SUPPORTS_FRAME_BUFFER )
                    if ( use_frame_buffer ) {
                        // Filter blend on the colour + reflection attachments;
                        // fog/sky attachments (2/3) stay replace.
                        glBlendFuncSeparatei( 0, GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE );
                        glBlendFuncSeparatei( 1, GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE );
                    } else
#endif
                    {
                        glBlendFuncSeparate( GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE );
                    }
                }
            }
            shader.set_uniform1i( "u_TranslucentMultiply", translucent_pass && tblend == TranslucentBlend_Multiply ? 1 : 0 );
            if ( occl && renderOrder == RenderOrder_Opaque ) {
                // Two sub-passes over the front-to-back list. (1) Consume
                // last frame's probe result (non-blocking) and draw chunks
                // still believed visible — this builds the opaque depth
                // buffer. (2) Probe every frustum-visible chunk's box
                // against that depth with all writes masked. A failed probe
                // means the content (confined to [0,CHUNK_SIZE)) can't be
                // visible; the occl streak gates it out (ChunkOcclState).
                for ( int i = 0; i < this->num_drawable; i++ ) {
                    Chunk &chunk = *this->drawable_chunks[ i ];
                    if ( chunk.cached_cull_normal ) {
                        continue;
                    }
                    // Non-probe passes leave pending queries in flight — the
                    // next probe pass consumes them.
                    if ( this->occl_probe_pass && chunk.occl.pending ) {
                        GLuint available = 0;
                        glGetQueryObjectuiv( chunk.occl.query, GL_QUERY_RESULT_AVAILABLE, &available );
                        if ( available ) {
                            GLuint passed = 0;
                            glGetQueryObjectuiv( chunk.occl.query, GL_QUERY_RESULT, &passed );
                            chunk.occl.record( passed, view_gen, rot_gen );
                        }
                    }
                    if ( cam_in_box( chunk ) ) {
                        chunk.occl.reset_streak( );
                    }
                    if ( !this->occl_hides( chunk ) ) {
                        chunk.light_ensure_texture( this );
                        chunk.draw( renderer, texture, shader, static_cast<RenderOrder>( renderOrder ), draw_reflect, render_origin, light_base_loc );
                    }
                }
                if ( this->occl_probe_pass ) {
                    this->occl_shader.bind( );
                    this->occl_shader.set_uniform_mat4f( "u_MVP", mvp );
                    const int box_min_loc = this->occl_shader.get_uniform_location( "u_BoxMin" );
                    this->occl_va.bind( );
                    // LEQUAL, not LESS: a chunk's boundary surface is coplanar
                    // with its own box face, so under LESS a fully walled chunk
                    // would self-occlude and flicker draw/hide each frame. Ties
                    // only ever occur against the chunk's own geometry
                    // (neighbors sit a whole block away), so passing them is
                    // conservative — overdraw at worst, never a hole. Cull face
                    // off so a camera inside a box still rasterizes it.
                    glDisable( GL_CULL_FACE );
                    glDepthFunc( GL_LEQUAL );
                    glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
                    glDepthMask( GL_FALSE );
                    for ( int i = 0; i < this->num_drawable; i++ ) {
                        Chunk &chunk = *this->drawable_chunks[ i ];
                        if ( chunk.cached_cull_normal || chunk.occl.pending || cam_in_box( chunk ) ) {
                            continue;
                        }
                        if ( chunk.occl.query == 0 ) {
                            glGenQueries( 1, &chunk.occl.query );
                        }
                        const glm::vec3 box_min = glm::vec3( chunk.chunk_pos * CHUNK_SIZE_I ) - render_origin;
                        glUniform3f( box_min_loc, box_min.x, box_min.y, box_min.z );
                        glBeginQuery( GL_ANY_SAMPLES_PASSED, chunk.occl.query );
                        glDrawArrays( GL_TRIANGLES, 0, 36 );
                        glEndQuery( GL_ANY_SAMPLES_PASSED );
                        chunk.occl.mark_probe_issued( view_gen, rot_gen );
                    }
                    glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
#if ( SUPPORTS_FRAME_BUFFER )
                    if ( use_frame_buffer ) {
                        // world.cpp masks the sky-color attachment (3) off after
                        // the sky pass so terrain draws can't clobber the
                        // captured sky — the blanket unmask above re-enabled it,
                        // so put it back or fog compositing blends terrain pixels
                        // toward whatever garbage lands there.
                        glColorMaski( 3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
                    }
#endif
                    glDepthMask( GL_TRUE );
                    glDepthFunc( GL_LESS );
                    glEnable( GL_CULL_FACE );
                }
            } else {
                // Water/Translucent walk the blended subset back-to-front
                // (compositing order); the rest walk the full list
                // front-to-back. Both lists share the same distance order.
                Chunk *const *order_list = back_to_front ? this->blended_chunks : this->drawable_chunks;
                const int order_count = back_to_front ? this->num_blended : this->num_drawable;
                for ( int i = 0; i < order_count; i++ ) {
                    Chunk &chunk = *order_list[ back_to_front ? order_count - 1 - i : i ];
                    if ( draw_reflect ) {
                        if ( !chunk.cached_cull_reflect ) {
                            chunk.light_ensure_texture( this );
                            chunk.draw( renderer, texture, shader, static_cast<RenderOrder>( renderOrder ), draw_reflect, render_origin, light_base_loc );
                        }
                    } else {
                        if ( chunk.cached_cull_normal ||
                             ( occl && this->occl_hides( chunk ) && !cam_in_box( chunk ) ) ) {
                            continue;
                        }
                        if ( renderOrder == RenderOrder_Translucent && tblend != TranslucentBlend_Multiply && this->occl_probe_pass ) {
                            // Instance order inside a chunk is the
                            // compositing order — re-sort it back-to-front
                            // along the view (rate-limited inside). Skip
                            // multiply blending (order-independent anyway)
                            // and the second stereo eye (reuses eye 0's
                            // order — otherwise the two views would
                            // re-sort each other's VBO every frame).
                            chunk.sort_translucent_instances( cam_abs, view_dir );
                        }
                        chunk.light_ensure_texture( this );
                        chunk.draw( renderer, texture, shader, static_cast<RenderOrder>( renderOrder ), draw_reflect, render_origin, light_base_loc );
                    }
                }
            }
            // Block-body model instances (falling blocks, piston movers)
            // ride this order's terrain pass — one group per light chunk,
            // drawn through the same shader + index buffer right after the
            // chunk list so movers pick up identical lighting and blending.
            if ( this->body_batch != nullptr ) {
                // The opaque occlusion probe leaves occl_shader bound; the raw
                // glUniform3f below targets whatever program is current.
                shader.bind( );
                for ( auto &entry : this->body_batch->groups ) {
                    const BlockInstanceBatch::Key &key = entry.first;
                    BlockInstanceBatch::Group &g = entry.second;
                    if ( key.order != renderOrder || g.cpu.empty( ) ) {
                        continue;
                    }
                    Chunk *light_chunk = this->get_chunk( key.chunk_pos );
                    if ( light_chunk != nullptr ) {
                        light_chunk->light_ensure_texture( this );
                    }
                    if ( light_chunk != nullptr && light_chunk->light.texture != 0 &&
                         !light_chunk->light.tex_stale.load( std::memory_order_relaxed ) ) {
                        glBindTexture( GL_TEXTURE_3D, light_chunk->light.texture );
                        const glm::vec3 light_base = glm::vec3( key.chunk_pos * CHUNK_SIZE_I ) - render_origin;
                        glUniform3f( light_base_loc, light_base.x, light_base.y, light_base.z );
                    } else {
                        glBindTexture( GL_TEXTURE_3D, light_fallback_texture( ) );
                        glUniform3f( light_base_loc, -render_origin.x, -render_origin.y, -render_origin.z );
                    }
                    unsigned batch_off = 0;
                    unsigned batch_cnt = 0;
                    chunk_ib_full_face_range( static_cast<RenderOrder>( renderOrder ), draw_reflect, &batch_off, &batch_cnt );
                    if ( batch_cnt == 0 ) {
                        continue;
                    }
                    this->body_batch->ensure_gl( g, g.water_layout );
                    renderer.draw( g.va, chunk_ib_buffer( ), shader, g.cpu.size( ), batch_cnt, batch_off );
                }
            }
            if ( translucent_pass ) {
                glDepthMask( GL_TRUE );
                if ( tblend == TranslucentBlend_Multiply ) {
#if ( SUPPORTS_FRAME_BUFFER )
                    if ( use_frame_buffer ) {
                        glBlendFuncSeparatei( 0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                        glBlendFuncSeparatei( 1, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                    } else
#endif
                    {
                        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
                    }
                }
            }
        }
    }
    if constexpr ( DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
        shouldInc++;
        if ( shouldInc > 100 ) {
            shouldInc = 0;
            showRotation++;
            if ( showRotation >= 8 ) {
                showRotation = 0;
            }
        }
        shader.set_uniform1f( "u_ShowRotation", showRotation );
    }
    shader.set_uniform1f( "u_shouldDiscardAlpha", 1 );
}
