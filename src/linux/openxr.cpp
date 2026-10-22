// OpenXR stereo rendering backend for the Linux build (desktop + Steam
// Frame). Entire file is REPGAME_OPENXR-gated: the makefiles compile every
// src/linux/*.cpp into every Linux-flavored target.
#if defined( REPGAME_OPENXR )

#include "linux/RepGameLinux.hpp"
#include <EGL/egl.h>
#include "common/RepGame.hpp"

// Platform define selection must precede openxr_platform.h.
#define XR_USE_PLATFORM_EGL
#if defined( REPGAME_GLES )
#define XR_USE_GRAPHICS_API_OPENGL_ES
#else
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <vector>
#include <cstring>

#include "linux/openxr.hpp"

namespace {

const char *xr_result_str( XrInstance instance, XrResult result ) {
    static char buf[ XR_MAX_RESULT_STRING_SIZE ];
    if ( instance != XR_NULL_HANDLE ) {
        xrResultToString( instance, result, buf );
    } else {
        snprintf( buf, sizeof( buf ), "%d", static_cast<int>( result ) );
    }
    return buf;
}

#define XR_LOG_CALL( instance, expr )                                                                                                                                                                                                          \
    ( {                                                                                                                                                                                                                                        \
        const XrResult _rc = ( expr );                                                                                                                                                                                                         \
        if ( XR_FAILED( _rc ) ) {                                                                                                                                                                                                              \
            pr_debug( "%s failed: %s", #expr, xr_result_str( ( instance ), _rc ) );                                                                                                                                                            \
        }                                                                                                                                                                                                                                      \
        _rc;                                                                                                                                                                                                                                   \
    } )

// OpenXR extension functions are resolved through xrGetInstanceProcAddr.
#define XR_GET_PROC( instance, name )                                                                                                                                                                                                          \
    PFN_##name name = nullptr;                                                                                                                                                                                                                 \
    xrGetInstanceProcAddr( instance, #name, reinterpret_cast<PFN_xrVoidFunction *>( &name ) );                                                                                                                                                 \
    if ( !name ) {                                                                                                                                                                                                                             \
        pr_debug( "xrGetInstanceProcAddr(%s) failed", #name );                                                                                                                                                                                 \
    }

bool has_instance_extension( const std::vector<XrExtensionProperties> &props, const char *name ) {
    for ( const XrExtensionProperties &p : props ) {
        if ( strcmp( p.extensionName, name ) == 0 ) {
            return true;
        }
    }
    return false;
}

} // namespace

struct OpenXRSession::Impl {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    bool session_active = false;
    bool exit_requested = false;
    XrSpace local_space = XR_NULL_HANDLE;
    XrSpace view_space = XR_NULL_HANDLE;
    XrEnvironmentBlendMode blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    int eye_w = 0;
    int eye_h = 0;

    XrSwapchain swapchains[ 2 ] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    int64_t swapchain_format = 0;
    // One FBO per swapchain image; one depth renderbuffer per eye, shared by
    // that eye's FBOs (only one image is rendered at a time).
    std::vector<unsigned int> fbos[ 2 ];
    unsigned int depth_rbo[ 2 ] = { 0, 0 };
    int acquired_image[ 2 ] = { -1, -1 };

    // Third swapchain carries the UI quad layer (inventory/hotbar rendered
    // as a transparent premultiplied layer). One FBO per image, each with
    // its own depth/stencil renderbuffer — the UI pass clears depth and
    // must not share it across acquired images.
    XrSwapchain ui_swapchain = XR_NULL_HANDLE;
    std::vector<unsigned int> ui_fbos;
    std::vector<unsigned int> ui_depth_rbos;
    int ui_acquired_image = -1;
    // Quad layer queued by set_ui_layer for the next end_frame.
    bool ui_layer_set = false;
    VrUiLayer ui_layer{ };

    XrTime predicted_display_time = 0;
    XrView located_views[ 2 ] = { };
    glm::quat last_head = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
    glm::vec3 last_head_pos = glm::vec3( 0.0f );
    bool last_rendered = false;

    // Actions. frame_controller is set when the runtime offers
    // XR_VALVE_frame_controller_interaction (enables the Frame controller
    // interaction profile).
    bool frame_controller_ext = false;
    XrActionSet action_set = XR_NULL_HANDLE;
    XrAction act_move = XR_NULL_HANDLE, act_turn = XR_NULL_HANDLE;
    XrAction act_jump = XR_NULL_HANDLE, act_sneak = XR_NULL_HANDLE, act_sprint = XR_NULL_HANDLE;
    XrAction act_break_block = XR_NULL_HANDLE, act_place_block = XR_NULL_HANDLE, act_pick_block = XR_NULL_HANDLE;
    XrAction act_hotbar_next = XR_NULL_HANDLE, act_hotbar_prev = XR_NULL_HANDLE;
    XrAction act_inventory = XR_NULL_HANDLE, act_toggle_fly = XR_NULL_HANDLE;
    XrAction act_aim = XR_NULL_HANDLE, act_haptic = XR_NULL_HANDLE;
    XrSpace aim_space = XR_NULL_HANDLE;
};

OpenXRSession::OpenXRSession( ) : impl( new Impl( ) ) {
}
OpenXRSession::~OpenXRSession( ) {
    destroy( );
    delete impl;
    impl = nullptr;
}

bool OpenXRSession::init( ) {
    // Discover which graphics-binding extension the runtime offers: a GLES
    // build prefers XR_KHR_opengl_es_enable, desktop GL prefers
    // XR_KHR_opengl_enable — with the other as fallback either way.
    uint32_t ext_count = 0;
    xrEnumerateInstanceExtensionProperties( nullptr, 0, &ext_count, nullptr );
    std::vector<XrExtensionProperties> ext_props( ext_count );
    for ( XrExtensionProperties &p : ext_props ) {
        p.type = XR_TYPE_EXTENSION_PROPERTIES;
    }
    xrEnumerateInstanceExtensionProperties( nullptr, ext_count, &ext_count, ext_props.data( ) );

#if defined( REPGAME_GLES )
    const char *primary_graphics_ext = "XR_KHR_opengl_es_enable";
    const char *fallback_graphics_ext = "XR_KHR_opengl_enable";
#else
    const char *primary_graphics_ext = "XR_KHR_opengl_enable";
    const char *fallback_graphics_ext = "XR_KHR_opengl_es_enable";
#endif
    const char *graphics_ext = nullptr;
    if ( has_instance_extension( ext_props, primary_graphics_ext ) ) {
        graphics_ext = primary_graphics_ext;
    } else if ( has_instance_extension( ext_props, fallback_graphics_ext ) ) {
        graphics_ext = fallback_graphics_ext;
    }
    if ( !graphics_ext ) {
        pr_debug( "OpenXR: no OpenGL graphics extension offered by any installed runtime" );
        return false;
    }
    if ( !has_instance_extension( ext_props, XR_MNDX_EGL_ENABLE_EXTENSION_NAME ) ) {
        pr_debug( "OpenXR: required extension %s not offered (need an EGL-capable runtime)", XR_MNDX_EGL_ENABLE_EXTENSION_NAME );
        return false;
    }
    const char *frame_controller_ext = "XR_VALVE_frame_controller_interaction";
    impl->frame_controller_ext = has_instance_extension( ext_props, frame_controller_ext );
    const char *enabled_extensions[] = { XR_MNDX_EGL_ENABLE_EXTENSION_NAME, graphics_ext, frame_controller_ext };

    XrInstanceCreateInfo instance_info{ };
    instance_info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    strcpy( instance_info.applicationInfo.applicationName, "RepGame" );
    instance_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    instance_info.enabledExtensionCount = impl->frame_controller_ext ? 3 : 2;
    instance_info.enabledExtensionNames = enabled_extensions;
    const XrResult rc = xrCreateInstance( &instance_info, &impl->instance );
    if ( XR_FAILED( rc ) ) {
        // No runtime installed is the common failure here.
        pr_debug( "OpenXR: xrCreateInstance failed (%d) — no usable runtime?", static_cast<int>( rc ) );
        return false;
    }

    XrSystemGetInfo system_info{ };
    system_info.type = XR_TYPE_SYSTEM_GET_INFO;
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrGetSystem( impl->instance, &system_info, &impl->system ) ) ) ) {
        pr_debug( "OpenXR: no HMD (XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) reported" );
        return false;
    }

