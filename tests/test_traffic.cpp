#include "s376/airport.hpp"
#include "s376/traffic.hpp"

#include <cmath>
#include <iostream>
#include <string>

extern int g_fails;
extern int g_passes;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n";          \
        }                                                                                          \
    } while (0)
#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        const double _a = static_cast<double>(a);                                                  \
        const double _b = static_cast<double>(b);                                                  \
        if (std::fabs(_a - _b) <= (eps)) {                                                         \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a << " "  \
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

void test_traffic() {
    CHECK(s376::traffic_density(7.5f) > s376::traffic_density(3.0f));
    CHECK(s376::traffic_density(17.0f) > s376::traffic_density(23.0f));
    CHECK(s376::traffic_density(12.0f) > s376::traffic_density(2.0f));
    CHECK(s376::traffic_night(2.0f) && s376::traffic_night(21.0f));
    CHECK(!s376::traffic_night(12.0f));
    CHECK(s376::class_weight(s376::RoadClass::Motorway) >
          s376::class_weight(s376::RoadClass::Secondary));
    CHECK(s376::class_speed_mps(s376::RoadClass::Motorway) >
          s376::class_speed_mps(s376::RoadClass::Primary));
    CHECK(s376::class_lamp_spacing_m(s376::RoadClass::Motorway) >
          s376::class_lamp_spacing_m(s376::RoadClass::Secondary));
    CHECK(s376::class_lamp_spacing_m(s376::RoadClass::Primary) >= 30.f);
    CHECK(std::string(s376::road_class_name(s376::RoadClass::Trunk)) == "trunk");

    const std::string mw = "{\"tags\":{\"highway\":\"motorway\"}}";
    const std::string link = "{\"tags\":{\"highway\":\"motorway_link\"}}";
    const std::string taxi = "{\"tags\":{\"aeroway\":\"taxiway\"}}";
    CHECK(s376::osm_tags_are_road(mw));
    CHECK(!s376::osm_tags_are_road(link));
    CHECK(!s376::osm_tags_are_road(taxi));
    CHECK(s376::parse_osm_highway(mw) == s376::RoadClass::Motorway);

    const auto jfk = s376::kjfk_public();
    const auto ewr = s376::kewr_public();
    bool van = false, belt = false, term = false, njtp = false;
    for (const auto& r : jfk.roads) {
        if (r.name == "Van Wyck")
            van = r.cls == s376::RoadClass::Motorway && r.length_m > 2000;
        if (r.name == "Belt Parkway")
            belt = r.cls == s376::RoadClass::Trunk && r.length_m > 2000;
        if (r.name == "JFK Terminal")
            term = r.path.size() >= 4;
    }
    for (const auto& r : ewr.roads) {
        if (r.name == "NJ Turnpike")
            njtp = r.cls == s376::RoadClass::Motorway && r.length_m > 2000;
    }
    CHECK(van && belt && term && njtp);
    CHECK(jfk.roads.size() >= 4);
    CHECK(ewr.roads.size() >= 2);

    auto world = s376::build_traffic({jfk, ewr}, 180);
    CHECK(world.roads.size() >= 6);
    CHECK(static_cast<int>(world.cars.size()) >= 40);
    CHECK(static_cast<int>(world.cars.size()) <= s376::kMaxTrafficCars);

    double lat0 = 0, lon0 = 0, h0 = 0;
    CHECK(s376::sample_road(world.roads[0], 100.0, lat0, lon0, h0));
    CHECK(std::fabs(lat0) > 1.0);

    const int day_n =
        s376::step_traffic(world, 1.0 / 60.0, 8.0f, 40.64, -73.78, 50.0);
    CHECK(day_n > 10);
    const auto s0 = world.cars[0].s_m;
    s376::step_traffic(world, 1.0, 8.0f, 40.64, -73.78, 50.0);
    CHECK(std::fabs(world.cars[0].s_m - s0) > 1.0);

    auto night_w = world;
    const int night_n =
        s376::step_traffic(night_w, 1.0 / 60.0, 2.0f, 40.64, -73.78, 50.0);
    CHECK(night_n < day_n);

    const int far =
        s376::step_traffic(world, 0.0, 8.0f, 47.45, -122.31, 100.0);
    CHECK(far < day_n);

    s376::TrafficMesh mesh;
    s376::step_traffic(world, 0.0, 2.0f, 40.64, -73.78, 50.0);
    s376::build_traffic_mesh(world, 2.0f, mesh);
    CHECK(mesh.body.size() >= 36);
    CHECK(mesh.body.size() % 3 == 0);
    CHECK(mesh.lamps.size() >= 24);
    s376::build_traffic_mesh(world, 12.0f, mesh);
    CHECK(mesh.lamps.empty());
}
