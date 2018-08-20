
BlockID finalBlockId = AIR; // base block type is grass I guess
if ( y < terrainHeight ) {
    finalBlockId = DIRT;
    if ( -2.3 + WATER_LEVEL < terrainHeight && terrainHeight < 0.3 + WATER_LEVEL ) {
        finalBlockId = SAND;
    } else if ( terrainHeight < WATER_LEVEL + 0.3 ) {
        float under_water = MAP_GEN( under_water_block, x, z );
        finalBlockId = under_water > 0.5 ? GRAVEL : SAND;
    } else if ( terrainHeight > MOUNTAN_CAP_HEIGHT ) {
        float mountian_block = MAP_GEN( mountian_block, x, z );
        if ( mountian_block * ( terrainHeight - MOUNTAN_CAP_HEIGHT ) > 25 ) {
            finalBlockId = SNOW;
        } else if ( mountian_block * ( terrainHeight - MOUNTAN_CAP_HEIGHT ) > 10 ) {
            finalBlockId = STONE;
        } else if ( y + 1 >= terrainHeight ) {
            finalBlockId = GRASS;
        }
    } else if ( y + 1 >= terrainHeight ) {
        finalBlockId = GRASS;
    }
} else {
    // There should not be a block here, but water is still possible at low height
    if ( y < WATER_LEVEL ) {
        finalBlockId = WATER;
    }
}