    // Eye render size: max of the two recommended rects, scaled by
    // REPGAME_VR_RENDER_SCALE (default 1.0) and clamped to the max rect.
    XrViewConfigurationView eye_configs[ 2 ] = { };
    eye_configs[ 0 ].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    eye_configs[ 1 ].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    uint32_t num_views = 0;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrEnumerateViewConfigurationViews( impl->instance, impl->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &num_views, eye_configs ) ) ) || num_views < 2 ) {
        return false;
    }
    float scale = 1.0f;
    if ( const char *env = getenv( "REPGAME_VR_RENDER_SCALE" ) ) {
        scale = static_cast<float>( atof( env ) );
        if ( scale <= 0.0f ) {
            scale = 1.0f;
        }
    }
    const uint32_t rec_w = std::max( eye_configs[ 0 ].recommendedImageRectWidth, eye_configs[ 1 ].recommendedImageRectWidth );
    const uint32_t rec_h = std::max( eye_configs[ 0 ].recommendedImageRectHeight, eye_configs[ 1 ].recommendedImageRectHeight );
    const uint32_t max_w = std::max( eye_configs[ 0 ].maxImageRectWidth, eye_configs[ 1 ].maxImageRectWidth );
    const uint32_t max_h = std::max( eye_configs[ 0 ].maxImageRectHeight, eye_configs[ 1 ].maxImageRectHeight );
    impl->eye_w = std::min<uint32_t>( max_w, static_cast<uint32_t>( rec_w * scale ) );
    impl->eye_h = std::min<uint32_t>( max_h, static_cast<uint32_t>( rec_h * scale ) );
    pr_debug( "OpenXR: eye %dx%d (recommended %ux%u, scale %.2f)", impl->eye_w, impl->eye_h, rec_w, rec_h, scale );
    return true;
}

