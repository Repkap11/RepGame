#pragma once

#include <string>

// Captures all framebuffer color attachments and the final display as TGA files
// in the screenshots/ directory. Only implemented on Linux/Windows (platforms
// with framebuffer support). No-op on other platforms.
void take_screenshot( unsigned int frameBufferId, const std::string &prefix );
