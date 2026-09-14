#pragma once

// Warps the cursor to (x, y) on the given Wayland surface using the
// zwp_pointer_constraints_v1 lock→hint→unlock approach. This works on
// GNOME Wayland where SDL_WarpMouseInWindow fails because Mutter only
// honors wp_pointer_warp_v1 while a mouse button is held.
//
// display/surface should be the wl_display/wl_surface obtained from
// SDL_GetWindowProperties. Returns true if the warp was attempted.
bool wayland_warp_cursor( struct wl_display *display, struct wl_surface *surface, int x, int y );
