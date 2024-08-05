#include "common/imgui_overlay.hpp"

ImGuiDebugVars debug_vars;

ImGuiDebugVars &imgui_overlay_get_imgui_debug_vars( ) {
    return debug_vars;
}

#if ( defined( REPGAME_WASM ) )

void imgui_overlay_init( ImGuiOverlay *imgui_overlay ) {
}
void imgui_overlay_attach_to_window( ImGuiOverlay *ui_overlay, SDL_Window *window, SDL_GLContext gl_context ) {
}
void imgui_overlay_handle_sdl2_event( ImGuiOverlay *ui_overlay, SDL_Event *event, bool *handledMouse, bool *handledKeyboard ) {
}
void imgui_overlay_draw( ImGuiOverlay *ui_overlay, InputState *input ) {
}
void imgui_overlay_cleanup( ImGuiOverlay *ui_overlay ) {
}

#else

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

void imgui_overlay_init( ImGuiOverlay *imgui_overlay ) {
    // imgui_overlay->show_demo_window = true;
}

void imgui_overlay_attach_to_window( ImGuiOverlay *ui_overlay, SDL_Window *window, SDL_GLContext gl_context ) {
    IMGUI_CHECKVERSION( );
    ImGui::CreateContext( );
    ImGuiIO &io = ImGui::GetIO( );
    io.WantCaptureMouse = true;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark( );

    const char *glsl_version = "#version 300 es";

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL( window, gl_context );
    ImGui_ImplOpenGL3_Init( glsl_version );

    float IMGUI_UI_SCALE = 2.0f;

    ImGui::GetStyle( ).ScaleAllSizes( IMGUI_UI_SCALE );
    ImFontConfig cfg;
    cfg.SizePixels = 13 * IMGUI_UI_SCALE;
    io.FontDefault = io.Fonts->AddFontDefault( &cfg );
    ; //->DisplayOffset.y = IMGUI_UI_SCALE;
}

void imgui_overlay_handle_sdl2_event( ImGuiOverlay *ui_overlay, SDL_Event *event, bool *handledMouse, bool *handledKeyboard ) {
    ImGui_ImplSDL2_ProcessEvent( event );
    ImGuiIO &io = ImGui::GetIO( );
    *handledMouse = io.WantCaptureMouse;
    *handledKeyboard = io.WantCaptureKeyboard;
}

void imgui_overlay_draw( ImGuiOverlay *imgui_overlay, InputState *input ) {
    ImGui_ImplOpenGL3_NewFrame( );
    ImGui_ImplSDL2_NewFrame( );
    ImGui::NewFrame( );

    if ( SHOW_IMGUI ) {
        ImGuiIO &io = ImGui::GetIO( );
        if ( false ) {
            ImGui::ShowDemoWindow( &input->inventory_open );
        } else {

            ImGui::Begin( "RepGame" ); // Create a window called "Hello, world!" and append into it.
            ImGui::Text( "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate );

            ImGui::SliderFloat2( "Corner 1", &debug_vars.corner1.x, -0.5f, 1.5f );
            ImGui::SliderFloat2( "Corner 3", &debug_vars.corner3.x, -0.5f, 1.5f );
            // debug_vars.corner1.x = round( debug_vars.corner1.x * 20 ) / 20;
            // debug_vars.corner1.y = round( debug_vars.corner1.y * 20 ) / 20;

            // ImGui::Text( "This is some useful text." );               // Display some text (you can use a format strings too)
            // ImGui::Checkbox( "Demo Window", &input->inventory_open ); // Edit bools storing our window open/close state
            // ImGui::Checkbox( "Another Window", &input->inventory_open );

            // if ( ImGui::Button( "Button" ) ) // Buttons return true when clicked (most widgets return true when edited/activated)
            //     counter++;
            // ImGui::SameLine( );
            // ImGui::Text( "counter = %d", counter );

            ImGui::End( );
        }
    }
    ImGui::Render( );
    ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData( ) );
}

void imgui_overlay_cleanup( ImGuiOverlay *imgui_overlay ) {
    ImGui_ImplOpenGL3_Shutdown( );
    ImGui_ImplSDL2_Shutdown( );
    ImGui::DestroyContext( );
}
#endif