#include "s376/region.hpp"

#include "s376/airport.hpp"
#include "s376/geo.hpp"
#include "s376/imagery.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace s376 {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

std::string icao_upper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == '[' || c == ']' || c == '"') {
            if (!cur.empty()) {
                out.push_back(trim(cur));
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    cur = trim(cur);
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

} // namespace

RegionPlan region_plan_for_icao(const std::string& icao) {
    const AirportRef apt = lookup_airport_ref(icao);
    RegionPlan p;
    p.name = apt.icao;
    p.icao = apt.icao;
    p.airports = {apt.icao};
    p.radius_km = kFarFieldKm;
    const double rlat = p.radius_km / 111.32;
    const double rlon = rlat / std::max(0.2, std::cos(geo::deg2rad(apt.lat)));
    p.south = apt.lat - rlat;
    p.north = apt.lat + rlat;
    p.west = apt.lon - rlon;
    p.east = apt.lon + rlon;
    p.dem = apt.conus ? "3dep" : "glo30";
    if (const auto* src = airport_imagery(apt.icao)) {
        p.imagery = src->public_kind;
        p.near_m = src->public_m;
        p.policy = imagery_policy_name(src->policy);
        p.source = std::string(src->public_label) + " — " + src->notes;
        p.local_import = src->policy == ImageryPolicy::RasterLocal ||
                         src->policy == ImageryPolicy::Either;
        if (src->policy == ImageryPolicy::RasterLocal)
            p.imagery = "raster-local";
    } else {
        p.imagery = apt.conus ? "naip" : "s2";
        p.policy = "public";
    }
    p.near_dem_m = apt.conus ? 1.0 : 30.0;
    p.mid_m = p.near_m > 0 ? p.near_m * 4.0 : 4.0;
    p.far_m = p.near_m > 0 ? p.near_m * 16.0 : 20.0;
    return p;
}

RegionPlan kjfk_nearfield_03m_plan() {
    RegionPlan p = region_plan_for_icao("KJFK");
    p.name = "KJFK-near";
    p.near_m = 0.30;
    p.near_dem_m = 1.0;
    p.near_km = 3.0;
    p.approach_nm = 3.0;
    p.radius_km = 8.0;
    p.mid_km = 8.0;
    p.imagery = "nys";
    p.dem = "3dep";
    p.policy = "public";
    p.source = "NYS ITS 2024 / NYC_Orthos_2024 15–30 cm within ~3 km of each runway. "
               "Plan only — do not change the in-flight hangar 1 m fetch.";
    const double rlat = 3.0 / 111.32;
    const double rlon = rlat / std::max(0.2, std::cos(geo::deg2rad(40.639928)));
    p.south = 40.639928 - rlat;
    p.north = 40.639928 + rlat;
    p.west = -73.778692 - rlon;
    p.east = -73.778692 + rlon;
    try {
        const Airport a = airport_by_icao("KJFK");
        for (const auto& rw : a.runways) {
            const double pts[4][2] = {{rw.le_lat, rw.le_lon}, {rw.he_lat, rw.he_lon}};
            for (const auto& pt : pts) {
                p.south = std::min(p.south, pt[0] - rlat);
                p.north = std::max(p.north, pt[0] + rlat);
                p.west = std::min(p.west, pt[1] - rlon);
                p.east = std::max(p.east, pt[1] + rlon);
            }
        }
    } catch (...) {
    }
    return p;
}

RegionPlan load_region_plan(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot read region plan " + path);
    RegionPlan p;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty() || line.front() == '[')
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = unquote(line.substr(eq + 1));
        if (k == "name")
            p.name = v;
        else if (k == "icao")
            p.icao = icao_upper(v);
        else if (k == "south")
            p.south = std::stod(v);
        else if (k == "north")
            p.north = std::stod(v);
        else if (k == "west")
            p.west = std::stod(v);
        else if (k == "east")
            p.east = std::stod(v);
        else if (k == "near_m")
            p.near_m = std::stod(v);
        else if (k == "near_dem_m")
            p.near_dem_m = std::stod(v);
        else if (k == "mid_m")
            p.mid_m = std::stod(v);
        else if (k == "far_m")
            p.far_m = std::stod(v);
        else if (k == "near_km")
            p.near_km = std::stod(v);
        else if (k == "mid_km")
            p.mid_km = std::stod(v);
        else if (k == "approach_nm")
            p.approach_nm = std::stod(v);
        else if (k == "radius_km")
            p.radius_km = std::stod(v);
        else if (k == "imagery")
            p.imagery = v;
        else if (k == "dem")
            p.dem = v;
        else if (k == "policy")
            p.policy = v;
        else if (k == "source")
            p.source = v;
        else if (k == "airports")
            p.airports = split_list(v);
        else if (k == "local_import")
            p.local_import = (v == "true" || v == "1");
    }
    if (p.icao.empty() && !p.airports.empty())
        p.icao = icao_upper(p.airports.front());
    if (p.name.empty())
        p.name = p.icao;
    if (p.airports.empty() && !p.icao.empty())
        p.airports = {p.icao};
    p.local_import = p.local_import || p.policy == "raster-local" || p.imagery == "raster-local";
    return p;
}

