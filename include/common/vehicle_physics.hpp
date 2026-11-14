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

// Exact oriented-hull collision (vehicle_hull.cpp) for WATER vehicles:
// the hull's true footprint — a def.length x def.width rectangle rotated
// by yaw, extruded def.height — SAT-tested against terrain boxes in 2D
// (axes: world x, world z, hull forward, hull side).
//
// vehicle_hull_penetration: deepest minimum-translation depth of any
// overlapping terrain box (0 = clear by more than the contact skin).
double vehicle_hull_penetration( World &world, const VehicleTypeDef &def, const glm::dvec3 &pos, float yaw );
// Pushes the hull out of terrain along the deepest overlaps' MTVs (up to
// 4 passes, total push capped at push_cap). Returns the number of
// contact normals written (the velocity component into each should be
// removed by the caller to get wall sliding); applied_push gets the
// summed push. A legacy/embedded hull escapes over a few ticks instead
// of teleporting.
int vehicle_hull_depenetrate( World &world, const VehicleTypeDef &def, glm::dvec3 &pos, float yaw, double push_cap,
                              glm::dvec2 *applied_push, glm::dvec2 normals[ 4 ] );
// Vertical clamp for the exact footprint: only terrain boxes whose XZ
// rect the hull strictly overlaps participate. Returns the applied Y
// delta; `standing` reports a downward clamp (resting on the ground).
double vehicle_hull_move_y( World &world, const VehicleTypeDef &def, glm::dvec3 &pos, float yaw, double dy, bool *standing );
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
