#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "common/mob_stress.hpp"
#include "common/Logging.hpp"
#include "common/utils/env_config.hpp"

MobStressSpec mob_stress_parse( const char *text ) {
    MobStressSpec spec;
    if ( text == nullptr || text[ 0 ] == '\0' ) {
        return spec;
    }
    const std::string s( text );
    size_t start = 0;
    bool first = true;
    int max = 0;
    while ( start <= s.size( ) ) {
        const size_t comma = s.find( ',', start );
        const std::string tok = s.substr( start, comma == std::string::npos ? std::string::npos : comma - start );
        start = comma == std::string::npos ? s.size( ) + 1 : comma + 1;
        if ( tok.empty( ) ) {
            continue;
        }
        if ( first ) {
            first = false;
            spec.count = atoi( tok.c_str( ) );
            continue;
        }
        if ( tok.rfind( "ai=", 0 ) == 0 ) {
            spec.ai = atoi( tok.c_str( ) + 3 ) != 0;
            continue;
        }
        if ( tok.rfind( "max=", 0 ) == 0 ) {
            max = atoi( tok.c_str( ) + 4 );
            continue;
        }
        int type = -1;
        for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
            if ( tok == mob_types[ t ].name ) {
                type = t;
            }
        }
        if ( type < 0 ) {
            pr_debug( "REPGAME_MOB_WORLD: unknown token '%s' (want count,ai=0|1,max=N,<mob type>...)", tok.c_str( ) );
            continue;
        }
        spec.types.push_back( static_cast<MobType>( type ) );
    }
    if ( spec.count <= 0 ) {
        pr_debug( "REPGAME_MOB_WORLD: '%s' has no positive count — mob world off", text );
        return MobStressSpec( );
    }
    if ( spec.count > MOB_STRESS_MAX ) {
        pr_debug( "REPGAME_MOB_WORLD: count %d clamped to %d", spec.count, MOB_STRESS_MAX );
        spec.count = MOB_STRESS_MAX;
    }
    spec.capacity = max > spec.count ? ( max > MOB_STRESS_MAX ? MOB_STRESS_MAX : max ) : spec.count;
    if ( spec.types.empty( ) ) {
        for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
            spec.types.push_back( static_cast<MobType>( t ) );
        }
    }
    spec.side = static_cast<int>( ceil( sqrt( static_cast<double>( spec.capacity ) ) ) );
    spec.origin = -spec.extent( ) / 2;
    return spec;
}

const MobStressSpec &mob_stress_spec( ) {
    static const MobStressSpec spec = mob_stress_parse( env_get( "REPGAME_MOB_WORLD" ) );
    return spec;
}

glm::dvec3 mob_stress_cell_pos( const MobStressSpec &spec, const int i ) {
    const int cell = spec.capacity > 0 ? i % spec.capacity : 0;
    const int col = cell % ( spec.side > 0 ? spec.side : 1 );
    const int row = cell / ( spec.side > 0 ? spec.side : 1 );
    const double half = MOB_STRESS_SPACING * 0.5;
    return glm::dvec3( spec.origin + col * MOB_STRESS_SPACING + half, 1.0, spec.origin + row * MOB_STRESS_SPACING + half );
}

BlockCell mob_stress_arena_cell( const MobStressSpec &spec, const int x, const int y, const int z ) {
    if ( y <= 0 ) {
        // Solid ground: an air void under the floor would make the light
        // cascade run removal BFS over the whole connected void volume.
        return block_cell_pack( SANDSTONE, 0 );
    }
    if ( y <= 2 ) {
        const int lo = spec.origin - 1;
        const int hi = spec.origin + spec.extent( );
        const bool in_x = x >= lo && x <= hi;
        const bool in_z = z >= lo && z <= hi;
        if ( ( in_z && ( x == lo || x == hi ) ) || ( in_x && ( z == lo || z == hi ) ) ) {
            return block_cell_pack( STONE, 0 );
        }
    }
    return block_cell_pack( AIR, 0 );
}
