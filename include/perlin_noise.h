#ifndef HEADER_PERLIN_NOISE_H
#define HEADER_PERLIN_NOISE_H

float perlin_noise_ground_noise( int x, int z );
float perlin_noise_hills( int x, int z );
float perlin_noise_mountians( int x, int z );
float perlin_noise_mountian_block( int x, int z );
float perlin_noise_under_water_block( int x, int z );
float perlin_noise_level( int x, int z );

#endif