bool OpenXRSession::create_session( ) {
    // Graphics requirements must be queried before xrCreateSession.
#if defined( REPGAME_GLES )
    XR_GET_PROC( impl->instance, xrGetOpenGLESGraphicsRequirementsKHR );
    if ( xrGetOpenGLESGraphicsRequirementsKHR ) {
        XrGraphicsRequirementsOpenGLESKHR reqs{ };
        reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
        if ( XR_SUCCEEDED( XR_LOG_CALL( impl->instance, xrGetOpenGLESGraphicsRequirementsKHR( impl->instance, impl->system, &reqs ) ) ) ) {
            pr_debug( "OpenXR: GLES requirements min %u.%u.%u max %u.%u.%u", static_cast<unsigned>( XR_VERSION_MAJOR( reqs.minApiVersionSupported ) ), static_cast<unsigned>( XR_VERSION_MINOR( reqs.minApiVersionSupported ) ),
                      static_cast<unsigned>( XR_VERSION_PATCH( reqs.minApiVersionSupported ) ), static_cast<unsigned>( XR_VERSION_MAJOR( reqs.maxApiVersionSupported ) ),
                      static_cast<unsigned>( XR_VERSION_MINOR( reqs.maxApiVersionSupported ) ), static_cast<unsigned>( XR_VERSION_PATCH( reqs.maxApiVersionSupported ) ) );
        }
    }
#else
    XR_GET_PROC( impl->instance, xrGetOpenGLGraphicsRequirementsKHR );
    if ( xrGetOpenGLGraphicsRequirementsKHR ) {
        XrGraphicsRequirementsOpenGLKHR reqs{ };
        reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
        if ( XR_SUCCEEDED( XR_LOG_CALL( impl->instance, xrGetOpenGLGraphicsRequirementsKHR( impl->instance, impl->system, &reqs ) ) ) ) {
            pr_debug( "OpenXR: GL requirements min %u.%u.%u max %u.%u.%u", static_cast<unsigned>( XR_VERSION_MAJOR( reqs.minApiVersionSupported ) ), static_cast<unsigned>( XR_VERSION_MINOR( reqs.minApiVersionSupported ) ),
                      static_cast<unsigned>( XR_VERSION_PATCH( reqs.minApiVersionSupported ) ), static_cast<unsigned>( XR_VERSION_MAJOR( reqs.maxApiVersionSupported ) ),
                      static_cast<unsigned>( XR_VERSION_MINOR( reqs.maxApiVersionSupported ) ), static_cast<unsigned>( XR_VERSION_PATCH( reqs.maxApiVersionSupported ) ) );
        }
    }
#endif

    // Bind the SDL window's EGL context to the session.
    const EGLDisplay egl_display = SDL_EGL_GetCurrentDisplay( );
    if ( egl_display == EGL_NO_DISPLAY || egl_display == nullptr ) {
        pr_debug( "OpenXR: SDL GL context is not EGL — cannot bind session" );
        return false;
    }
    const EGLConfig egl_config = SDL_EGL_GetCurrentConfig( );
    const EGLContext egl_context = static_cast<EGLContext>( SDL_GL_GetCurrentContext( ) );
    XrGraphicsBindingEGLMNDX binding{ };
    binding.type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX;
    binding.getProcAddress = reinterpret_cast<PFN_xrEglGetProcAddressMNDX>( eglGetProcAddress );
    binding.display = egl_display;
    binding.config = egl_config;
    binding.context = egl_context;

    XrSessionCreateInfo session_info{ };
    session_info.type = XR_TYPE_SESSION_CREATE_INFO;
    session_info.next = &binding;
    session_info.systemId = impl->system;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrCreateSession( impl->instance, &session_info, &impl->session ) ) ) ) {
        return false;
    }

    // Reference spaces: LOCAL for view poses + the projection layer, VIEW
    // located against LOCAL each frame for the head orientation.
    XrReferenceSpaceCreateInfo space_info{ };
    space_info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    space_info.poseInReferenceSpace.orientation.w = 1.0f;
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrCreateReferenceSpace( impl->session, &space_info, &impl->local_space ) ) ) ) {
        return false;
    }
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrCreateReferenceSpace( impl->session, &space_info, &impl->view_space ) ) ) ) {
        return false;
    }

    // Environment blend: OPAQUE when offered, else the first advertised.
    uint32_t blend_count = 0;
    xrEnumerateEnvironmentBlendModes( impl->instance, impl->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blend_count, nullptr );
    std::vector<XrEnvironmentBlendMode> modes( blend_count );
    xrEnumerateEnvironmentBlendModes( impl->instance, impl->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blend_count, &blend_count, modes.data( ) );
    if ( blend_count > 0 ) {
        impl->blend_mode = modes[ 0 ];
        for ( XrEnvironmentBlendMode m : modes ) {
            if ( m == XR_ENVIRONMENT_BLEND_MODE_OPAQUE ) {
                impl->blend_mode = m;
            }
        }
    }

    // Swapchain format: prefer linear RGBA8 (the game writes sRGB-encoded
    // values itself); SRGB8_ALPHA8 otherwise — on GLES an SRGB swapchain
    // additionally applies HW sRGB conversion unless the runtime's context
    // exposes GL_EXT_sRGB_write_control, so disable it for the frame.
    uint32_t format_count = 0;
    xrEnumerateSwapchainFormats( impl->session, 0, &format_count, nullptr );
    std::vector<int64_t> formats( format_count );
    xrEnumerateSwapchainFormats( impl->session, format_count, &format_count, formats.data( ) );
    impl->swapchain_format = format_count ? formats[ 0 ] : 0;
    bool srgb_swapchain = false;
    for ( int64_t f : formats ) {
        if ( f == 0x8058 /* GL_RGBA8 */ ) {
            impl->swapchain_format = f;
            srgb_swapchain = false;
            break;
        }
        if ( f == 0x8C43 /* GL_SRGB8_ALPHA8 */ && impl->swapchain_format == formats[ 0 ] ) {
            impl->swapchain_format = f;
            srgb_swapchain = true;
        }
    }
    if ( srgb_swapchain ) {
        pr_debug( "OpenXR: no GL_RGBA8 swapchain format; using GL_SRGB8_ALPHA8 (game writes sRGB-encoded values)" );
#if defined( REPGAME_GLES )
        const char *exts = reinterpret_cast<const char *>( glGetString( GL_EXTENSIONS ) );
        if ( exts && strstr( exts, "GL_EXT_sRGB_write_control" ) ) {
            glDisable( 0x8DB9 /* GL_FRAMEBUFFER_SRGB_EXT */ );
        }
#endif
    }
    pr_debug( "OpenXR: swapchain format 0x%llx", static_cast<long long>( impl->swapchain_format ) );

    for ( int eye = 0; eye < 2; eye++ ) {
        XrSwapchainCreateInfo sc_info{ };
        sc_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        sc_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc_info.format = impl->swapchain_format;
        sc_info.sampleCount = 1;
        sc_info.width = impl->eye_w;
        sc_info.height = impl->eye_h;
        sc_info.faceCount = 1;
        sc_info.arraySize = 1;
        sc_info.mipCount = 1;
        if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrCreateSwapchain( impl->session, &sc_info, &impl->swapchains[ eye ] ) ) ) ) {
            return false;
        }
        uint32_t image_count = 0;
        xrEnumerateSwapchainImages( impl->swapchains[ eye ], 0, &image_count, nullptr );
        if ( image_count == 0 ) {
            pr_debug( "OpenXR: swapchain eye %d has no images", eye );
            return false;
        }
