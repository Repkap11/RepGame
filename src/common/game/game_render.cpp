// RepGame member functions: the per-frame draw path.

#include <cmath>
#include <ctime>
#include <string>
#include <unistd.h>
#include <chrono>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/map_storage.hpp"
#include "common/renderer/texture.hpp"
#include "common/renderer/frame_buffer.hpp"
#include "common/world.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/utils/env_config.hpp"
#include "common/light.hpp"
#include "common/map_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/game/game_internal.hpp"

void RepGame::draw( float alpha ) {
    draw_frame( alpha, nullptr, 0 );
}

void RepGame::draw_vr( float alpha, const RenderView *views, int num_views ) {
    draw_frame( alpha, views, num_views );
}

void RepGame::draw_frame( float alpha, const RenderView *views, int num_views ) {
    static int screenshot_counter = 0;
    if ( globalGameState.input.exitGame ) {
        // Don't bother draw the state if the game is exiting
        return;
    }
    const long long t_draw_start = repgame_now_us( );
    profiling.us_render = 0;
    profiling.us_world_draw = 0;
    profiling.us_ui_draw = 0;
    profiling.num_drawable_chunks = 0;
    profiling.num_chunks_remeshed = 0;
    // Interpolate the rendered camera between the previous tick's transform (prev_*) and the
    // current tick's transform. This decouples the visual camera from the fixed-timestep
    // simulation, so panning/walking stays smooth even when the render rate exceeds UPS_RATE
    // or when per-tick input deltas (e.g. mouse motion) are non-uniform.
    if ( alpha < 0.0f ) {
        alpha = 0.0f;
    } else if ( alpha > 1.0f ) {
        alpha = 1.0f;
    }
    glm::dvec3 render_pos = glm::mix( globalGameState.camera.prev_pos, globalGameState.camera.pos, static_cast<double>( alpha ) );
    // Interpolate the feet (not the eye) so a sneak-pose change doesn't shift
    // the ground plane, then add the render-eased eye height so the camera
    // glides down/up over a few ticks instead of snapping.
    const double prev_feet_y = globalGameState.camera.prev_pos.y - globalGameState.camera.prev_eye_height;
    const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
    render_pos.y = glm::mix( prev_feet_y, feet_y, static_cast<double>( alpha ) ) + globalGameState.camera.eye_height_render;
    float render_angle_V = glm::mix( globalGameState.camera.prev_angle_V, globalGameState.camera.angle_V, alpha );
    // angle_H wraps around 360, so interpolate along the shortest angular path.
    float angle_diff = globalGameState.camera.angle_H - globalGameState.camera.prev_angle_H;
    while ( angle_diff > 180.0f ) {
        angle_diff -= 360.0f;
    }
    while ( angle_diff < -180.0f ) {
        angle_diff += 360.0f;
    }
    float render_angle_H = globalGameState.camera.prev_angle_H + angle_diff * alpha;
    while ( render_angle_H < 0.0f ) {
        render_angle_H += 360.0f;
    }
    while ( render_angle_H >= 360.0f ) {
        render_angle_H -= 360.0f;
    }
    if ( run_policy().capture.enabled && run_policy().capture.view_index < static_cast<int>( run_policy().capture.views.size( ) ) ) {
        // Draw the capture view exactly, bypassing tick interpolation and
        // eye-height easing, so the rendered camera is bit-identical to the
        // one an F12 log line hands to REPGAME_CAPTURE.
        const CaptureView &view = run_policy().capture.views[ run_policy().capture.view_index ];
        render_pos = glm::dvec3( view.x, view.y, view.z );
        render_angle_H = view.yaw;
        render_angle_V = view.pitch;
    }
    glm::vec3 render_look;
    glm::mat4 render_rotation;
    glm::mat4 render_view_look;
    glm::mat4 render_view_trans;
    build_camera_view( render_angle_H, render_angle_V, render_pos, render_look, render_rotation, render_view_look, render_view_trans );

    // Floating origin: snap an integer origin to the chunk grid so all
    // coordinates fed to the GPU stay near zero regardless of how far the
    // player is from the world origin. blockCoords in the VBO are
    // integer-valued floats, so subtracting the integer u_Origin in the
    // shader is exact in float32 (both < 2^24). The view translation also
    // becomes small, eliminating the large-float cancellation that caused
    // Z-fighting between the selection outline and terrain at distance.
    const glm::ivec3 renderOrigin = glm::ivec3( glm::floor( render_pos / glm::dvec3( CHUNK_SIZE_F ) ) ) * CHUNK_SIZE_I;

    constexpr glm::mat4 rotation = glm::mat4( //
        1, 0, 0, 0,                           //
        0, -1, 0, 0,                          //
        0, 0, 1, 0,                           //
        0, 0, 0, 1                            //
    );

    globalGameState.multiplayer.process_events( globalGameState );
    // In capture mode the pinned view owns render_pos; send the unsneaked
    // eye height consistently with the rest of the packet.
    const float send_eye_height = run_policy().capture.enabled ? static_cast<float>( globalGameState.camera.eye_height )
                                                    : static_cast<float>( globalGameState.camera.eye_height_render );
    const uint8_t send_flags = ( globalGameState.game_mode == GameMode_Survival && !globalGameState.input.no_clip )
                                   ? NET_PLAYER_FLAG_TARGETABLE
                                   : 0;
    if ( !globalGameState.spawn_pending ) {
        globalGameState.multiplayer.update_players_position( glm::vec3( render_pos ), render_rotation, send_eye_height, send_flags );
    }

#if defined( REPGAME_WASM )
    // With pthreads enabled, terrain generation runs on background workers and
    // the main thread just drains finished chunks (like native). Without
    // pthreads, generation happens inline in dequeue(), so limit_render keeps
    // the time-budgeted loop in render_chunks from blocking the frame.
#  if defined( __EMSCRIPTEN_PTHREADS__ )
    const bool limit_render = false;
#  else
    const bool limit_render = true;
#  endif
    const bool do_render = true;
#else
    const bool do_render = true;
    const bool limit_render = false;
#endif
    if ( do_render ) {
        const long long t_render_start = repgame_now_us( );
        // Live render distance: storing it is all ChunkLoader needs — the
        // next render_chunks rescan parks/reactivates slots.
        globalGameState.world.chunkLoader.set_render_distance( globalGameState.input.render_distance );
        globalGameState.world.render( globalGameState.multiplayer, render_pos, limit_render, render_rotation );
        profiling.us_render = repgame_now_us( ) - t_render_start;
        profiling.num_drawable_chunks = globalGameState.world.get_num_drawable_chunks( );
        profiling.num_chunks_remeshed = globalGameState.world.get_num_remeshed_chunks( );
    }

    { // TEMP light diagnostic
        static int light_dbg = 0;
        // Skipped in fly mode: FLYMET owns the light_dbg_stats interval there
        // (a second caller would split the counters across two streams).
        if ( ++light_dbg % 120 == 0 && !RepGame::fly_mode( ) ) {
            const glm::ivec3 p = glm::ivec3( glm::floor( render_pos ) );
            const glm::ivec3 up = p + glm::ivec3( 0, 5, 0 );
            char dbg[ 1024 ];
            globalGameState.world.chunkLoader.light_dbg_stats( dbg, sizeof( dbg ) );
            pr_debug( "LIGHT probe p=(%d,%d,%d) id=%d sky=%d blk=%d | up id=%d sky=%d | q=%zu | %s",
                      p.x, p.y, p.z,
                      globalGameState.world.chunkLoader.light_probe_id( p ),
                      globalGameState.world.chunkLoader.light_probe( p, LIGHT_CHANNEL_SKY ),
                      globalGameState.world.chunkLoader.light_probe( p, LIGHT_CHANNEL_BLOCK ),
                      globalGameState.world.chunkLoader.light_probe_id( up ),
                      globalGameState.world.chunkLoader.light_probe( up, LIGHT_CHANNEL_SKY ),
                      globalGameState.world.chunkLoader.light_queue_sizes( ), dbg );
        }
    }

    showErrors( );

    // glm::mat4 mvp_mirror = glm::translate( mvp, glm::vec3( 0, -10, 0 ) );
    // glm::mat4 rotation = glm::diagonal4x4( glm::vec4( 1, 1, -1, 1 ) );
    // glm::mat4 rotation = glm::rotate( glm::mat4( 1.0 ), glm::radians( 90.0f ), glm::vec3( 1, 0, 1 ) );
    // glm::mat4 mvp_reflect = mvp * rotation;
    // mvp_mirror = glm::translate( mvp_mirror, glm::vec3( 0, -10, 0 ) );
    glm::ivec3 round_block = glm::ivec3( glm::round( render_pos - 0.5 ) );
    BlockState blockInHead = globalGameState.world.get_loaded_block( round_block );
    bool headInWater = blockInHead.id == WATER;

    globalGameState.blocksTexture.bind( );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_T, GL_REPEAT );

    // night_ambient (deep night) .. 1.0 (noon) — scales the sky-light
    // nibble in the chunk shader and dims sky/mobs in the object shader.
    // The night_ambient floor is moonlight: it lifts terrain, sky, mobs, and
    // fog together since they all scale by this same factor.
    ImGuiDebugVars &debugVars = imgui_overlay_get_imgui_debug_vars( );
    const float daylight = glm::max( light_daylight_factor( globalGameState.world_time ), debugVars.night_ambient );

    {
        const long long t_world_draw_start = repgame_now_us( );
        // Wrapped to an hour so shader sine functions keep float precision.
        const float time_s = static_cast<float>( fmod( repgame_now_us( ) / 1.0e6, 3600.0 ) );
        int chunk_debug_flags = 0;
        if ( run_policy().capture.enabled && run_policy().capture.view_index < static_cast<int>( run_policy().capture.views.size( ) ) ) {
            chunk_debug_flags = run_policy().capture.views[ run_policy().capture.view_index ].flags;
        }
        // With light mipmaps off the volume is non-mipmapped RG8 and the
        // shader's footprint-mip branch must never run (a missing level
        // reads b=0 and ramps distant light to 1) — force bit 32, the same
        // flag that disables it for capture A/B.
        if ( !light_mipmaps_enabled( ) ) {
            chunk_debug_flags |= 32;
        }
        globalGameState.world.set_chunk_debug_flags( chunk_debug_flags );
        if ( views == nullptr ) {
            // Flat path: the screen projection and the interpolated camera —
            // no FBO bind, no clear, no viewport change (the main loop owns
            // the default framebuffer and already cleared it).
            // Subtract the integer origin in double so the small rebased
            // offset keeps full precision, then cast to float for the view
            // matrix.
            render_view_trans = glm::translate( glm::mat4( 1.0f ), glm::vec3( -( render_pos - glm::dvec3( renderOrigin ) ) ) );
            const glm::mat4 mvp_sky = globalGameState.screen.proj * render_view_look;
            // glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * globalGameState.camera.view_look;
            const glm::mat4 mvp = mvp_sky * render_view_trans;
            const glm::mat4 flipped_look = render_view_look * rotation;
            // glm::mat4 flipped_look = rotation * globalGameState.camera.view_look;
            // glm::mat4 flipped_trans = rotation * globalGameState.camera.view_trans;
            // TODO globalGameState.camera.y causes a strange jump, perhaps the vars are updated in the wrong order.
            const float height_above_water = static_cast<float>( render_pos.y ) + ( 1.0f - WATER_HEIGHT );
            const float offset = 2.0f * height_above_water;
            const glm::mat4 flipped_trans = glm::translate( render_view_trans, glm::vec3( 0.0, offset, 0.0 ) );
            // glm::mat4 flipped_trans = globalGameState.camera.view_trans;
            const glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * flipped_look;
            const glm::mat4 mvp_reflect = globalGameState.screen.proj * flipped_look * flipped_trans;
            // inv_mvp lets the fullscreen water shader unproject pixels to the
            // rebased world frame for screen-space reflections.
            const glm::mat4 inv_mvp = glm::inverse( mvp );
            globalGameState.world.draw( globalGameState.blocksTexture, mvp, inv_mvp, mvp_reflect, mvp_sky, mvp_sky_reflect, globalGameState.input.debug_mode, !globalGameState.input.inventory_open && !globalGameState.input.menu_open, render_pos.y, headInWater,
                                        globalGameState.input.worldDrawQuality, render_pos, renderOrigin, time_s, daylight, debugVars.min_ambient, alpha );
        } else {
            // Stereo: per-eye view = body yaw ∘ tracked eye pose, eye position
            // offset from the (once-per-frame) render origin. Eye 0 issues
            // the occlusion probes; eye 1 reuses this view generation's
            // verdicts (the two eyes' boxes are close enough that a shared
            // verdict costs a little overdraw, never a hole).
            const glm::mat4 R_body = glm::rotate( glm::mat4( 1.0f ), glm::radians( -globalGameState.vr.body_yaw ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
            for ( int i = 0; i < num_views; i++ ) {
                const RenderView &view = views[ i ];
                const glm::mat4 eye_rot = R_body * glm::mat4_cast( view.orientation );
                const glm::mat4 view_look = glm::transpose( eye_rot );
                const glm::dvec3 eye_pos = render_pos + glm::dvec3( glm::vec3( R_body * glm::vec4( view.position, 0.0f ) ) );
                const glm::mat4 view_trans = glm::translate( glm::mat4( 1.0f ), glm::vec3( -( eye_pos - glm::dvec3( renderOrigin ) ) ) );
                const glm::mat4 mvp_sky = view.proj * view_look;
                const glm::mat4 mvp = mvp_sky * view_trans;
                const glm::mat4 flipped_look = view_look * rotation;
                const float height_above_water = static_cast<float>( eye_pos.y ) + ( 1.0f - WATER_HEIGHT );
                const float offset = 2.0f * height_above_water;
                const glm::mat4 flipped_trans = glm::translate( view_trans, glm::vec3( 0.0, offset, 0.0 ) );
                const glm::mat4 mvp_sky_reflect = view.proj * flipped_look;
                const glm::mat4 mvp_reflect = view.proj * flipped_look * flipped_trans;
                const glm::mat4 inv_mvp = glm::inverse( mvp );
                FrameBuffer::set_display( view.fbo );
                FrameBuffer::bind_display( );
                glViewport( 0, 0, view.width, view.height );
                glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
                globalGameState.world.chunkLoader.set_occlusion_probe_pass( i == 0 );
                globalGameState.world.draw( globalGameState.blocksTexture, mvp, inv_mvp, mvp_reflect, mvp_sky, mvp_sky_reflect, globalGameState.input.debug_mode, !globalGameState.input.inventory_open && !globalGameState.input.menu_open, static_cast<float>( eye_pos.y ), headInWater,
                                            globalGameState.input.worldDrawQuality, eye_pos, renderOrigin, time_s, daylight, debugVars.min_ambient, alpha );
            }
            globalGameState.world.chunkLoader.set_occlusion_probe_pass( true );
            FrameBuffer::set_display( 0 );
        }
        profiling.us_world_draw = repgame_now_us( ) - t_world_draw_start;
    }

    // Non-GL bookkeeping shared by flat and VR (debug HUD vars, loader stats,
    // PageUp/PageDown time skip).
    debugVars.player_pos = glm::vec3( globalGameState.camera.pos );
    debugVars.world_time = globalGameState.world_time;
    debugVars.daylight = daylight;
    globalGameState.world.chunkLoader.debug_loader_stats( &debugVars.loader );
    debugVars.debris_particles = static_cast<int>( globalGameState.world.blockDebris.count( ) );
    debugVars.primed_tnt = static_cast<int>( globalGameState.world.bodies.count_tnt( ) );
    debugVars.falling_blocks = static_cast<int>( globalGameState.world.bodies.count_falling( ) );
    debugVars.item_entities = static_cast<int>( globalGameState.world.items.count( ) );
    // PageUp/PageDown nudges: ±1 hour per keypress (±6 with Shift),
    // accumulated on the input side. Wrap into one day cycle.
    if ( globalGameState.input.time_skip_hours != 0 ) {
        globalGameState.world_time += static_cast<long>( globalGameState.input.time_skip_hours ) * ( DAY_LENGTH_TICKS / 24 );
        globalGameState.input.time_skip_hours = 0;
        globalGameState.world_time = ( ( globalGameState.world_time % DAY_LENGTH_TICKS ) + DAY_LENGTH_TICKS ) % DAY_LENGTH_TICKS;
        // When connected, push the new time to the server so every
        // player on it sees the change.
        globalGameState.multiplayer.send_world_time( globalGameState.world_time );
    }

    // Flat-only tail: HUD/held item/ImGui, screenshot logging, capture shots,
    // periodic and anomaly dumps. Skipped in VR — the compositor owns the
    // final image and none of these paths know the eye FBO.
    if ( views != nullptr ) {
        profiling.us_total_draw = repgame_now_us( ) - t_draw_start;
        return;
    }

    {
        const long long t_ui_start = repgame_now_us( );
        draw_ui_pass( render_pos, daylight );
        if ( !run_policy().capture.enabled ) {
            imgui_overlay_draw( &globalGameState.ui.imgui, globalGameState.input, globalGameState );
        }
        profiling.us_ui_draw = repgame_now_us( ) - t_ui_start;
    }
    showErrors( );

    if ( globalGameState.input.screenshot_requested ) {
        globalGameState.input.screenshot_requested = false;
        // Capture intermediate framebuffer attachments for debugging.
        std::string prefix = "screenshot_" + std::to_string( screenshot_counter++ );
        globalGameState.world.screenshot( prefix );
        // Emit the current view in REPGAME_CAPTURE format so an interesting
        // spot can be reproduced deterministically in capture mode.
        // Log the exact rendered camera (this frame's interpolated pos +
        // eased eye height) with round-trip precision so REPGAME_CAPTURE
        // reproduces this frame sub-pixel-exactly.
        pr_debug( "Capture view: REPGAME_CAPTURE=\"%.17g,%.17g,%.17g,%.9g,%.9g\"", render_pos.x,
                  render_pos.y, render_pos.z, static_cast<double>( render_angle_H ),
                  static_cast<double>( render_angle_V ) );
    }

    // Capture mode: the settled view was drawn this frame — shoot it and
    // advance to the next view (or exit after the last).
    if ( run_policy().capture.enabled && run_policy().capture.shot_pending ) {
        run_policy().capture.shot_pending = false;
        globalGameState.world.screenshot( "capture_" + std::to_string( run_policy().capture.view_index ) );
#if defined( REPGAME_WASM )
        // WASM: exiting reloads the page and loses the captures — mark done
        // and keep the camera pinned on the last (still valid) view.
        if ( run_policy().capture.view_index == static_cast<int>( run_policy().capture.views.size( ) ) - 1 ) {
            run_policy().capture.finished = true;
            pr_debug( "Capture complete" );
        } else {
            run_policy().capture.view_index++;
            run_policy().capture.ticks_in_view = 0;
            run_policy().capture.idle_streak = 0;
        }
#else
        run_policy().capture.view_index++;
        if ( run_policy().capture.view_index >= static_cast<int>( run_policy().capture.views.size( ) ) ) {
            globalGameState.input.exitGame = true;
        } else {
            run_policy().capture.ticks_in_view = 0;
            run_policy().capture.idle_streak = 0;
        }
#endif
    }

    // REPGAME_CAPTURE_PERIODIC=n: dump all FBO attachments + display every n
    // frames regardless of anomalies — samples whatever is on screen for
    // offline corruption scans (e.g. during daytime with terrain visible).
    static int periodic_frame = 0;
    if ( run_policy( ).capture_periodic > 0 && ( periodic_frame++ % run_policy( ).capture_periodic ) == 0 ) {
        std::string prefix = "periodic_" + std::to_string( screenshot_counter++ );
        globalGameState.world.screenshot( prefix );
    }

    // REPGAME_CAPTURE_ANOMALY=1: per-frame display readback, scans for
    // isolated bright RGB outliers (cheap, mild pipeline stall).
    // REPGAME_CAPTURE_ANOMALY=2: additionally blit-resolves FBO attachment 0
    // and scans its alpha for isolated outliers (heavy — halves framerate —
    // but the fog-alpha signature fires on ANY backdrop, textured terrain
    // included, where bright-speck detection is blind).
    // On a hit, dump all FBO attachments + display for forensics.
#if ( SUPPORTS_FRAME_BUFFER )
    const int capture_anomaly = run_policy( ).capture_anomaly;
    if ( capture_anomaly ) {
        GLint viewport[ 4 ];
        glGetIntegerv( GL_VIEWPORT, viewport );
        const int w = viewport[ 2 ], h = viewport[ 3 ];
        static std::vector<unsigned char> px;
        px.resize( w * h * 4 );
        // Level 1 scans the display: skip the top-left debug overlay block
        // (imgui text trips the outlier test). Level 2 scans attachment 0,
        // which contains no UI — no skip needed.
        const int skip_y = capture_anomaly >= 2 ? -1 : h - 400, skip_x = 720;
        const bool check_alpha = capture_anomaly >= 2; // display alpha is 0/255, meaningless
        if ( capture_anomaly >= 2 ) {
            static GLuint resolveFBO = 0, resolveTex = 0;
            if ( !resolveFBO ) {
                glGenFramebuffers( 1, &resolveFBO );
                glGenTextures( 1, &resolveTex );
                glBindTexture( GL_TEXTURE_2D, resolveTex );
                glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
                glBindFramebuffer( GL_FRAMEBUFFER, resolveFBO );
                glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0 );
            }
            glBindFramebuffer( GL_READ_FRAMEBUFFER, globalGameState.world.frameBuffer.id( ) );
            glReadBuffer( GL_COLOR_ATTACHMENT0 );
            glBindFramebuffer( GL_DRAW_FRAMEBUFFER, resolveFBO );
            const GLenum att = GL_COLOR_ATTACHMENT0;
            glDrawBuffers( 1, &att );
            const bool fbo_ok = glCheckFramebufferStatus( GL_READ_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE &&
                                glCheckFramebufferStatus( GL_DRAW_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
            if ( !fbo_ok ) {
                goto anomaly_done; // LOW mode: no FBO path, nothing to scan
            }
            glBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST );
            glBindFramebuffer( GL_READ_FRAMEBUFFER, resolveFBO );
            glReadBuffer( GL_COLOR_ATTACHMENT0 );
            glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data( ) );
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        } else {
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
            glReadBuffer( GL_BACK );
            glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data( ) );
        }
        static int anomaly_hits = 0, frames_since_hit = 999;
        frames_since_hit++;
        for ( int y = 1; y < h - 1 && anomaly_hits < 200 && frames_since_hit > 30; y++ ) {
            for ( int x = 1; x < w - 1; x++ ) {
                if ( y > skip_y && x < skip_x ) {
                    continue;
                }
                const unsigned char *c = &px[ ( y * w + x ) * 4 ];
                int worst_diff = 1000000, worst_alpha = 1000000;
                for ( int dy = -1; dy <= 1; dy++ ) {
                    for ( int dx = -1; dx <= 1; dx++ ) {
                        if ( dx == 0 && dy == 0 ) {
                            continue;
                        }
                        const unsigned char *n = &px[ ( ( y + dy ) * w + ( x + dx ) ) * 4 ];
                        int d = abs( c[ 0 ] - n[ 0 ] );
                        d = std::max( d, abs( c[ 1 ] - n[ 1 ] ) );
                        worst_diff = std::min( worst_diff, std::max( d, abs( c[ 2 ] - n[ 2 ] ) ) );
                        worst_alpha = std::min( worst_alpha, abs( c[ 3 ] - n[ 3 ] ) );
                    }
                }
                if ( ( c[ 0 ] + c[ 1 ] + c[ 2 ] >= 200 && worst_diff > 140 ) || ( check_alpha && worst_alpha > 80 ) ) {
                    anomaly_hits++;
                    frames_since_hit = 0;
                    pr_debug( "ANOMALY pixel at (%d,%d) rgba=(%d,%d,%d,%d) min_rgb_diff=%d min_alpha_diff=%d — dumping attachments",
                              x, y, c[ 0 ], c[ 1 ], c[ 2 ], c[ 3 ], worst_diff, worst_alpha );
                    std::string prefix = "anomaly_" + std::to_string( screenshot_counter++ );
                    globalGameState.world.screenshot( prefix );
                    goto anomaly_done;
                }
            }
        }
    anomaly_done:;
    }
