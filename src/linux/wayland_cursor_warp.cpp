#include "linux/wayland_cursor_warp.hpp"

#include <wayland-client.h>
#include "wayland-protocols/pointer-constraints-unstable-v1-client-protocol.h"

#include <cstdio>
#include <cstring>

// Context passed to Wayland listeners during a warp attempt.
struct WarpContext {
    struct wl_seat *seat;
    struct zwp_pointer_constraints_v1 *pointer_constraints;
    uint32_t seat_name;
    uint32_t constraints_name;
    bool has_pointer_cap;
    struct wl_surface *target_surface;
    bool pointer_entered;
};

static void registry_global( void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version ) {
    WarpContext *ctx = static_cast<WarpContext *>( data );
    if ( strcmp( interface, "wl_seat" ) == 0 ) {
        ctx->seat = static_cast<struct wl_seat *>( wl_registry_bind( registry, name, &wl_seat_interface, 1 ) );
        ctx->seat_name = name;
    } else if ( strcmp( interface, "zwp_pointer_constraints_v1" ) == 0 ) {
        ctx->pointer_constraints = static_cast<struct zwp_pointer_constraints_v1 *>( wl_registry_bind( registry, name, &zwp_pointer_constraints_v1_interface, 1 ) );
        ctx->constraints_name = name;
    }
}

static void registry_global_remove( void *data, struct wl_registry *registry, uint32_t name ) {
    // Not handled; we destroy everything before the next roundtrip.
}

static const struct wl_registry_listener registry_listener = { registry_global, registry_global_remove };

static void seat_capabilities( void *data, struct wl_seat *seat, uint32_t caps ) {
    WarpContext *ctx = static_cast<WarpContext *>( data );
    if ( caps & WL_SEAT_CAPABILITY_POINTER ) {
        ctx->has_pointer_cap = true;
    }
}

static const struct wl_seat_listener seat_listener = { seat_capabilities, nullptr };

static void pointer_enter( void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y ) {
    WarpContext *ctx = static_cast<WarpContext *>( data );
    if ( surface == ctx->target_surface ) {
        ctx->pointer_entered = true;
    }
}

static void pointer_leave( void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface ) {}
static void pointer_motion( void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y ) {}
static void pointer_button( void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state ) {}
static void pointer_axis( void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value ) {}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter, pointer_leave, pointer_motion, pointer_button, pointer_axis,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

bool wayland_warp_cursor( struct wl_display *display, struct wl_surface *surface, int x, int y ) {
    if ( !display || !surface ) {
        return false;
    }

    WarpContext ctx = {};
    ctx.target_surface = surface;

    // 1. Discover the wl_seat and zwp_pointer_constraints_v1 globals.
    struct wl_registry *registry = wl_display_get_registry( display );
    wl_registry_add_listener( registry, &registry_listener, &ctx );
    wl_display_roundtrip( display );

    if ( !ctx.pointer_constraints ) {
        // Compositor doesn't support pointer constraints; caller should fall back.
        if ( ctx.seat ) {
            wl_seat_destroy( ctx.seat );
        }
        wl_registry_destroy( registry );
        return false;
    }

    // 2. Wait for the seat to report pointer capability, then get a wl_pointer.
    if ( ctx.seat ) {
        wl_seat_add_listener( ctx.seat, &seat_listener, &ctx );
        wl_display_roundtrip( display );
    }

    struct wl_pointer *pointer = nullptr;
    if ( ctx.has_pointer_cap && ctx.seat ) {
        pointer = wl_seat_get_pointer( ctx.seat );
    }

    if ( !pointer ) {
        zwp_pointer_constraints_v1_destroy( ctx.pointer_constraints );
        if ( ctx.seat ) {
            wl_seat_destroy( ctx.seat );
        }
        wl_registry_destroy( registry );
        return false;
    }

    // 3. Wait for the pointer to enter our surface (so the compositor
    //    knows this pointer has focus and will accept a lock request).
    wl_pointer_add_listener( pointer, &pointer_listener, &ctx );
    wl_display_roundtrip( display );

    // 4. Create a one-shot locked pointer, set the cursor position hint
    //    to (x, y), commit, and destroy. Mutter warps the cursor to the
    //    hint position when the locked pointer is destroyed.
    struct zwp_locked_pointer_v1 *lock =
        zwp_pointer_constraints_v1_lock_pointer( ctx.pointer_constraints, surface, pointer, nullptr,
                                                  ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT );
    zwp_locked_pointer_v1_set_cursor_position_hint( lock, wl_fixed_from_int( x ), wl_fixed_from_int( y ) );
    wl_surface_commit( surface );
    zwp_locked_pointer_v1_destroy( lock );
    wl_display_roundtrip( display );

    // 5. Clean up.
    wl_pointer_destroy( pointer );
    zwp_pointer_constraints_v1_destroy( ctx.pointer_constraints );
    wl_seat_destroy( ctx.seat );
    wl_registry_destroy( registry );

    return true;
}
