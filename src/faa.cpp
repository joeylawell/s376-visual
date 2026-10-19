#include "s376/faa.hpp"

#include "s376/geo.hpp"

#include <cctype>
#include <string>

namespace s376 {
namespace {

constexpr const char* kNasrSrc =
    "FAA Chart Supplement / NFDC NASR (28-day family), cross-checked OurAirports";
constexpr const char* kNasrEff = "2026-10-01";
constexpr const char* kNasrRet = "2026-10-18";

std::string up(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

FaaEnd end(const char* icao, const char* ident, double lat, double lon, double elev_ft,
           double brg, double len_ft, double wid_ft, double disp_ft, const char* als,
           const char* papi, double papi_deg, bool reil, bool tdz, bool emas, double emas_l,
           double emas_w) {
    FaaEnd e;
    e.icao = icao;
    e.ident = ident;
    e.lat = lat;
    e.lon = lon;
    e.elev_ft = elev_ft;
    e.true_brg_deg = brg;
    e.length_ft = len_ft;
    e.width_ft = wid_ft;
    e.displaced_ft = disp_ft;
    e.als = als;
    e.papi = papi;
    e.papi_deg = papi_deg;
    e.reil = reil;
    e.tdz = tdz;
    e.emas = emas;
    e.emas_len_ft = emas_l;
    e.emas_wid_ft = emas_w;
    e.source = kNasrSrc;
    e.effective = kNasrEff;
    e.retrieved = kNasrRet;
    return e;
}

void stamp(FaaAirportRef& a, const char* src, const char* eff, const char* ret) {
    a.source = src;
    a.effective = eff;
    a.retrieved = ret;
    for (auto& e : a.ends) {
        e.source = src;
        e.effective = eff;
        e.retrieved = ret;
    }
}

FaaAirportRef& once_ref(FaaAirportRef& a, bool& once) {
    once = true;
    return a;
}

} // namespace

const FaaAirportRef& faa_kjfk() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once = true;
    a.icao = "KJFK";
    a.name = "John F Kennedy International";
    a.arp_lat = geo::dms_to_deg(40, 38, 23.741);
    a.arp_lon = geo::dms_to_deg(73, 46, 43.292, true);
    a.elev_ft = 13.0;
    a.source = "FAA AIP AD 2 KJFK (faa.gov) + NFDC, cross-checked OurAirports";
    a.effective = kNasrEff;
    a.retrieved = kNasrRet;
    // Physical ends from FAA AIP AD 2.12. Displaced / ALS / PAPI from
    // AD 2.14 + chart supplement. EMAS from AIP general remarks (DER 4R / 22L).
    // True bearings are WGS-84 geodesics between the surveyed ends (NASR
    // TRUE_ALIGNMENT is the integer rounding). VGSI TCH from NASR
    // THR_CROSSING_HGT; ILS TCH from the d-TPP plates.
    a.ends.push_back(end("KJFK", "04L", geo::dms_to_deg(40, 37, 19.2754),
                         geo::dms_to_deg(73, 47, 8.1029, true), 11.9, 30.7583, 12079, 200, 460,
                         "NONE", "P4L", 3.00, false, true, false, 0, 0));
    a.ends.back().vgsi_tch_ft = 75;
    a.ends.back().ils_tch_ft = 0;
    a.ends.push_back(end("KJFK", "22R", geo::dms_to_deg(40, 39, 1.8338),
                         geo::dms_to_deg(73, 45, 47.9596, true), 12.5, 210.7728, 12079, 200, 3424,
                         "NONE", "P4L", 3.00, false, false, false, 0, 0));
    a.ends.back().vgsi_tch_ft = 77;
    a.ends.back().papi_usable_right_deg = 8; // unusable beyond 8° right of CL
    a.ends.push_back(end("KJFK", "04R", geo::dms_to_deg(40, 37, 31.5418),
                         geo::dms_to_deg(73, 46, 13.2441, true), 11.9, 30.7680, 8400, 200, 0,
                         "ALSF-2", "P4L", 3.00, false, true, true, 393, 226));
    a.ends.back().vgsi_tch_ft = 69;
    a.ends.back().ils_tch_ft = 53;
    a.ends.push_back(end("KJFK", "22L", geo::dms_to_deg(40, 38, 42.8531),
                         geo::dms_to_deg(73, 45, 17.5027, true), 11.9, 210.7781, 8400, 200, 0,
                         "ALSF-2", "P4L", 3.00, false, true, true, 405, 226));
    a.ends.back().vgsi_tch_ft = 66;
    a.ends.back().papi_rotate_deg = -4; // horizontal offset 4° left
    a.ends.push_back(end("KJFK", "13L", geo::dms_to_deg(40, 39, 27.952),
                         geo::dms_to_deg(73, 47, 24.8606, true), 12.9, 120.7546, 10000, 200, 907,
                         "ALSF-2", "P4L", 3.00, false, true, false, 0, 0));
    a.ends.back().vgsi_tch_ft = 74;
    a.ends.back().ils_tch_ft = 58;
    a.ends.back().rlls_lead_in = true;
    a.ends.back().papi_rotate_deg = 10;      // rotated 10° right of CL
    a.ends.back().papi_usable_left_deg = 0; // unusable left of CL
    a.ends.push_back(end("KJFK", "31R", geo::dms_to_deg(40, 38, 37.4085),
                         geo::dms_to_deg(73, 45, 33.3818, true), 12.6, 300.7748, 10000, 200, 1027,
                         "MALSR", "P4R", 3.00, false, true, false, 0, 0));
    a.ends.back().vgsi_tch_ft = 54;
    a.ends.push_back(end("KJFK", "13R", geo::dms_to_deg(40, 38, 54.1008),
                         geo::dms_to_deg(73, 49, 0.173, true), 12.5, 120.7382, 14511, 200, 2044,
                         "RLLS", "P4L+P4L", 3.00, false, false, false, 0, 0));
    a.ends.back().vgsi_tch_ft = 79;
    a.ends.back().rlls_lead_in = true;
    a.ends.back().papi_rotate_deg = -22; // first P4L offset 22° left
    a.ends.push_back(end("KJFK", "31L", geo::dms_to_deg(40, 37, 40.7799),
                         geo::dms_to_deg(73, 46, 18.4107, true), 12.6, 300.7674, 14511, 200, 3264,
                         "NONE", "P4L", 3.00, false, false, false, 0, 0));
    a.ends.back().vgsi_tch_ft = 74;
    stamp(a, a.source.c_str(), a.effective.c_str(), a.retrieved.c_str());
    return a;
}

const FaaAirportRef& faa_kewr() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once = true;
    a.icao = "KEWR";
    a.name = "Newark Liberty International";
    a.arp_lat = geo::dms_to_deg(40, 41, 33.0); // N40°41.55′
    a.arp_lon = geo::dms_to_deg(74, 10, 7.2, true);
    a.elev_ft = 18.0;
    a.source = "FAA NFDC airportDisplay KEWR + AIP AD 2 + chart supplement, "
               "cross-checked AirNav/OurAirports";
    a.effective = kNasrEff;
    a.retrieved = kNasrRet;
    a.ends.push_back(end("KEWR", "04L", geo::dms_to_deg(40, 40, 31.3731),
                         geo::dms_to_deg(74, 10, 46.0156, true), 9.8, 26, 11000, 150, 2540.6,
                         "MALSR", "P4L", 3.10, false, true, false, 0, 0));
    a.ends.push_back(end("KEWR", "22R", geo::dms_to_deg(40, 42, 9.2158),
                         geo::dms_to_deg(74, 9, 43.8183, true), 8.5, 206, 11000, 150, 1440.9,
                         "MALSR", "P4L", 3.10, true, true, false, 0, 0));
    a.ends.push_back(end("KEWR", "04R", geo::dms_to_deg(40, 40, 39.3027),
                         geo::dms_to_deg(74, 10, 27.2825, true), 11.0, 26, 9999, 150, 1190.2,
                         "ALSF-2", "P4L", 2.95, false, true, false, 0, 0));
    a.ends.push_back(end("KEWR", "22L", geo::dms_to_deg(40, 42, 8.2401),
                         geo::dms_to_deg(74, 9, 30.7356, true), 9.0, 206, 9999, 150, 1794.1,
                         "ALSF-2", "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KEWR", "11", geo::dms_to_deg(40, 42, 10.0957),
                         geo::dms_to_deg(74, 10, 50.5467, true), 17.5, 95, 6725, 150, 0, "NONE",
                         "P4R", 3.00, true, true, true, 0, 0));
    a.ends.push_back(end("KEWR", "29", geo::dms_to_deg(40, 42, 4.3179),
                         geo::dms_to_deg(74, 9, 23.5566, true), 9.9, 275, 6725, 150, 224, "NONE",
                         "P4R", 3.00, true, true, true, 0, 0));
    stamp(a, a.source.c_str(), a.effective.c_str(), a.retrieved.c_str());
    return a;
}