bool write_region_plan(const std::string& path, const RegionPlan& p) {
    std::ofstream o(path);
    if (!o)
        return false;
    o << "# Fetch / import plan. No imagery. --plan only.\n";
    o << "name = \"" << p.name << "\"\n";
    o << "icao = \"" << p.icao << "\"\n";
    o << "south = " << p.south << "\n";
    o << "north = " << p.north << "\n";
    o << "west = " << p.west << "\n";
    o << "east = " << p.east << "\n";
    o << "near_m = " << p.near_m << "\n";
    o << "near_dem_m = " << p.near_dem_m << "\n";
    o << "mid_m = " << p.mid_m << "\n";
    o << "far_m = " << p.far_m << "\n";
    o << "near_km = " << p.near_km << "\n";
    o << "mid_km = " << p.mid_km << "\n";
    o << "approach_nm = " << p.approach_nm << "\n";
    o << "radius_km = " << p.radius_km << "\n";
    o << "imagery = \"" << p.imagery << "\"\n";
    o << "dem = \"" << p.dem << "\"\n";
    o << "policy = \"" << p.policy << "\"\n";
    o << "local_import = " << (p.local_import ? "true" : "false") << "\n";
    o << "source = \"" << p.source << "\"\n";
    o << "airports = [";
    for (std::size_t i = 0; i < p.airports.size(); ++i) {
        if (i)
            o << ", ";
        o << "\"" << p.airports[i] << "\"";
    }
    o << "]\n";
    return static_cast<bool>(o);
}

std::string format_region_plan(const RegionPlan& p) {
    std::ostringstream o;
    o << p.icao << "  " << p.policy << "  img=" << p.imagery << "  dem=" << p.dem
      << "  near=" << p.near_m << "m  r=" << p.radius_km << "km";
    if (p.local_import)
        o << "  import-raster";
    o << "\n  box S " << p.south << " N " << p.north << " W " << p.west << " E " << p.east << "\n";
    if (!p.source.empty())
        o << "  " << p.source << "\n";
    return o.str();
}

FetchRequest fetch_request_from_region(const RegionPlan& p, const std::string& out_dir) {
    FetchRequest r;
    r.icao = p.icao.empty() ? p.name : p.icao;
    r.radius_km = p.radius_km;
    r.dem = p.dem.empty() ? "auto" : p.dem;
    r.imagery = p.imagery.empty() ? "auto" : p.imagery;
    r.out_dir = out_dir;
    r.near_m = p.near_m;
    r.near_dem_m = p.near_dem_m;
    r.mid_m = p.mid_m;
    r.far_m = p.far_m;
    r.near_radius_km = p.near_km;
    r.mid_radius_km = p.mid_km;
    r.approach_nm = p.approach_nm;
    r.plan_only = true;
    r.dry_run = true;
    return r;
}

std::vector<std::string> raster_airport_icaos() {
    return {"KJFK", "KEWR", "EDDF", "EGLL", "KBOS", "KIAD", "KLAS", "KLAX", "KMCO", "KMIA",
            "KRNO", "KSEA", "KSFO", "LEBL", "SEQM", "SKBO", "SKRG", "VNKT"};
}

std::string region_plan_path(const std::string& icao, const std::string& data_root) {
    std::string id = icao;
    for (char& c : id)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return data_root + "/" + id + ".toml";
}

bool write_fetch_plan_toml(const std::string& path, const FetchPlan& plan, const FetchRequest& req) {
    std::ofstream o(path);
    if (!o)
        return false;
    o << "# Generated by s376-cache --plan. No imagery downloaded.\n";
    o << "icao = \"" << plan.icao << "\"\n";
    o << "dem = \"" << plan.dem_kind << "\"\n";
    o << "imagery = \"" << plan.img_kind << "\"\n";
    o << "covers_faa = " << (plan.covers_faa ? "true" : "false") << "\n";
    o << "tiles = " << plan.tiles.size() << "\n";
    o << "near_m = " << req.near_m << "\n";
    o << "near_km = " << req.near_radius_km << "\n";
    o << "approach_nm = " << req.approach_nm << "\n";
    o << "radius_km = " << req.radius_km << "\n";
    o << "est_packed_bytes = " << plan.estimate.est_packed_bytes << "\n";
    for (const auto& t : plan.tiles) {
        o << "[[tiles]]\n";
        o << "lod = " << t.lod << "\n";
        o << "ix = " << t.ix << "\n";
        o << "iy = " << t.iy << "\n";
        o << "west = " << t.box.west << "\n";
        o << "south = " << t.box.south << "\n";
        o << "east = " << t.box.east << "\n";
        o << "north = " << t.box.north << "\n";
        o << "img_w = " << t.img_w << "\n";
        o << "img_h = " << t.img_h << "\n";
    }
    return static_cast<bool>(o);
}

} // namespace s376
