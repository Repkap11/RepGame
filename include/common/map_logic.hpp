// Included by c and cuda version of map_gen
BlockID finalBlockId = AIR;
if ( y < terrainHeight ) {
    if ( y + DIRT_SURFACE_THCKNESS < terrainHeight ) { // Deep underground
        finalBlockId = STONE;
    } else { // Near the surface
        finalBlockId = DIRT;
        if ( -2.3 + WATER_LEVEL < terrainHeight && terrainHeight < 0.3 + WATER_LEVEL ) {
            finalBlockId = SAND;
        } else if ( terrainHeight < WATER_LEVEL + 0.3 ) {
            float under_water = MAP_GEN( under_water_block, x, z );
            finalBlockId = under_water > 0.5 ? GRAVEL : SAND;
        } else if ( terrainHeight > MOUNTAN_CAP_HEIGHT ) { // Tall enough to be a mountian
            float diff = terrainHeight - MOUNTAN_CAP_HEIGHT;
            float depth_of_dirt = DIRT_SURFACE_THCKNESS - ( diff / 30.0f );
            // 0 -> 4
            // 2 -> 3
            // 4 -> 2
            // 6 -> 1
            if ( depth_of_dirt < 1 ) {
                depth_of_dirt = 1;
            }
            if ( y + depth_of_dirt < terrainHeight ) { // Not the top block of a mountian
                finalBlockId = STONE;
            } else { // Surface blocks of mountian
                float mountian_block = MAP_GEN( mountian_block, x, z );
                if ( mountian_block * ( terrainHeight - MOUNTAN_CAP_HEIGHT ) > 20 ) {
                    if ( y + 1 < terrainHeight ) { // Only snow on very top
                        finalBlockId = DIRT;
                    } else {
                        finalBlockId = SNOW;
                    }
                } else if ( depth_of_dirt == 1 && mountian_block * ( terrainHeight - MOUNTAN_CAP_HEIGHT ) > 10 ) {
                    finalBlockId = DIRT;
                } else {
                    if ( y + 1 < terrainHeight ) { // Only grass on very top
                        finalBlockId = DIRT;
                    } else {
                        finalBlockId = GRASS;
                    }
                }
            }
        } else if ( y + 1 >= terrainHeight ) {
            finalBlockId = GRASS;
        }
    } // End near the surface
} else {
    // There should not be a block here, but water is still possible at low height
    if ( y < WATER_LEVEL ) {
        finalBlockId = WATER;
    }
}