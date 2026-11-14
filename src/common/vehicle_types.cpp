// The vehicle type table — every type-specific constant the generic code
// needs (box, seat, model/skin, drop item, physics dispatch). Adding a
// vehicle kind is a VehicleType value plus a row here. Compiled into the
// server too (load_vehicles validates record types).

#include "common/vehicle_types.hpp"

#include <cstring>

// Boats share MOB_MODEL_BOAT with a skin per wood variant, in the same
// order the boat skins appear in mobs_manifest.txt. Seat/eye values are
// capture-tuned starting points.
#define BOAT_DEF( NAME, ITEM, SKIN ) { NAME, ITEM, MOB_MODEL_BOAT, SKIN, VehiclePhysics::WATER, \
                                       1.375, 0.5625, 1.0, 0.3, 0.3, 1.0f }
static const VehicleTypeDef VEHICLE_TYPE_DEFS[ VEHICLE_TYPE_COUNT ] = {
    // name        item      model               skin              physics
    // width  height  rider_eye  seat_hip  seat_back  model_scale
    { "minecart", MINECART, MOB_MODEL_MINECART, MOB_SKIN_MINECART, VehiclePhysics::RAIL,
      0.98,  0.7,     1.3,        0.4,      0.25,      1.05f },
    BOAT_DEF( "oak_boat", OAK_BOAT, MOB_SKIN_BOAT_BOAT_OAK ),
    BOAT_DEF( "spruce_boat", SPRUCE_BOAT, MOB_SKIN_BOAT_BOAT_SPRUCE ),
    BOAT_DEF( "birch_boat", BIRCH_BOAT, MOB_SKIN_BOAT_BOAT_BIRCH ),
    BOAT_DEF( "jungle_boat", JUNGLE_BOAT, MOB_SKIN_BOAT_BOAT_JUNGLE ),
    BOAT_DEF( "acacia_boat", ACACIA_BOAT, MOB_SKIN_BOAT_BOAT_ACACIA ),
    BOAT_DEF( "dark_oak_boat", DARK_OAK_BOAT, MOB_SKIN_BOAT_BOAT_DARKOAK ),
    BOAT_DEF( "mangrove_boat", MANGROVE_BOAT, MOB_SKIN_BOAT_MANGROVE_BOAT ),
    BOAT_DEF( "cherry_boat", CHERRY_BOAT, MOB_SKIN_BOAT_CHERRY_BOAT ),
    BOAT_DEF( "pale_oak_boat", PALE_OAK_BOAT, MOB_SKIN_BOAT_PALE_OAK_BOAT ),
    BOAT_DEF( "poplar_boat", POPLAR_BOAT, MOB_SKIN_BOAT_POPLAR_BOAT ),
};
#undef BOAT_DEF

const VehicleTypeDef &vehicle_type_def( const VehicleType t ) {
    return VEHICLE_TYPE_DEFS[ vehicle_type_valid( t ) ? t : VEHICLE_MINECART ];
}

bool vehicle_type_valid( const uint32_t t ) {
    return t < VEHICLE_TYPE_COUNT;
}

VehicleType vehicle_type_for_item( const ItemID item ) {
    for ( int t = 0; t < VEHICLE_TYPE_COUNT; t++ ) {
        if ( VEHICLE_TYPE_DEFS[ t ].item == item ) {
            return static_cast<VehicleType>( t );
        }
    }
    return VEHICLE_TYPE_COUNT;
}

bool vehicle_type_from_name( const char *name, VehicleType *out ) {
    for ( int t = 0; t < VEHICLE_TYPE_COUNT; t++ ) {
        if ( strcmp( VEHICLE_TYPE_DEFS[ t ].name, name ) == 0 ) {
            *out = static_cast<VehicleType>( t );
            return true;
        }
    }
    return false;
}