#if defined( REPGAME_GLES )
        std::vector<XrSwapchainImageOpenGLESKHR> images( image_count );
        for ( XrSwapchainImageOpenGLESKHR &img : images ) {
            img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        }
#else
        std::vector<XrSwapchainImageOpenGLKHR> images( image_count );
        for ( XrSwapchainImageOpenGLKHR &img : images ) {
            img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        }
#endif
        if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrEnumerateSwapchainImages( impl->swapchains[ eye ], image_count, &image_count, reinterpret_cast<XrSwapchainImageBaseHeader *>( images.data( ) ) ) ) ) ) {
            return false;
        }
        // Shared depth/stencil renderbuffer for this eye's FBOs.
        glGenRenderbuffers( 1, &impl->depth_rbo[ eye ] );
        glBindRenderbuffer( GL_RENDERBUFFER, impl->depth_rbo[ eye ] );
        glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, impl->eye_w, impl->eye_h );
        impl->fbos[ eye ].resize( image_count );
        for ( uint32_t i = 0; i < image_count; i++ ) {
            glGenFramebuffers( 1, &impl->fbos[ eye ][ i ] );
            glBindFramebuffer( GL_FRAMEBUFFER, impl->fbos[ eye ][ i ] );
            glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[ i ].image, 0 );
            glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, impl->depth_rbo[ eye ] );
            const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
            if ( status != GL_FRAMEBUFFER_COMPLETE ) {
                pr_debug( "OpenXR: eye %d image %u FBO incomplete: 0x%x", eye, i, status );
                return false;
            }
        }
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    }

    // Third swapchain for the UI quad layer: same extent/format/usage as
    // the eyes, but one depth/stencil renderbuffer per image.
    {
        XrSwapchainCreateInfo sc_info{ };
        sc_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        sc_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc_info.format = impl->swapchain_format;
        sc_info.sampleCount = 1;
        sc_info.width = impl->eye_w;
        sc_info.height = impl->eye_h;
        sc_info.faceCount = 1;
        sc_info.arraySize = 1;
        sc_info.mipCount = 1;
        if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrCreateSwapchain( impl->session, &sc_info, &impl->ui_swapchain ) ) ) ) {
            return false;
        }
        uint32_t image_count = 0;
        xrEnumerateSwapchainImages( impl->ui_swapchain, 0, &image_count, nullptr );
        if ( image_count == 0 ) {
            pr_debug( "OpenXR: ui swapchain has no images" );
            return false;
        }
#if defined( REPGAME_GLES )
        std::vector<XrSwapchainImageOpenGLESKHR> images( image_count );
        for ( XrSwapchainImageOpenGLESKHR &img : images ) {
            img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        }
#else
        std::vector<XrSwapchainImageOpenGLKHR> images( image_count );
        for ( XrSwapchainImageOpenGLKHR &img : images ) {
            img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        }
