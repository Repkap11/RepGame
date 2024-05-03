#include "common/imgui_overlay.hpp"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <SDL.h>
#if defined( IMGUI_IMPL_OPENGL_ES2 )
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif
// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

void imgui_overlay_init( ImGuiOverlay *imgui_overlay) {
}

void imgui_overlay_attach_to_window( ImGuiOverlay *ui_overlay, SDL_Window *window, SDL_GLContext gl_context ) {
    IMGUI_CHECKVERSION( );
    ImGui::CreateContext( );
    ImGuiIO &io = ImGui::GetIO( );
    ( void )io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark( );

    const char *glsl_version = "#version 300 es";

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL( window, gl_context );
    ImGui_ImplOpenGL3_Init( glsl_version );

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL( window, gl_context );
    ImGui_ImplOpenGL3_Init( glsl_version );
}

void imgui_overlay_on_screen_size_change( ImGuiOverlay *imgui_overlay, int width, int height ) {
}

void imgui_overlay_draw( ImGuiOverlay *imgui_overlay ) {
    ImGui_ImplOpenGL3_NewFrame( );
    ImGui_ImplSDL2_NewFrame( );
    ImGui::NewFrame( );

    if ( imgui_overlay->show_demo_window ) {
        ImGui::ShowDemoWindow( &imgui_overlay->show_demo_window );
    }
    ImGui::Render( );
    ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData( ) );
}

void imgui_overlay_cleanup( ImGuiOverlay *imgui_overlay ) {
}