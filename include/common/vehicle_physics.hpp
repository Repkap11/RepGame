#pragma once

#include <glm/glm.hpp>

#include "common/vehicles.hpp"

class World;
struct RepGameState;

// Internal seam between the generic Vehicles system (vehicles.cpp) and
// the per-physics TUs — one function pair per VehiclePhysics value.
// vehicle_rail.cpp holds the rail-following minecart sim; a future
// vehicle_boat.cpp implements the same shape for WATER physics.
//
// vehicle_tick_rail: the owned per-tick sim for one vehicle — rail
// following, derailment, rider/shove input.
void vehicle_tick_rail( RepGameState &state, Vehicles::Vehicle &v, const Vehicles::VehicleTickInput &in );
// vehicle_tick_boat: the owned per-tick sim for WATER-physics vehicles —
// surface/draft buoyancy, rider paddle + turn input, land crawl.
void vehicle_tick_boat( RepGameState &state, Vehicles::Vehicle &v, const Vehicles::VehicleTickInput &in );
// How deep a boat's bottom rides under the water surface. Shared with
// boat placement (spawn bottom = surface - draft).
inline constexpr double VEHICLE_BOAT_DRAFT = 0.1;
// Shared angle wrap used by both the rail sim and the generic net/render
// interpolation.
double vehicle_wrap_180( double d );
// Mirror pitch is not streamed — each physics model derives it from the
// world under the mirror's position (rail slope for RAIL). Also updates
// v.on_rail from the cell under the mirror.
float vehicle_rail_mirror_pitch( World &world, Vehicles::Vehicle &v );
// Rail spawn pose for spawn_on_rail: centre of the rail cell's track
// segment, yaw along the rail axis nearest player_yaw. False when the
// cell holds no rail.
bool vehicle_rail_spawn_pose( World &world, const glm::ivec3 &rail_cell, float player_yaw, glm::dvec3 *pos, float *yaw );