#endif
        if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrEnumerateSwapchainImages( impl->ui_swapchain, image_count, &image_count, reinterpret_cast<XrSwapchainImageBaseHeader *>( images.data( ) ) ) ) ) ) {
            return false;
        }
        impl->ui_fbos.resize( image_count );
        impl->ui_depth_rbos.resize( image_count );
        for ( uint32_t i = 0; i < image_count; i++ ) {
            glGenRenderbuffers( 1, &impl->ui_depth_rbos[ i ] );
            glBindRenderbuffer( GL_RENDERBUFFER, impl->ui_depth_rbos[ i ] );
            glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, impl->eye_w, impl->eye_h );
            glGenFramebuffers( 1, &impl->ui_fbos[ i ] );
            glBindFramebuffer( GL_FRAMEBUFFER, impl->ui_fbos[ i ] );
            glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[ i ].image, 0 );
            glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, impl->ui_depth_rbos[ i ] );
            const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
            if ( status != GL_FRAMEBUFFER_COMPLETE ) {
                pr_debug( "OpenXR: ui image %u FBO incomplete: 0x%x", i, status );
                return false;
            }
        }
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    }

    // --- Actions ---
    XrActionSetCreateInfo set_info{ };
    set_info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    strcpy( set_info.actionSetName, "gameplay" );
    strcpy( set_info.localizedActionSetName, "Gameplay" );
    set_info.priority = 0;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrCreateActionSet( impl->instance, &set_info, &impl->action_set ) ) ) ) {
        return false;
    }
    const auto mk_action = [ this ]( XrActionType type, const char *name, const char *localized, XrAction &out ) {
        XrActionCreateInfo info{ };
        info.type = XR_TYPE_ACTION_CREATE_INFO;
        info.actionType = type;
        strcpy( info.actionName, name );
        strcpy( info.localizedActionName, localized );
        XR_LOG_CALL( impl->instance, xrCreateAction( impl->action_set, &info, &out ) );
    };
    mk_action( XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move", impl->act_move );
    mk_action( XR_ACTION_TYPE_VECTOR2F_INPUT, "turn", "Turn", impl->act_turn );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "jump", "Jump", impl->act_jump );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "sneak", "Sneak", impl->act_sneak );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "sprint", "Sprint", impl->act_sprint );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "break_block", "Break Block", impl->act_break_block );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "place_block", "Place Block", impl->act_place_block );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "pick_block", "Pick Block", impl->act_pick_block );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "hotbar_next", "Hotbar Next", impl->act_hotbar_next );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "hotbar_prev", "Hotbar Prev", impl->act_hotbar_prev );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "inventory", "Inventory", impl->act_inventory );
    mk_action( XR_ACTION_TYPE_BOOLEAN_INPUT, "toggle_fly", "Toggle Fly", impl->act_toggle_fly );
    mk_action( XR_ACTION_TYPE_POSE_INPUT, "aim", "Aim", impl->act_aim );
    mk_action( XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic", impl->act_haptic );

    // Suggested bindings per interaction profile. A rejected suggestion is
    // logged and skipped — the runtime may not know every profile.
    const auto suggest = [ this ]( const char *profile, const std::vector<std::pair<XrAction, const char *>> &map ) {
        XrPath profile_path = XR_NULL_PATH;
        if ( XR_FAILED( xrStringToPath( impl->instance, profile, &profile_path ) ) ) {
            return;
        }
        std::vector<XrActionSuggestedBinding> bindings( map.size( ) );
        for ( size_t i = 0; i < map.size( ); i++ ) {
            if ( XR_FAILED( xrStringToPath( impl->instance, map[ i ].second, &bindings[ i ].binding ) ) ) {
                return;
            }
            bindings[ i ].action = map[ i ].first;
        }
        XrInteractionProfileSuggestedBinding sug{ };
        sug.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        sug.interactionProfile = profile_path;
        sug.countSuggestedBindings = static_cast<uint32_t>( bindings.size( ) );
        sug.suggestedBindings = bindings.data( );
        if ( XR_FAILED( xrSuggestInteractionProfileBindings( impl->instance, &sug ) ) ) {
            pr_debug( "OpenXR: binding suggestion for %s rejected (skipped)", profile );
        }
    };
    const std::vector<std::pair<XrAction, const char *>> frame_bindings = {
        { impl->act_move, "/user/hand/left/input/thumbstick" },
        { impl->act_turn, "/user/hand/right/input/thumbstick" },
        { impl->act_jump, "/user/hand/right/input/a/click" },
        { impl->act_sneak, "/user/hand/left/input/thumbstick/click" },
        { impl->act_sprint, "/user/hand/left/input/squeeze/value" },
        { impl->act_break_block, "/user/hand/right/input/trigger/value" },
        { impl->act_place_block, "/user/hand/left/input/trigger/value" },
        { impl->act_pick_block, "/user/hand/right/input/squeeze/value" },
        { impl->act_hotbar_next, "/user/hand/right/input/b/click" },
        { impl->act_hotbar_prev, "/user/hand/right/input/x/click" },
        { impl->act_inventory, "/user/hand/right/input/y/click" },
        { impl->act_toggle_fly, "/user/hand/left/input/dpad_up/click" },
        { impl->act_aim, "/user/hand/right/input/aim/pose" },
        { impl->act_haptic, "/user/hand/right/output/haptic" },
    };
    if ( impl->frame_controller_ext ) {
        suggest( "/interaction_profiles/valve/frame_controller_valve", frame_bindings );
    }
    {
        std::vector<std::pair<XrAction, const char *>> touch_bindings = frame_bindings;
        // Touch has no dpad: hotbar/inventory on left face buttons,
        // toggle_fly on right stick click.
        for ( auto &b : touch_bindings ) {
            if ( b.first == impl->act_hotbar_prev ) {
                b.second = "/user/hand/left/input/x/click";
            } else if ( b.first == impl->act_inventory ) {
                b.second = "/user/hand/left/input/y/click";
            } else if ( b.first == impl->act_toggle_fly ) {
                b.second = "/user/hand/right/input/thumbstick/click";
            }
        }
        suggest( "/interaction_profiles/oculus/touch_controller", touch_bindings );
    }
    // Monado's simulated controllers (useful for testing).
    suggest( "/interaction_profiles/khr/simple_controller", { { impl->act_break_block, "/user/hand/right/input/select/click" },
                                                              { impl->act_place_block, "/user/hand/left/input/select/click" },
                                                              { impl->act_inventory, "/user/hand/right/input/menu/click" },
                                                              { impl->act_aim, "/user/hand/right/input/aim/pose" },
                                                              { impl->act_haptic, "/user/hand/right/output/haptic" } } );

    XrSessionActionSetsAttachInfo attach_info{ };
    attach_info.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attach_info.countActionSets = 1;
    attach_info.actionSets = &impl->action_set;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrAttachSessionActionSets( impl->session, &attach_info ) ) ) ) {
        return false;
    }
    XrActionSpaceCreateInfo aim_space_info{ };
    aim_space_info.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
    aim_space_info.action = impl->act_aim;
    aim_space_info.poseInActionSpace.orientation.w = 1.0f;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrCreateActionSpace( impl->session, &aim_space_info, &impl->aim_space ) ) ) ) {
        return false;
    }
    return true;
}

