#pragma once

// OpenXR session wrapper for the Linux/Steam Frame build. Only compiled
// under REPGAME_OPENXR; a pimpl keeps the OpenXR C headers out of the shared
// game code (RenderView/quat are the only types leaking through, both from
// common/RepGame.hpp).
#if defined( REPGAME_OPENXR )

#include "common/RepGame.hpp"

class OpenXRSession {
    struct Impl;
    Impl *impl = nullptr;

  public:
    OpenXRSession( );
    ~OpenXRSession( );

    bool init( );           // instance + system; false => logged reason, VR unavailable
    bool create_session( ); // needs the current SDL EGL context; spaces, swapchains, FBOs
    void poll_events( bool &exit_requested );
    bool is_running( ) const;
    // xrWaitFrame + xrBeginFrame; if should_render also locate views/head and
    // acquire+wait eye + UI images, filling views[0..1].
    bool begin_frame( RenderView views[ 2 ], bool &should_render, glm::quat &head, glm::vec3 &head_pos );
    void end_frame( ); // release acquired images; xrEndFrame (projection [+ quad] layers if rendered, else 0 layers)
    // FBO of the UI swapchain image acquired by begin_frame — valid only
    // until end_frame. False when no UI image was acquired this frame.
    bool ui_target( unsigned int &fbo, int &w, int &h ) const;
    // Queue the UI quad layer for the next end_frame (game decides pose).
    void set_ui_layer( const VrUiLayer &layer );
    // xrSyncActions + per-action state into `out` (defaults when the session
    // isn't focused/running); locates the aim space for the frame's
    // predicted display time.
    void sync_input( VrControllerInput &out );
    void haptic_pulse( float amplitude, float seconds );
    void request_exit( );
    void destroy( );
    int eye_width( ) const;
    int eye_height( ) const;
};

#endif
