#ifndef HEADER_IMGUI_OVERLAY_H
#define HEADER_IMGUI_OVERLAY_H

typedef struct {
    int screen_width;
    int screen_height;
} ImGuiOverlay;

void imgui_overlay_init( ImGuiOverlay *ui_overlay );
void imgui_overlay_on_screen_size_change( ImGuiOverlay *ui_overlay, int width, int height );
void imgui_overlay_draw( ImGuiOverlay *ui_overlay );
void imgui_overlay_cleanup( ImGuiOverlay *ui_overlay );

#endif