void OpenXRSession::poll_events( bool &exit_requested ) {
    XrEventDataBuffer event{ };
    event.type = XR_TYPE_EVENT_DATA_BUFFER;
    while ( xrPollEvent( impl->instance, &event ) == XR_SUCCESS ) {
        if ( event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED ) {
            const XrEventDataSessionStateChanged &ev = *reinterpret_cast<XrEventDataSessionStateChanged *>( &event );
            impl->session_state = ev.state;
            pr_debug( "OpenXR: session state -> %d", static_cast<int>( ev.state ) );
            switch ( ev.state ) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo begin_info{ };
                    begin_info.type = XR_TYPE_SESSION_BEGIN_INFO;
                    begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if ( XR_SUCCEEDED( XR_LOG_CALL( impl->instance, xrBeginSession( impl->session, &begin_info ) ) ) ) {
                        impl->session_active = true;
                    }
                    break;
                }
                case XR_SESSION_STATE_STOPPING:
                    XR_LOG_CALL( impl->instance, xrEndSession( impl->session ) );
                    impl->session_active = false;
                    break;
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING:
                    impl->session_active = false;
                    impl->exit_requested = true;
                    exit_requested = true;
                    break;
                default:
                    break;
            }
        } else if ( event.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED ) {
            XrPath profile = XR_NULL_PATH;
            XrInteractionProfileState prof_state{ };
            prof_state.type = XR_TYPE_INTERACTION_PROFILE_STATE;
            XrPath right_hand = XR_NULL_PATH;
            xrStringToPath( impl->instance, "/user/hand/right", &right_hand );
            if ( XR_SUCCEEDED( xrGetCurrentInteractionProfile( impl->session, right_hand, &prof_state ) ) && prof_state.interactionProfile != XR_NULL_PATH ) {
                char path_buf[ XR_MAX_PATH_LENGTH ];
                uint32_t len = 0;
                xrPathToString( impl->instance, prof_state.interactionProfile, sizeof( path_buf ), &len, path_buf );
                pr_debug( "OpenXR: /user/hand/right interaction profile -> %s", path_buf );
            } else {
                pr_debug( "OpenXR: /user/hand/right interaction profile -> none" );
            }
        } else if ( event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING ) {
            impl->session_active = false;
            impl->exit_requested = true;
            exit_requested = true;
        }
        event = XrEventDataBuffer{ };
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

bool OpenXRSession::is_running( ) const {
    return impl->session_active && !impl->exit_requested;
}

bool OpenXRSession::begin_frame( RenderView views[ 2 ], bool &should_render, glm::quat &head, glm::vec3 &head_pos ) {
    should_render = false;
    if ( !is_running( ) ) {
        return false;
    }
    XrFrameWaitInfo wait_info{ };
    wait_info.type = XR_TYPE_FRAME_WAIT_INFO;
    XrFrameState frame_state{ };
    frame_state.type = XR_TYPE_FRAME_STATE;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrWaitFrame( impl->session, &wait_info, &frame_state ) ) ) ) {
        return false;
    }
    impl->predicted_display_time = frame_state.predictedDisplayTime;
    XrFrameBeginInfo begin_info{ };
    begin_info.type = XR_TYPE_FRAME_BEGIN_INFO;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrBeginFrame( impl->session, &begin_info ) ) ) ) {
        return false;
    }
    should_render = frame_state.shouldRender == XR_TRUE;
    impl->last_rendered = should_render;

    // Head pose: locate VIEW in LOCAL at the predicted display time; keep the
    // last valid orientation when the runtime reports an invalid pose (only
    // orientation matters for the head quat — 3DoF/tracking-loss still
    // reports it).
    XrSpaceLocation head_location{ };
    head_location.type = XR_TYPE_SPACE_LOCATION;
    if ( XR_SUCCEEDED( xrLocateSpace( impl->view_space, impl->local_space, impl->predicted_display_time, &head_location ) ) ) {
        if ( head_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) {
            const XrQuaternionf &q = head_location.pose.orientation;
            impl->last_head = glm::quat( q.w, q.x, q.y, q.z );
        }
        if ( head_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) {
            const XrVector3f &p = head_location.pose.position;
            impl->last_head_pos = glm::vec3( p.x, p.y, p.z );
        }
    }
    head = impl->last_head;
    head_pos = impl->last_head_pos;
    if ( !should_render ) {
        return true;
    }

    // Per-eye poses + FOVs at the same predicted display time.
    XrViewLocateInfo locate_info{ };
    locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
    locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate_info.displayTime = impl->predicted_display_time;
    locate_info.space = impl->local_space;
    XrViewState view_state{ };
    view_state.type = XR_TYPE_VIEW_STATE;
    uint32_t num_views = 2;
    impl->located_views[ 0 ] = XrView{ };
    impl->located_views[ 0 ].type = XR_TYPE_VIEW;
    impl->located_views[ 1 ] = XrView{ };
    impl->located_views[ 1 ].type = XR_TYPE_VIEW;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrLocateViews( impl->session, &locate_info, &view_state, 2, &num_views, impl->located_views ) ) ) || num_views < 2 ) {
        should_render = false;
        impl->last_rendered = false;
        return true;
    }

    for ( int eye = 0; eye < 2; eye++ ) {
        const XrView &v = impl->located_views[ eye ];
        views[ eye ].proj = RepGame::projection_from_fov( v.fov.angleLeft, v.fov.angleRight, v.fov.angleUp, v.fov.angleDown, 0.1f, CAMERA_FAR_PLANE );
        views[ eye ].orientation = glm::quat( v.pose.orientation.w, v.pose.orientation.x, v.pose.orientation.y, v.pose.orientation.z );
        views[ eye ].position = glm::vec3( v.pose.position.x, v.pose.position.y, v.pose.position.z );
        views[ eye ].width = impl->eye_w;
        views[ eye ].height = impl->eye_h;

        uint32_t image_index = 0;
        XrSwapchainImageAcquireInfo acquire_info{ };
        acquire_info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrAcquireSwapchainImage( impl->swapchains[ eye ], &acquire_info, &image_index ) ) ) ) {
            should_render = false;
            impl->last_rendered = false;
            return true;
        }
        XrSwapchainImageWaitInfo image_wait{ };
        image_wait.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
        image_wait.timeout = XR_INFINITE_DURATION;
        XR_LOG_CALL( impl->instance, xrWaitSwapchainImage( impl->swapchains[ eye ], &image_wait ) );
        impl->acquired_image[ eye ] = static_cast<int>( image_index );
        views[ eye ].fbo = impl->fbos[ eye ][ image_index ];
    }

    // Acquire the UI layer image for this frame — released in end_frame
    // with the same unconditional rule as the eyes.
    uint32_t ui_index = 0;
    XrSwapchainImageAcquireInfo ui_acquire{ };
    ui_acquire.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    if ( XR_FAILED( XR_LOG_CALL( impl->instance, xrAcquireSwapchainImage( impl->ui_swapchain, &ui_acquire, &ui_index ) ) ) ) {
        should_render = false;
        impl->last_rendered = false;
        return true;
    }
    XrSwapchainImageWaitInfo ui_wait{ };
    ui_wait.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    ui_wait.timeout = XR_INFINITE_DURATION;
    XR_LOG_CALL( impl->instance, xrWaitSwapchainImage( impl->ui_swapchain, &ui_wait ) );
    impl->ui_acquired_image = static_cast<int>( ui_index );
    return true;
}

