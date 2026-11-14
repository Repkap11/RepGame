#pragma once

#include <cstdint>

#include "common/item.hpp"            // ItemID
#include "common/mob_models_gen.hpp"  // MobModel

// The vehicle type table: every per-type constant the generic Vehicles
// sim/render needs lives in one VehicleTypeDef row, so adding a vehicle
// kind is a new enum + a new row — no branches in the generic code.
// `physics` picks which per-type physics TU (vehicle_rail.cpp,
// vehicle_boat.cpp) drives the owned sim.
//
// Values are append-only: a VehicleType travels on the wire inside
// NetEntityRecord::type and persists in VehicleFileRecord::type, so an
// existing value must never be renumbered or reused.
enum VehicleType : uint8_t {
    VEHICLE_MINECART = 0,
    VEHICLE_OAK_BOAT,
    VEHICLE_SPRUCE_BOAT,
    VEHICLE_BIRCH_BOAT,
    VEHICLE_JUNGLE_BOAT,
    VEHICLE_ACACIA_BOAT,
    VEHICLE_DARK_OAK_BOAT,
    VEHICLE_MANGROVE_BOAT,
    VEHICLE_CHERRY_BOAT,
    VEHICLE_PALE_OAK_BOAT,
    VEHICLE_POPLAR_BOAT,
    VEHICLE_TYPE_COUNT
};

enum class VehiclePhysics : uint8_t {
    RAIL,
    WATER,
};

// One concentric piece of a compound collision hull: an oriented
// rectangle (half extents along the hull fwd/side axes) centred on the
// vehicle. The union of a type's rects is its collision footprint.
struct VehicleHullRect {
    double half_length, half_width;
};

struct VehicleTypeDef {
    const char *name;        // "minecart" — script / mp-test / inspect spelling
    ItemID item;             // dropped when broken, placed from
    MobModel model;          // MOB_MODEL_MINECART
    int skin;                // skin index within the model
    VehiclePhysics physics;
    double width, length, height; // hull footprint XZ (minecart: length == width -> one square box)
    double rider_eye_offset; // camera height above bottom-centre while riding
    double seat_hip_height;  // remote-avatar hip height above bottom-centre
    double seat_back;        // remote-avatar pin distance behind centre
    float model_scale;       // render scale factor on the model's 1/16 units
    // Compound collision hull (nullptr -> the single width x length
    // rect). Every rect corner inside radius 1.0 of centre lets a boat
    // spin in place in 2-wide water while the narrow end rects still
    // cover the bow/stern.
    const VehicleHullRect *hull_rects;
    int hull_rect_count;
};

const VehicleTypeDef &vehicle_type_def( VehicleType t );
bool vehicle_type_valid( uint32_t t );
// The vehicle type placed by `item` (VEHICLE_TYPE_COUNT when none).
VehicleType vehicle_type_for_item( ItemID item );
// The vehicle type named `name` (script / mp-test / inspect spelling).
bool vehicle_type_from_name( const char *name, VehicleType *out );