#endif
    profiling.us_total_draw = repgame_now_us( ) - t_draw_start;
}
glm::mat4 RepGame::projection_from_fov( float angle_left, float angle_right, float angle_up, float angle_down, float near_z, float far_z ) {
    // Per-edge FOV angles (OpenXR convention: left/down are negative), GL
    // [-1,1] clip-space depth. Column-major: m[col][row].
    const float tl = tanf( angle_left ), tr = tanf( angle_right );
    const float tu = tanf( angle_up ), td = tanf( angle_down );
    const float w = tr - tl, h = tu - td;
    glm::mat4 m( 0.0f );
    m[ 0 ][ 0 ] = 2.0f / w;
    m[ 1 ][ 1 ] = 2.0f / h;
    m[ 2 ][ 0 ] = ( tr + tl ) / w;
    m[ 2 ][ 1 ] = ( tu + td ) / h;
    m[ 2 ][ 2 ] = -( far_z + near_z ) / ( far_z - near_z );
    m[ 2 ][ 3 ] = -1.0f;
    m[ 3 ][ 2 ] = -2.0f * far_z * near_z / ( far_z - near_z );
    return m;
}
// The UI draw sequence shared by the flat tail and the VR UI layer pass.
// Caller owns the bound FBO, viewport and blend state — this does the atlas
// rebind, depth/stencil clear, overlay + held-item draws, and the VR laser
// pointer marker.
void RepGame::draw_ui_pass( const glm::dvec3 &render_pos, float daylight ) {
    // glTexParameteri edits the texture bound on the *active* unit, so
    // re-bind the atlas first — world.draw leaves the light unit active.
    globalGameState.blocksTexture.bind( );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    showErrors( );
    // Clear depth and stencil before drawing the UI/ImGui overlays. The world
    // compositing pass uses stencil on the default framebuffer (for the
    // water-blur separation) and leaves stencil values behind; clearing it
    // here ensures a clean state for the overlays.
    glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

    if ( !run_policy().capture.enabled ) {
        const ChestContents *chest_contents = nullptr;
        if ( globalGameState.ui.chest.open ) {
            chest_contents = globalGameState.world.get_chest_contents( globalGameState.ui.chest.pos );
        }
        // Light the in-hand block from the flood-fill volume at the eye,
        // combined like the chunk/object shaders (skylight scales with
        // the day factor, torch light doesn't, min ambient lifts caves).
        const glm::vec2 held_light = globalGameState.world.chunkLoader.light_sample( glm::vec3( render_pos ) );
        const float held_block_light = glm::mix( imgui_overlay_get_imgui_debug_vars( ).min_ambient, 1.0f, glm::max( held_light.x * daylight, held_light.y ) );
        globalGameState.ui.overlay.draw( globalGameState.ui.creative, globalGameState.survival_inventory, globalGameState.hotbar, globalGameState.game_mode, globalGameState.world.renderer, globalGameState.blocksTexture, globalGameState.input, globalGameState.ui.font, globalGameState.screen.ortho_center, chest_contents ? &globalGameState.ui.chest.inventory_renderer : nullptr, chest_contents, globalGameState.ui.crafting.table_open, held_block_light );
        if ( repgame_crafting_grid_visible( globalGameState ) ) {
            const int dim = repgame_crafting_dim( globalGameState );
            InventoryRenderer &grid = repgame_crafting_grid( globalGameState );
            globalGameState.ui.overlay.draw_slot_panel( grid, globalGameState.ui.crafting.slots, dim * dim, globalGameState.world.renderer, globalGameState.blocksTexture, globalGameState.ui.font, globalGameState.screen.ortho_center );
            globalGameState.ui.overlay.draw_slot_panel( globalGameState.ui.crafting.output, &globalGameState.ui.crafting.output_slot, 1, globalGameState.world.renderer, globalGameState.blocksTexture, globalGameState.ui.font, globalGameState.screen.ortho_center );
            // "->" arrow centered in the one-stride gap between the panels.
            const float arrow_size = 24.0f;
            const float arrow_x = ( grid.getInvRight( ) + globalGameState.ui.crafting.output.getInvLeft( ) ) / 2.0f - globalGameState.ui.font.getTextWidth( "->", arrow_size ) / 2.0f;
            const float arrow_y = ( grid.getInvTop( ) + grid.getInvBottom( ) ) / 2.0f - arrow_size / 2.0f;
            const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
            globalGameState.ui.font.drawString( globalGameState.world.renderer, "->", arrow_x, arrow_y, arrow_size, white, globalGameState.screen.ortho_center );
        }
        if ( repgame_block_world( ) && globalGameState.block_selection.selectionInBounds ) {
            // Block world: name the targeted block under the crosshair.
            const BlockState target = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
            if ( target.id < LAST_BLOCK_ID ) {
                const char *bname = block_definition_get_name( target.id );
                char buf[ 160 ];
                const Block *def = block_definition_get_definition( target.id );
                const char *top = texture_name( def->textures[ FACE_TOP ] );
                const char *side = texture_name( def->textures[ FACE_FRONT ] );
                snprintf( buf, sizeof( buf ), "%s (%d)  top=%s side=%s",
                          bname ? bname : "?", (int)target.id,
                          top ? top : "?", side ? side : "?" );
                const float text_size = 22.0f;
                const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
                const float tx = -globalGameState.ui.font.getTextWidth( buf, text_size ) / 2.0f;
                // Just above the hotbar: slot 0's cell bottom plus the
                // selected-slot highlight height (1.15x cell) and a small gap.
                float slot_x, slot_y;
                int block_size, cell_size;
                float ty = -36.0f;
                if ( globalGameState.hotbar.inventory_renderer.getSlotPosition( 0, slot_x, slot_y ) ) {
                    globalGameState.hotbar.inventory_renderer.getBlockMetrics( block_size, cell_size );
                    ty = slot_y + cell_size * 1.15f + 8.0f;
                }
                globalGameState.ui.font.drawString( globalGameState.world.renderer, buf, tx, ty, text_size, white, globalGameState.screen.ortho_center );
            }
        }
    }
    if ( !run_policy().capture.enabled && globalGameState.ui.holding_slot ) {
        int block_size, cell_size;
        globalGameState.survival_inventory.inventory_renderer.getBlockMetrics( block_size, cell_size );
        globalGameState.ui.overlay.draw_held_inventory_item( globalGameState.ui.held_slot, globalGameState.ui.holding_slot, globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y, block_size, cell_size, globalGameState.world.renderer, globalGameState.blocksTexture, globalGameState.ui.font, globalGameState.screen.ortho_center );
    }
    // The pause menu draws last, on top of the hotbar/inventory/held item.
    if ( !run_policy().capture.enabled && globalGameState.input.menu_open ) {
        globalGameState.ui.pause.draw( globalGameState.world.renderer, globalGameState.ui.overlay.get_shader( ), globalGameState.blocksTexture, globalGameState.ui.font, globalGameState.screen.ortho_center, globalGameState );
    }
    // Laser-pointer marker where the aim ray hits the world-locked UI quad.
    if ( globalGameState.vr.active && globalGameState.input.inventory_open && globalGameState.vr.pointer_on_ui ) {
        const float marker_size = 32.0f;
        const float marker_x = static_cast<float>( globalGameState.input.mouse.absPosition.x ) - globalGameState.screen.width / 2.0f - globalGameState.ui.font.getTextWidth( "+", marker_size ) / 2.0f;
        const float marker_y = globalGameState.screen.height / 2.0f - static_cast<float>( globalGameState.input.mouse.absPosition.y );
        const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
        globalGameState.ui.font.drawString( globalGameState.world.renderer, "+", marker_x, marker_y, marker_size, white, globalGameState.screen.ortho_center );
    }
}