bool OpenXRSession::ui_target( unsigned int &fbo, int &w, int &h ) const {
    if ( impl->ui_acquired_image < 0 ) {
        return false;
    }
    fbo = impl->ui_fbos[ impl->ui_acquired_image ];
    w = impl->eye_w;
    h = impl->eye_h;
    return true;
}

void OpenXRSession::set_ui_layer( const VrUiLayer &layer ) {
    impl->ui_layer = layer;
    impl->ui_layer_set = true;
}

void OpenXRSession::end_frame( ) {
    // Release every acquired image even when begin_frame bailed mid-acquire
    // — an unreleased image blocks the next acquire on that swapchain.
    for ( int eye = 0; eye < 2; eye++ ) {
        if ( impl->acquired_image[ eye ] >= 0 ) {
            XrSwapchainImageReleaseInfo release_info{ };
            release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            XR_LOG_CALL( impl->instance, xrReleaseSwapchainImage( impl->swapchains[ eye ], &release_info ) );
            impl->acquired_image[ eye ] = -1;
        }
    }
    if ( impl->ui_acquired_image >= 0 ) {
        XrSwapchainImageReleaseInfo release_info{ };
        release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        XR_LOG_CALL( impl->instance, xrReleaseSwapchainImage( impl->ui_swapchain, &release_info ) );
        impl->ui_acquired_image = -1;
    }

    XrCompositionLayerProjectionView layer_views[ 2 ] = { };
    XrCompositionLayerProjection layer{ };
    layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    XrCompositionLayerQuad ui_quad{ };
    ui_quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    XrFrameEndInfo end_info{ };
    end_info.type = XR_TYPE_FRAME_END_INFO;
    end_info.displayTime = impl->predicted_display_time;
    end_info.environmentBlendMode = impl->blend_mode;
    if ( impl->last_rendered ) {
        for ( int eye = 0; eye < 2; eye++ ) {
            layer_views[ eye ].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            layer_views[ eye ].pose = impl->located_views[ eye ].pose;
            layer_views[ eye ].fov = impl->located_views[ eye ].fov;
            layer_views[ eye ].subImage.swapchain = impl->swapchains[ eye ];
            layer_views[ eye ].subImage.imageRect.offset = { 0, 0 };
            layer_views[ eye ].subImage.imageRect.extent = { impl->eye_w, impl->eye_h };
            layer_views[ eye ].subImage.imageArrayIndex = 0;
        }
        layer.space = impl->local_space;
        layer.viewCount = 2;
        layer.views = layer_views;
        const XrCompositionLayerBaseHeader *layers[] = {
            reinterpret_cast<XrCompositionLayerBaseHeader *>( &layer ),
            reinterpret_cast<XrCompositionLayerBaseHeader *>( &ui_quad ),
        };
        end_info.layerCount = 1;
        if ( impl->ui_layer_set ) {
            // UI quad above the projection layer; the swapchain content is
            // premultiplied alpha, blended against the scene.
            ui_quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            ui_quad.space = impl->ui_layer.world_locked ? impl->local_space : impl->view_space;
            ui_quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            ui_quad.subImage.swapchain = impl->ui_swapchain;
            ui_quad.subImage.imageRect.offset = { 0, 0 };
            ui_quad.subImage.imageRect.extent = { impl->eye_w, impl->eye_h };
            ui_quad.subImage.imageArrayIndex = 0;
            const glm::quat &o = impl->ui_layer.orientation;
            const glm::vec3 &p = impl->ui_layer.position;
            ui_quad.pose.orientation = XrQuaternionf{ o.x, o.y, o.z, o.w };
            ui_quad.pose.position = XrVector3f{ p.x, p.y, p.z };
            ui_quad.size = XrExtent2Df{ impl->ui_layer.size.x, impl->ui_layer.size.y };
            end_info.layerCount = 2;
        }
        end_info.layers = layers;
    }
    impl->ui_layer_set = false;
    XR_LOG_CALL( impl->instance, xrEndFrame( impl->session, &end_info ) );
    impl->last_rendered = false;
}

