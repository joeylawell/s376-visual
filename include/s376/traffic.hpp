#pragma once

#include "s376/airport.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

// Raster's 2023 KJFK_KEWR pad had MSFS-style road traffic. We drive
// instanced low-poly cars on public OSM-class roads. 980 Ti budget:
// one batched draw, ≤ kMaxTrafficCars, distance-culled.

constexpr int kMaxTrafficCars = 280;
constexpr double kTrafficCullM = 7000.0;
constexpr double kTrafficLightCullM = 11000.0;

struct TrafficCar {
    int road = 0;
    double s_m = 0;
    int dir = 1; // +1 along path, −1 opposite
    float speed_mps = 20;
    float r = 0.55f;
    float g = 0.56f;
    float b = 0.58f;
    bool visible = true;
};

struct TrafficWorld {
    std::vector<Road> roads;
    std::vector<TrafficCar> cars;
    int visible = 0;
    int lamps = 0;
};

struct TrafficMesh {
    std::vector<MeshVert> body;
    std::vector<MeshVert> lamps;
};

const char* road_class_name(RoadClass c);
RoadClass parse_osm_highway(const std::string& tags); // Secondary if not a kept class
bool osm_tags_are_road(const std::string& tags);
float class_speed_mps(RoadClass c);
float class_weight(RoadClass c); // cars per km at density 1
// Typical cobra-head spacing on a lit highway of that class (metres).
float class_lamp_spacing_m(RoadClass c);
float traffic_density(float tod_hours);
bool traffic_night(float tod_hours);

void finalize_road(Road& r);
bool sample_road(const Road& r, double s_m, double& lat, double& lon, double& hdg);

// Van Wyck, Belt Parkway, JFK terminal roads, NJ Turnpike, US-1/9.
void add_public_ny_roads(Airport& a);

void collect_roads(TrafficWorld& w, const Airport& a);
void seed_traffic(TrafficWorld& w, int max_cars = kMaxTrafficCars, std::uint32_t rng = 1);
TrafficWorld build_traffic(const std::vector<Airport>& apts, int max_cars = kMaxTrafficCars);

int step_traffic(TrafficWorld& w, double dt, float tod_hours, double eye_lat, double eye_lon,
                 double eye_alt_m);
void build_traffic_mesh(const TrafficWorld& w, float tod_hours, TrafficMesh& out);

} // namespace s376
