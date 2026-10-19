#include "s376/accuracy.hpp"

#include "s376/geo.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace s376 {
namespace {

double heading_err(double a, double b) {
    double d = a - b;
    while (d > 180.0)
        d -= 360.0;
    while (d < -180.0)
        d += 360.0;
    return std::fabs(d);
}

const Runway* find_rw(const Airport& a, const std::string& ident) {
    for (const auto& rw : a.runways) {
        if (rw.ident_le == ident || rw.ident_he == ident)
            return &rw;
    }
    return nullptr;
}

void fail(AccReport& r, const std::string& check, const std::string& detail) {
    r.fails.push_back({check, detail});
}

} // namespace

AccReport check_airport_vs_faa(const Airport& a, const FaaAirportRef& faa) {
    AccReport r;
    r.icao = a.icao;
    const double arp_m = geo::ground_distance_m(a.arp_lat, a.arp_lon, faa.arp_lat, faa.arp_lon);
    ++r.checks;
    if (arp_m > kAccEndM) {
        std::ostringstream o;
        o << "ARP " << arp_m << " m from " << official_authority(faa);
        fail(r, "arp", o.str());
    }
    for (const auto& fe : faa.ends) {
        const Runway* rw = find_rw(a, fe.ident);
        ++r.checks;
        if (!rw) {
            fail(r, "end-missing", fe.ident);
            continue;
        }
        const bool le = rw->ident_le == fe.ident;
        const double lat = le ? rw->le_lat : rw->he_lat;
        const double lon = le ? rw->le_lon : rw->he_lon;
        const double elev_ft = (le ? rw->le_elev_m : rw->he_elev_m) * geo::kMToFt;
        const double disp = le ? rw->le_displaced_ft : rw->he_displaced_ft;
        const std::string als = le ? rw->approach_le : rw->approach_he;
        const std::string papi = le ? rw->papi_le : rw->papi_he;
        const double papi_deg = le ? rw->papi_deg_le : rw->papi_deg_he;
        const double pos_m = geo::ground_distance_m(lat, lon, fe.lat, fe.lon);
        ++r.checks;
        if (pos_m > kAccEndM) {
            std::ostringstream o;
            o << fe.ident << " end " << pos_m << " m from " << official_authority(faa);
            fail(r, "end-pos", o.str());
        }
        // Heading limit is 0.1° vs the WGS84 geodesic between the two
        // surveyed FAA ends. Chart true bearings are integer / 0.1° rounded.
        const FaaEnd* other = faa_end(faa, le ? rw->ident_he : rw->ident_le);
        double exp_hdg = fe.true_brg_deg;
        if (other)
            geo::inverse_wgs84(fe.lat, fe.lon, other->lat, other->lon, &exp_hdg);
        const double hdg = le ? rw->heading_deg : rw->heading_he_deg;
        ++r.checks;
        if (heading_err(hdg, exp_hdg) > kAccHeadingDeg) {
            std::ostringstream o;
            o << fe.ident << " heading " << hdg << " vs geodesic " << exp_hdg
              << " (chart " << fe.true_brg_deg << ")";
            fail(r, "heading", o.str());
        }
        double tlat = lat, tlon = lon;
        if (fe.displaced_ft > 0.5) {
            offset_m(lat, lon, exp_hdg, fe.displaced_ft * geo::kFtToM, tlat, tlon);
            double got_lat = lat, got_lon = lon;
            if (disp > 0.5)
                offset_m(lat, lon, hdg, disp * geo::kFtToM, got_lat, got_lon);
            const double tm = geo::ground_distance_m(got_lat, got_lon, tlat, tlon);
            ++r.checks;
            if (tm > kAccThresholdM) {
                std::ostringstream o;
                o << fe.ident << " displaced threshold " << tm << " m from " << official_authority(faa);
                fail(r, "threshold", o.str());
            }
        }
        ++r.checks;
        if (als != fe.als) {
            fail(r, "als", fe.ident + " " + als + " vs " + official_authority(faa) + " " + fe.als);
        }
        ++r.checks;
        if (papi != fe.papi) {
            fail(r, "papi-side",
                 fe.ident + " " + papi + " vs " + official_authority(faa) + " " + fe.papi);
        }
        ++r.checks;
        if (std::fabs(papi_deg - fe.papi_deg) > 0.05) {
            std::ostringstream o;
            o << fe.ident << " PAPI " << papi_deg << " vs " << official_authority(faa) << " "
              << fe.papi_deg;
            fail(r, "papi-angle", o.str());
        }
        ++r.checks;
        if (std::fabs(elev_ft - fe.elev_ft) > kAccElevM * geo::kMToFt) {
            std::ostringstream o;
            o << fe.ident << " elev " << elev_ft << " ft vs " << official_authority(faa) << " "
              << fe.elev_ft;
            fail(r, "end-elev", o.str());
        }
        const double wid_ft = rw->width_m * geo::kMToFt;
        ++r.checks;
        if (std::fabs(wid_ft - fe.width_ft) > 1.0) {
            std::ostringstream o;
            o << fe.ident << " width " << wid_ft << " ft vs official " << fe.width_ft;
            fail(r, "width", o.str());
        }
        const bool reil = le ? rw->reil_le : rw->reil_he;
        ++r.checks;
        if (reil != fe.reil)
            fail(r, "reil", fe.ident + std::string(reil ? " on" : " off") + " vs official " +
                                (fe.reil ? "on" : "off"));
        const bool tdz = le ? rw->tdz_le : rw->tdz_he;
        ++r.checks;
        if (tdz != fe.tdz)
            fail(r, "tdz", fe.ident + std::string(tdz ? " on" : " off") + " vs official " +
                               (fe.tdz ? "on" : "off"));
    }
    return r;
}