void OpenXRSession::sync_input( VrControllerInput &out ) {
    out = VrControllerInput{ };
    if ( !is_running( ) || impl->action_set == XR_NULL_HANDLE ) {
        return;
    }
    XrActiveActionSet active_set{ };
    active_set.actionSet = impl->action_set;
    active_set.subactionPath = XR_NULL_PATH;
    XrActionsSyncInfo sync_info{ };
    sync_info.type = XR_TYPE_ACTIONS_SYNC_INFO;
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &active_set;
    const XrResult rc = xrSyncActions( impl->session, &sync_info );
    if ( XR_FAILED( rc ) || rc == XR_SESSION_NOT_FOCUSED ) {
        return;
    }

    const auto get_vec2 = [ this ]( XrAction action, glm::vec2 &v ) {
        XrActionStateVector2f state{ };
        state.type = XR_TYPE_ACTION_STATE_VECTOR2F;
        XrActionStateGetInfo gi{ };
        gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
        gi.action = action;
        xrGetActionStateVector2f( impl->session, &gi, &state );
        if ( state.isActive ) {
            v = glm::vec2( state.currentState.x, state.currentState.y );
        }
    };
    const auto get_bool = [ this ]( XrAction action, bool &b ) {
        XrActionStateBoolean state{ };
        state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
        XrActionStateGetInfo gi{ };
        gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
        gi.action = action;
        xrGetActionStateBoolean( impl->session, &gi, &state );
        if ( state.isActive ) {
            b = state.currentState == XR_TRUE;
        }
    };
    get_vec2( impl->act_move, out.move );
    get_vec2( impl->act_turn, out.turn );
    get_bool( impl->act_jump, out.jump );
    get_bool( impl->act_sneak, out.sneak );
    get_bool( impl->act_sprint, out.sprint );
    get_bool( impl->act_break_block, out.break_block );
    get_bool( impl->act_place_block, out.place_block );
    get_bool( impl->act_pick_block, out.pick_block );
    get_bool( impl->act_hotbar_next, out.hotbar_next );
    get_bool( impl->act_hotbar_prev, out.hotbar_prev );
    get_bool( impl->act_inventory, out.inventory );
    get_bool( impl->act_toggle_fly, out.toggle_fly );

    // Aim pose (right controller) in LOCAL space at this frame's predicted
    // display time — the same time the views are located for.
    if ( impl->aim_space != XR_NULL_HANDLE && impl->predicted_display_time != 0 ) {
        XrSpaceLocation aim_location{ };
        aim_location.type = XR_TYPE_SPACE_LOCATION;
        if ( XR_SUCCEEDED( xrLocateSpace( impl->aim_space, impl->local_space, impl->predicted_display_time, &aim_location ) ) &&
             ( aim_location.locationFlags & ( XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) ) == ( XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) ) {
            out.aim_valid = true;
            const XrQuaternionf &q = aim_location.pose.orientation;
            out.aim_orientation = glm::quat( q.w, q.x, q.y, q.z );
            out.aim_position = glm::vec3( aim_location.pose.position.x, aim_location.pose.position.y, aim_location.pose.position.z );
        }
    }
}

void OpenXRSession::haptic_pulse( float amplitude, float seconds ) {
    if ( !is_running( ) || impl->act_haptic == XR_NULL_HANDLE ) {
        return;
    }
    XrHapticVibration vib{ };
    vib.type = XR_TYPE_HAPTIC_VIBRATION;
    vib.amplitude = amplitude;
    vib.frequency = XR_FREQUENCY_UNSPECIFIED;
    vib.duration = static_cast<XrDuration>( seconds * 1.0e9f );
    XrHapticActionInfo info{ };
    info.type = XR_TYPE_HAPTIC_ACTION_INFO;
    info.action = impl->act_haptic;
    XR_LOG_CALL( impl->instance, xrApplyHapticFeedback( impl->session, &info, reinterpret_cast<const XrHapticBaseHeader *>( &vib ) ) );
}

void OpenXRSession::request_exit( ) {
    if ( impl->session != XR_NULL_HANDLE && impl->session_active ) {
        XR_LOG_CALL( impl->instance, xrRequestExitSession( impl->session ) );
    }
}

void OpenXRSession::destroy( ) {
    if ( !impl ) {
        return;
    }
    for ( int eye = 0; eye < 2; eye++ ) {
        if ( impl->fbos[ eye ].size( ) ) {
            glDeleteFramebuffers( static_cast<GLsizei>( impl->fbos[ eye ].size( ) ), impl->fbos[ eye ].data( ) );
            impl->fbos[ eye ].clear( );
        }
        if ( impl->depth_rbo[ eye ] ) {
            glDeleteRenderbuffers( 1, &impl->depth_rbo[ eye ] );
            impl->depth_rbo[ eye ] = 0;
        }
        if ( impl->swapchains[ eye ] != XR_NULL_HANDLE ) {
            xrDestroySwapchain( impl->swapchains[ eye ] );
            impl->swapchains[ eye ] = XR_NULL_HANDLE;
        }
    }
    if ( impl->ui_fbos.size( ) ) {
        glDeleteFramebuffers( static_cast<GLsizei>( impl->ui_fbos.size( ) ), impl->ui_fbos.data( ) );
        impl->ui_fbos.clear( );
    }
    if ( impl->ui_depth_rbos.size( ) ) {
        glDeleteRenderbuffers( static_cast<GLsizei>( impl->ui_depth_rbos.size( ) ), impl->ui_depth_rbos.data( ) );
        impl->ui_depth_rbos.clear( );
    }
    if ( impl->ui_swapchain != XR_NULL_HANDLE ) {
        xrDestroySwapchain( impl->ui_swapchain );
        impl->ui_swapchain = XR_NULL_HANDLE;
    }
    if ( impl->view_space != XR_NULL_HANDLE ) {
        xrDestroySpace( impl->view_space );
        impl->view_space = XR_NULL_HANDLE;
    }
    if ( impl->local_space != XR_NULL_HANDLE ) {
        xrDestroySpace( impl->local_space );
        impl->local_space = XR_NULL_HANDLE;
    }
    if ( impl->aim_space != XR_NULL_HANDLE ) {
        xrDestroySpace( impl->aim_space );
        impl->aim_space = XR_NULL_HANDLE;
    }
    if ( impl->action_set != XR_NULL_HANDLE ) {
        xrDestroyActionSet( impl->action_set );
        impl->action_set = XR_NULL_HANDLE;
    }
    if ( impl->session != XR_NULL_HANDLE ) {
        xrDestroySession( impl->session );
        impl->session = XR_NULL_HANDLE;
    }
    if ( impl->instance != XR_NULL_HANDLE ) {
        xrDestroyInstance( impl->instance );
        impl->instance = XR_NULL_HANDLE;
    }
    impl->session_active = false;
}

int OpenXRSession::eye_width( ) const {
    return impl->eye_w;
}
int OpenXRSession::eye_height( ) const {
    return impl->eye_h;
}

#endif // REPGAME_OPENXR
