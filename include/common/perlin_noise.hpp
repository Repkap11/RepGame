#pragma once

// Raw 0-255 hash roll on integer coords — roughly uniform, unlike the
// smoothed perlin fields, so it's usable for low-probability thresholds.
int noise2( int x, int y, int seed );

float perlin_noise2d( float x, float y, float freq, int depth, int seed );
float perlin_noise3d( float x, float y, float z, float freq, int depth, int seed );