const FaaAirportRef& faa_ksea() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KSEA";
    a.name = "Seattle-Tacoma International";
    a.arp_lat = 47.0 + 26.99 / 60.0;   // CS N47°26.99′
    a.arp_lon = -(122.0 + 18.71 / 60.0);
    a.elev_ft = 432.0;
    a.ends.push_back(end("KSEA", "16L", 47.463799, -122.307999, 432, 180, 11901, 150, 0, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KSEA", "34R", 47.431198, -122.307999, 347, 360, 11901, 150, 0, "MALSR",
                         "P4L", 2.75, false, true, false, 0, 0));
    a.ends.push_back(end("KSEA", "16C", 47.463799, -122.310997, 430, 180, 9426, 150, 0, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KSEA", "34C", 47.438000, -122.310997, 363, 360, 9426, 150, 0, "MALSR",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KSEA", "16R", 47.463799, -122.318001, 430, 180, 8500, 150, 0, "ALSF-2",
                         "P4R", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KSEA", "34L", 47.440498, -122.318001, 363, 360, 8500, 150, 0, "MALSR",
                         "P4L", 3.00, false, false, false, 0, 0));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_ksfo() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KSFO";
    a.name = "San Francisco International";
    a.arp_lat = 37.0 + 37.13 / 60.0;
    a.arp_lon = -(122.0 + 22.53 / 60.0);
    a.elev_ft = 13.0;
    a.ends.push_back(end("KSFO", "10L", 37.628742, -122.39341, 5, 118, 11870, 200, 0, "NONE", "P4L",
                         3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KSFO", "28R", 37.613538, -122.35716, 13, 298, 11870, 200, 300, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KSFO", "10R", 37.626298, -122.393124, 6, 118, 11381, 200, 0, "NONE", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KSFO", "28L", 37.61172, -122.358367, 13, 298, 11381, 200, 300, "MALSR",
                         "P4L", 2.85, false, false, false, 0, 0));
    a.ends.push_back(end("KSFO", "01L", 37.607898, -122.38295, 10, 28, 7650, 200, 640, "NONE", "NONE",
                         3.00, true, false, true, 413, 227));
    a.ends.push_back(end("KSFO", "19R", 37.626476, -122.37063, 9, 208, 7650, 200, 0, "NONE", "P4L",
                         3.15, false, false, true, 437, 227));
    a.ends.push_back(end("KSFO", "01R", 37.606333, -122.381061, 12, 28, 8650, 200, 560, "NONE", "NONE",
                         3.00, true, false, true, 409, 227));
    a.ends.push_back(end("KSFO", "19L", 37.627346, -122.367124, 10, 208, 8650, 200, 0, "MALSF", "P4L",
                         3.00, false, true, true, 373, 227));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_klas() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KLAS";
    a.name = "Harry Reid International";
    a.arp_lat = 36.0 + 4.82 / 60.0;
    a.arp_lon = -(115.0 + 9.15 / 60.0);
    a.elev_ft = 2183.0;
    a.ends.push_back(end("KLAS", "08L", 36.076333, -115.171333, 2155, 90, 14835, 150, 2459, "NONE",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KLAS", "26R", 36.076333, -115.121000, 2044, 270, 14835, 150, 1399, "MALS",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KLAS", "08R", 36.073601, -115.161003, 2157, 90, 10526, 150, 0, "NONE",
                         "P4L", 3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KLAS", "26L", 36.073700, -115.126000, 2048, 270, 10526, 150, 0, "MALSF",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KLAS", "01L", 36.075298, -115.169998, 2176, 25, 9770, 150, 586, "MALSF",
                         "P4L", 3.40, false, false, false, 0, 0));
    a.ends.push_back(end("KLAS", "19R", 36.097698, -115.157997, 2083, 205, 9770, 150, 783, "NONE",
                         "P4L", 3.04, true, false, false, 0, 0));
    a.ends.push_back(end("KLAS", "01R", 36.074167, -115.167500, 2170, 25, 9769, 150, 490, "NONE",
                         "P4L", 3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KLAS", "19L", 36.098500, -115.153500, 2080, 205, 9769, 150, 877, "NONE",
                         "P4L", 3.00, true, false, false, 0, 0));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_klax() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KLAX";
    a.name = "Los Angeles International";
    a.arp_lat = 33.0 + 56.55 / 60.0;
    a.arp_lon = -(118.0 + 24.48 / 60.0);
    a.elev_ft = 128.0;
    a.ends.push_back(end("KLAX", "06L", 33.949119, -118.431187, 112, 83, 8926, 150, 0, "MALSR",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KLAX", "24R", 33.952112, -118.401954, 117, 263, 8926, 150, 0, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KLAX", "06R", 33.946820, -118.434693, 108, 83, 10885, 150, 537, "MALSR",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KLAX", "24L", 33.950470, -118.399063, 111, 263, 10885, 150, 801, "MALSR",
                         "P4R", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KLAX", "07L", 33.935556, -118.422089, 119, 83, 12923, 150, 832, "MALSR",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KLAX", "25R", 33.939881, -118.379794, 94, 263, 12923, 150, 957, "MALSR",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KLAX", "07R", 33.933659, -118.419074, 118, 83, 11095, 200, 0, "MALSR",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KLAX", "25L", 33.937371, -118.382700, 95, 263, 11095, 200, 0, "ALSF-2",
                         "P4R", 3.00, false, true, false, 0, 0));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_kbos() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KBOS";
    a.name = "Boston Logan International";
    a.arp_lat = 42.0 + 21.78 / 60.0;
    a.arp_lon = -(71.0 + 0.38 / 60.0);
    a.elev_ft = 19.0;
    a.ends.push_back(end("KBOS", "04L", 42.357997, -71.014344, 14, 20, 7864, 150, 0, "NONE", "P4L",
                         3.10, true, false, true, 200, 226));
    a.ends.push_back(end("KBOS", "22R", 42.378322, -71.004511, 15, 200, 7864, 150, 819, "NONE", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KBOS", "04R", 42.351064, -71.011801, 18, 20, 10006, 150, 1155, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KBOS", "22L", 42.376923, -70.999288, 16, 200, 10006, 150, 1199, "MALSF",
                         "P4R", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KBOS", "09", 42.355801, -71.012901, 17, 77, 7001, 150, 0, "NONE", "NONE",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KBOS", "27", 42.360199, -70.987701, 15, 257, 7001, 150, 0, "NONE", "P4L",
                         3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KBOS", "15R", 42.374298, -71.017899, 17, 135, 10083, 150, 882, "MALSR",
                         "P4L", 3.00, false, true, true, 200, 226));
    a.ends.push_back(end("KBOS", "33L", 42.354599, -70.991600, 15, 315, 10083, 150, 0, "ALSF-2",
                         "P4R", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KBOS", "15L", 42.373600, -71.009102, 15, 135, 2557, 100, 0, "NONE", "NONE",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KBOS", "33R", 42.368599, -71.002502, 14, 315, 2557, 100, 0, "NONE", "P4R",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KBOS", "14", 42.356602, -71.023300, 17, 125, 5000, 100, 0, "NONE", "NONE",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KBOS", "32", 42.348598, -71.008202, 20, 305, 5000, 100, 0, "NONE", "P4L",
                         3.00, true, false, false, 0, 0));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_kiad() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KIAD";
    a.name = "Washington Dulles International";
    a.arp_lat = 38.0 + 56.85 / 60.0;
    a.arp_lon = -(77.0 + 27.60 / 60.0);
    a.elev_ft = 312.0;
    a.ends.push_back(end("KIAD", "01C", 38.939098, -77.459801, 286, 1, 11500, 150, 0, "MALSR", "P4L",
                         3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KIAD", "19C", 38.970600, -77.459297, 269, 181, 11500, 150, 0, "ALSF-2",
                         "P4R", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KIAD", "01R", 38.923801, -77.436401, 312, 0.7, 11500, 150, 0, "ALSF-2",
                         "P4R", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KIAD", "19L", 38.955299, -77.435997, 293, 180.7, 11500, 150, 0, "ALSF-2",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KIAD", "12", 38.943802, -77.490402, 310, 110.6, 10501, 150, 0, "MALSR",
                         "P4R", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KIAD", "30", 38.933601, -77.455902, 288, 290.6, 10501, 150, 0, "NONE",
                         "P4L", 3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KIAD", "01L", 38.945000, -77.474820, 286, 1, 9400, 150, 0, "ALSF-2", "P4L",
                         3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KIAD", "19R", 38.970780, -77.474450, 269, 181, 9400, 150, 0, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_kmco() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KMCO";
    a.name = "Orlando International";
    a.arp_lat = 28.0 + 25.76 / 60.0;
    a.arp_lon = -(81.0 + 18.54 / 60.0);
    a.elev_ft = 96.0;
    a.ends.push_back(end("KMCO", "18L", 28.448299, -81.322304, 93, 179, 12005, 200, 0, "NONE", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KMCO", "36R", 28.415300, -81.321999, 91, 359, 12005, 200, 0, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KMCO", "18R", 28.448299, -81.327003, 93, 179, 12004, 200, 0, "MALSR",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KMCO", "36L", 28.415300, -81.326599, 91, 359, 12004, 200, 0, "NONE", "P4L",
                         3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KMCO", "17R", 28.435600, -81.295898, 90, 179, 10000, 150, 0, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KMCO", "35L", 28.408100, -81.295601, 87, 359, 10000, 150, 0, "ALSF-2",
                         "P4R", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KMCO", "17L", 28.443701, -81.282600, 90, 179.5, 9001, 150, 0, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KMCO", "35R", 28.418900, -81.282303, 90, 359.5, 9001, 150, 0, "ALSF-2",
                         "P4R", 3.00, false, true, false, 0, 0));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_kmia() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KMIA";
    a.name = "Miami International";
    a.arp_lat = 25.0 + 47.72 / 60.0;
    a.arp_lon = -(80.0 + 17.41 / 60.0);
    a.elev_ft = 9.0;
    a.ends.push_back(end("KMIA", "08L", 25.802900, -80.301498, 8, 87.4, 8600, 150, 0, "NONE", "P4L",
                         3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KMIA", "26R", 25.804001, -80.275398, 8, 267.4, 8600, 150, 0, "NONE", "P4L",
                         3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KMIA", "08R", 25.800699, -80.301399, 8, 87, 10506, 200, 0, "MALSR", "P4L",
                         3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KMIA", "26L", 25.802000, -80.269501, 8, 267, 10506, 200, 0, "MALSF", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KMIA", "09", 25.786100, -80.314796, 7, 87, 13016, 150, 1358, "MALSR",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KMIA", "27", 25.787701, -80.275398, 8, 267, 13016, 150, 261, "MALSR",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KMIA", "12", 25.799299, -80.302300, 8, 119, 9360, 150, 0, "MALSR", "P4R",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KMIA", "30", 25.786600, -80.277496, 8, 299, 9360, 150, 945, "MALS", "P4L",
                         3.00, false, false, false, 0, 0));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_krno() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "KRNO";
    a.name = "Reno-Tahoe International";
    a.arp_lat = 39.0 + 29.95 / 60.0;
    a.arp_lon = -(119.0 + 46.09 / 60.0);
    a.elev_ft = 4415.0;
    a.ends.push_back(end("KRNO", "17R", 39.513802, -119.768997, 4415, 180, 11001, 150, 1000, "MALSR",
                         "P4L", 3.06, false, false, false, 0, 0));
    a.ends.push_back(end("KRNO", "35L", 39.483601, -119.768997, 4410, 360, 11001, 150, 990, "MALSR",
                         "P4L", 3.54, false, false, false, 0, 0));
    a.ends.push_back(end("KRNO", "17L", 39.513802, -119.766998, 4415, 180, 9000, 150, 0, "NONE",
                         "P4L", 3.00, true, false, false, 0, 0));
    a.ends.push_back(end("KRNO", "35R", 39.489101, -119.766998, 4408, 360, 9000, 150, 0, "NONE",
                         "P4L", 3.35, true, false, false, 0, 0));
    a.ends.push_back(end("KRNO", "08", 39.496300, -119.778999, 4409, 90, 6102, 150, 0, "NONE", "P4L",
                         3.20, true, false, false, 0, 0));
    a.ends.push_back(end("KRNO", "26", 39.496201, -119.757004, 4400, 270, 6102, 150, 0, "NONE", "P4L",
                         3.00, true, false, false, 0, 0));
    stamp(a, kNasrSrc, kNasrEff, kNasrRet);
    return a;
}

const FaaAirportRef& faa_eddf() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "EDDF";
    a.name = "Frankfurt Main";
    a.arp_lat = 50.026706;
    a.arp_lon = 8.55835;
    a.elev_ft = 364.0;
    const double w60 = 60.0 * geo::kMToFt;
    const double w45 = 45.0 * geo::kMToFt;
    a.ends.push_back(end("EDDF", "07C", 50.032600, 8.534630, 329, 69.6, 13123, w60, 0, "CAT-II",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("EDDF", "25C", 50.045101, 8.586980, 364, 249.6, 13123, w60, 0, "CAT-II",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("EDDF", "07L", 50.037102, 8.497080, 305, 69.6, 9186, w45, 0, "CAT-II",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("EDDF", "25R", 50.045799, 8.533720, 353, 249.6, 9186, w45, 0, "CAT-II",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("EDDF", "07R", 50.027500, 8.534170, 328, 69.6, 13123, w45, 0, "CAT-II",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("EDDF", "25L", 50.040100, 8.586530, 362, 249.6, 13123, w45, 0, "CAT-II",
                         "P4L", 3.00, false, true, false, 0, 0));
    // 18 is published one-way. South physical end is the 18 DER (no 36 landing ALS).
    a.ends.push_back(end("EDDF", "18", 50.034154, 8.525944, 326, 179.63, 13123, w45, 0, "CAT-I",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("EDDF", "36", 49.998493, 8.526297, 316, 359.63, 13123, w45, 0, "NONE",
                         "NONE", 3.00, false, false, false, 0, 0));
    stamp(a, "DFS Germany AIP AD 2 EDDF, cross-checked OurAirports", "2026-10-02", kNasrRet);
    return a;
}

const FaaAirportRef& faa_egll() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "EGLL";
    a.name = "London Heathrow";
    a.arp_lat = 51.470748;
    a.arp_lon = -0.459909;
    a.elev_ft = 83.0;
    const double w50 = 50.0 * geo::kMToFt;
    a.ends.push_back(end("EGLL", "09L", 51.47749, -0.489439, 79, 90, 12799, w50, 1007, "CAT-II",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("EGLL", "27R", 51.477681, -0.433227, 78, 270, 12799, w50, 0, "CAT-II",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("EGLL", "09R", 51.46478, -0.486808, 75, 90, 12001, w50, 1013, "CAT-II",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("EGLL", "27L", 51.464957, -0.434048, 77, 270, 12001, w50, 0, "CAT-II",
                         "P4L", 3.00, false, true, false, 0, 0));
    stamp(a, "NATS UK AIP AD 2 EGLL, cross-checked OurAirports", "2025-04-17", kNasrRet);
    return a;
}

const FaaAirportRef& faa_lebl() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "LEBL";
    a.name = "Barcelona El Prat";
    a.arp_lat = 41.2971;
    a.arp_lon = 2.07846;
    a.elev_ft = 12.0;
    const double w45 = 45.0 * geo::kMToFt;
    const double w60 = 60.0 * geo::kMToFt;
    a.ends.push_back(end("LEBL", "02", 41.287737, 2.084830, 7, 19, 8300, w45, 0, "CAT-I", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("LEBL", "20", 41.309293, 2.094668, 12, 199, 8300, w45, 0, "CAT-I", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("LEBL", "06L", 41.293244, 2.067251, 8, 66, 11000, w60, 1411, "CAT-I", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("LEBL", "24R", 41.305735, 2.103751, 10, 246, 11000, w60, 0, "CAT-II", "P4L",
                         3.00, false, true, false, 0, 0));
    a.ends.push_back(end("LEBL", "06R", 41.282311, 2.074342, 8, 66, 8727, w60, 0, "CAT-I", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("LEBL", "24L", 41.292218, 2.103282, 8, 246, 8727, w60, 0, "CAT-II", "P4L",
                         3.00, false, true, false, 0, 0));
    stamp(a, "ENAIRE Spain AIP AD 2 LEBL, cross-checked OurAirports", "2026-10-02", kNasrRet);
    return a;
}

const FaaAirportRef& faa_seqm() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "SEQM";
    a.name = "Quito Mariscal Sucre";
    a.arp_lat = -0.125399;
    a.arp_lon = -78.354306;
    a.elev_ft = 7841.0;
    const double w45 = 45.0 * geo::kMToFt;
    a.ends.push_back(end("SEQM", "18", -0.105617, -78.355209, 7776, 180, 13445, w45, 0, "SALS",
                         "P4L", 3.20, false, false, false, 0, 0));
    a.ends.push_back(end("SEQM", "36", -0.142650, -78.353622, 7910, 360, 13445, w45, 0, "CAT-II",
                         "P4L", 3.20, false, true, false, 0, 0));
    stamp(a, "DGAC Ecuador AIP AD 2 SEQM, cross-checked OurAirports", "2026-10-02", kNasrRet);
    return a;
}

const FaaAirportRef& faa_skbo() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "SKBO";
    a.name = "Bogotá El Dorado";
    a.arp_lat = 4.70159;
    a.arp_lon = -74.1469;
    a.elev_ft = 8361.0;
    const double w45 = 45.0 * geo::kMToFt;
    a.ends.push_back(end("SKBO", "14L", 4.71339, -74.1521, 8354, 127, 12467, w45, 0, "CAT-I", "P4L",
                         3.00, false, false, false, 0, 0));
    a.ends.push_back(end("SKBO", "32R", 4.69270, -74.124702, 8354, 307, 12467, w45, 0, "CAT-I",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("SKBO", "14R", 4.71050, -74.169197, 8350, 127, 12467, w45, 0, "CAT-I",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("SKBO", "32L", 4.68979, -74.141800, 8355, 307, 12467, w45, 0, "CAT-I",
                         "P4L", 3.00, false, false, false, 0, 0));
    // Current Aerocivil eAIP is AIRAC AMDT 73/26 (effective 2026-10-01) at
    // eaip-colombia.atnaerocivil.gov.co. AD 2.12 HTML is not fetchable from
    // this tree (404 / JS portal). Keep the last parseable AD 2 and warn.
    stamp(a, "Aerocivil Colombia AIP AD 2 SKBO (2016 edition; current AIRAC AMDT "
             "73/26 AD 2.12 not fetchable), cross-checked OurAirports",
          "2016-01-01", kNasrRet);
    return a;
}

const FaaAirportRef& faa_skrg() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "SKRG";
    a.name = "Rionegro / José María Córdova";
    a.arp_lat = 6.16454;
    a.arp_lon = -75.4231;
    a.elev_ft = 7025.0;
    const double w45 = 45.0 * geo::kMToFt;
    a.ends.push_back(end("SKRG", "01", 6.149270, -75.423019, 6967, 360, 11286, w45, 0, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("SKRG", "19", 6.180367, -75.423225, 7025, 180, 11286, w45, 0, "NONE", "P4L",
                         3.00, false, false, false, 0, 0));
    stamp(a, "Aerocivil Colombia AIP AD 2 SKRG AMDT 66-24, cross-checked OurAirports", "2024-11-28",
          kNasrRet);
    return a;
}

const FaaAirportRef& faa_vnkt() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once_ref(a, once);
    a.icao = "VNKT";
    a.name = "Kathmandu Tribhuvan";
    a.arp_lat = 27.6966;
    a.arp_lon = 85.3591;
    a.elev_ft = 4390.0;
    const double w46 = 46.0 * geo::kMToFt;
    // Older CAAN AD 2 listed 3050×46 m. Current physical length in the
    // national AIS / OurAirports cross-check is 10991 ft (3350 m) after
    // the published extension. Geometry uses the current physical ends.
    a.ends.push_back(end("VNKT", "02", 27.6838, 85.353401, 4313, 22, 10991, w46, 890, "SALS",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("VNKT", "20", 27.7094, 85.364799, 4387, 202, 10991, w46, 985, "SALS",
                         "P4L", 3.00, false, false, false, 0, 0));
    // Current CAAN eAIP (e-aip.caanepal.gov.np) AD 2.12 was not fetchable
    // here. Airport-profile PDFs still list 3050×46 m; this tree keeps the
    // last parseable physical ends and warns that the cycle is stale.
    stamp(a, "CAAN Nepal AIP AD 2 VNKT (last parseable cycle 2024-01-01; "
             "current eAIP AD 2.12 not fetchable), cross-checked OurAirports",
          "2024-01-01", kNasrRet);
    return a;
}

const FaaAirportRef* faa_airport(const std::string& icao) {
    const std::string id = up(icao);
    if (id == "KJFK")
        return &faa_kjfk();
    if (id == "KEWR")
        return &faa_kewr();
    if (id == "KSEA")
        return &faa_ksea();
    if (id == "KSFO")
        return &faa_ksfo();
    if (id == "KLAS")
        return &faa_klas();
    if (id == "KLAX")
        return &faa_klax();
    if (id == "KBOS")
        return &faa_kbos();
    if (id == "KIAD")
        return &faa_kiad();
    if (id == "KMCO")
        return &faa_kmco();
    if (id == "KMIA")
        return &faa_kmia();
    if (id == "KRNO")
        return &faa_krno();
    if (id == "EDDF")
        return &faa_eddf();
    if (id == "EGLL")
        return &faa_egll();
    if (id == "LEBL")
        return &faa_lebl();
    if (id == "SEQM")
        return &faa_seqm();
    if (id == "SKBO")
        return &faa_skbo();
    if (id == "SKRG")
        return &faa_skrg();
    if (id == "VNKT")
        return &faa_vnkt();
    return nullptr;
}

std::vector<const FaaAirportRef*> faa_all() {
    return {&faa_kjfk(), &faa_kewr(), &faa_ksea(), &faa_ksfo(), &faa_klas(), &faa_klax(),
            &faa_kbos(), &faa_kiad(), &faa_kmco(), &faa_kmia(), &faa_krno(), &faa_eddf(),
            &faa_egll(), &faa_lebl(), &faa_seqm(), &faa_skbo(), &faa_skrg(), &faa_vnkt()};
}

std::string official_authority(const FaaAirportRef& a) {
    const std::string& s = a.source;
    if (s.find("DFS") != std::string::npos)
        return "DFS";
    if (s.find("NATS") != std::string::npos)
        return "NATS";
    if (s.find("ENAIRE") != std::string::npos)
        return "ENAIRE";
    if (s.find("DGAC") != std::string::npos)
        return "DGAC Ecuador";
    if (s.find("Aerocivil") != std::string::npos)
        return "Aerocivil";
    if (s.find("CAAN") != std::string::npos)
        return "CAAN";
    if (s.find("FAA") != std::string::npos || s.find("NASR") != std::string::npos)
        return "FAA";
    if (!a.icao.empty() && (a.icao[0] == 'K' || a.icao[0] == 'k'))
        return "FAA";
    return "AIP";
}

namespace {

bool parse_ymd(const std::string& s, int& y, int& m, int& d) {
    if (s.size() < 10 || s[4] != '-' || s[7] != '-')
        return false;
    try {
        y = std::stoi(s.substr(0, 4));
        m = std::stoi(s.substr(5, 2));
        d = std::stoi(s.substr(8, 2));
    } catch (...) {
        return false;
    }
    return y >= 1900 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

int months_between(const std::string& earlier, const std::string& later) {
    int y0 = 0, m0 = 0, d0 = 0, y1 = 0, m1 = 0, d1 = 0;
    if (!parse_ymd(earlier, y0, m0, d0) || !parse_ymd(later, y1, m1, d1))
        return 0;
    int months = (y1 - y0) * 12 + (m1 - m0);
    if (d1 < d0)
        --months;
    return months;
}

} // namespace

bool official_source_stale(const FaaAirportRef& a, int max_months) {
    if (a.effective.empty() || a.retrieved.empty() || max_months < 1)
        return false;
    return months_between(a.effective, a.retrieved) > max_months;
}

const FaaEnd* faa_end(const FaaAirportRef& a, const std::string& ident) {
    const std::string id = up(ident);
    for (const auto& e : a.ends) {
        if (up(e.ident) == id)
            return &e;
    }
    return nullptr;
}

} // namespace s376
