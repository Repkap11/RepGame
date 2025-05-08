// Included by c and cuda version of map_gen
BlockID finalBlockId = AIR;
if ( y < terrainHeight ) {
    if ( y + DIRT_SURFACE_THICKNESS < terrainHeight ) { // Deep underground
        float should_be_iron_ore = MAP_GEN( is_iron_ore_instead_of_stone, x, y, z );
        if ( should_be_iron_ore < 0.05 ) {
            finalBlockId = IRON_ORE;
        } else {
            float should_be_coal_ore = MAP_GEN( is_coal_ore_instead_of_stone, x, y, z );
            if ( should_be_coal_ore < 0.05 ) {
                finalBlockId = COAL_ORE;
            } else {
                float should_be_gold_ore = MAP_GEN( is_gold_ore_instead_of_stone, x, y, z );
                if ( should_be_gold_ore < 0.05 ) {
                    finalBlockId = GOLD_ORE;
                } else {
                    finalBlockId = STONE;
                }
            }
        }
    } else { // Near the surface
        finalBlockId = DIRT;
        if ( -2.3 + WATER_LEVEL < terrainHeight && terrainHeight < 0.3 + WATER_LEVEL ) {
            finalBlockId = SAND;
        } else if ( terrainHeight < WATER_LEVEL + 0.3 ) {
            float under_water = MAP_GEN( under_water_block, x, z );
            finalBlockId = under_water > 0.5 ? GRAVEL : SAND;
        } else if ( terrainHeight > MOUNTAIN_CAP_HEIGHT ) { // Tall enough to be a mountian
            float diff = terrainHeight - MOUNTAIN_CAP_HEIGHT;
            float depth_of_dirt = DIRT_SURFACE_THICKNESS - ( diff / 30.0f );
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
                float mountian_block = MAP_GEN( mountains_block, x, z );
                if ( mountian_block * ( terrainHeight - MOUNTAIN_CAP_HEIGHT ) > 20 ) {
                    if ( y + 1 < terrainHeight ) { // Only snow on very top
                        finalBlockId = DIRT;
                    } else {
                        finalBlockId = SNOW;
                    }
                } else if ( depth_of_dirt == 1 && mountian_block * ( terrainHeight - MOUNTAIN_CAP_HEIGHT ) > 10 ) {
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
    if ( y > -100 ) {
        // See if we need a cave
        float cave_dencity = MAP_GEN( cave_density, x, y * 2.5f, z );
        float cave_lerp = 1.0f - MAP_GEN( inverse_lerp, -20.0f, 40.0f, y - terrainHeight );
        if ( cave_dencity < cave_lerp * CAVE_THRESHOLD ) {
            finalBlockId = AIR;
        }
    } else {
        // Super low is bedrock, and no caves.
        finalBlockId = BEDROCK;
    }
} else {
    // There should not be a block here, but water is still possible at low height
    if ( y < WATER_LEVEL ) {
        finalBlockId = WATER;
    }
}