double geotransform_roundtrip_m(const GeoBox& box, int w, int h, double lat, double lon) {
    if (w < 2 || h < 2 || box.east <= box.west || box.north <= box.south)
        return 1e9;
    const double u = (lon - box.west) / (box.east - box.west);
    const double v = (box.north - lat) / (box.north - box.south); // row 0 = north
    const double x = u * (w - 1);
    const double y = v * (h - 1);
    const double lon2 = box.west + (x / (w - 1)) * (box.east - box.west);
    const double lat2 = box.north - (y / (h - 1)) * (box.north - box.south);
    return geo::ground_distance_m(lat, lon, lat2, lon2);
}

bool imagery_aligned_to_runways(const GeoBox& box, int w, int h, const Airport& a, double tol_m,
                                AccReport* out) {
    bool ok = true;
    auto one = [&](double lat, double lon, const char* label) {
        const double m = geotransform_roundtrip_m(box, w, h, lat, lon);
        if (out)
            ++out->checks;
        if (m > tol_m) {
            ok = false;
            if (out) {
                std::ostringstream o;
                o << label << " imagery round-trip " << m << " m";
                out->fails.push_back({"imagery", o.str()});
            }
        }
    };
    one(a.arp_lat, a.arp_lon, "ARP");
    for (const auto& rw : a.runways) {
        one(rw.le_lat, rw.le_lon, rw.ident_le.c_str());
        one(rw.he_lat, rw.he_lon, rw.ident_he.c_str());
    }
    return ok;
}

bool elev_matches_faa(const DemGrid& dem, double lat, double lon, double faa_elev_m, double tol_m,
                      double* got_m) {
    double z = 0;
    if (!dem.sample_m(lat, lon, z))
        return false;
    if (got_m)
        *got_m = z;
    return std::fabs(z - faa_elev_m) <= tol_m;
}

bool box_contains(const GeoBox& b, double lat, double lon, double pad_deg) {
    return lat >= b.south - pad_deg && lat <= b.north + pad_deg && lon >= b.west - pad_deg &&
           lon <= b.east + pad_deg;
}

bool highest_res_covers_airport(const GeoBox& lod0, const Airport& a, AccReport* out) {
    bool ok = true;
    auto one = [&](double lat, double lon, const std::string& name) {
        if (out)
            ++out->checks;
        if (!box_contains(lod0, lat, lon, 0)) {
            ok = false;
            if (out)
                out->fails.push_back({"lod0-cover", name + " outside highest-res layer"});
        }
    };
    one(a.arp_lat, a.arp_lon, "ARP");
    for (const auto& rw : a.runways) {
        one(rw.le_lat, rw.le_lon, rw.ident_le);
        one(rw.he_lat, rw.he_lon, rw.ident_he);
    }
    return ok;
}

bool highest_res_covers_tiles(const std::vector<CacheTileRef>& tiles, const Airport& a,
                              AccReport* out) {
    auto covered = [&](double lat, double lon) {
        for (const auto& t : tiles) {
            if (t.lod != 0)
                continue;
            const GeoBox b{t.west, t.south, t.east, t.north};
            if (box_contains(b, lat, lon, 0))
                return true;
        }
        return false;
    };
    bool any = false;
    for (const auto& t : tiles) {
        if (t.lod == 0)
            any = true;
    }
    if (!any) {
        if (out)
            out->fails.push_back({"lod0-cover", "no lod0 tiles"});
        return false;
    }
    bool ok = true;
    auto one = [&](double lat, double lon, const std::string& name) {
        if (out)
            ++out->checks;
        if (!covered(lat, lon)) {
            ok = false;
            if (out)
                out->fails.push_back({"lod0-cover", name + " outside highest-res layer"});
        }
    };
    one(a.arp_lat, a.arp_lon, "ARP");
    for (const auto& rw : a.runways) {
        one(rw.le_lat, rw.le_lon, rw.ident_le);
        one(rw.he_lat, rw.he_lon, rw.ident_he);
    }
    return ok;
}

bool sample_cache_elev_m(const std::string& cache_dir, double lat, double lon, double& z_m) {
    const auto tiles = scan_cache_tiles(cache_dir);
    const CacheTileRef* best = nullptr;
    for (const auto& t : tiles) {
        const GeoBox b{t.west, t.south, t.east, t.north};
        if (!box_contains(b, lat, lon, 0))
            continue;
        if (!best || t.lod < best->lod || (t.lod == best->lod && t.dem_w * t.dem_h > best->dem_w * best->dem_h))
            best = &t;
    }
    if (best && DemGrid::sample_file_m(best->path + "/elev.s376dem", lat, lon, z_m))
        return true;
    return DemGrid::sample_file_m(cache_dir + "/elev.s376dem", lat, lon, z_m);
}

std::string format_acc_report(const AccReport& r) {
    std::ostringstream o;
    o << r.icao << " accuracy  " << r.checks << " checks  " << r.fails.size() << " fail\n";
    for (const auto& f : r.fails)
        o << "  FAIL " << f.check << "  " << f.detail << "\n";
    return o.str();
}

} // namespace s376
