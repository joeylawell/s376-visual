#pragma once

#include "s376/airport.hpp"

#include <string>
#include <vector>

namespace s376 {

// Authoritative runway-end records. Geometry and lights come from official
// publications only — never Raster:
//   US  — FAA NASR / NFDC airportDisplay + AIP AD 2 (28-day family)
//   non-US — national AIP AD 2 (DFS, NATS, ENAIRE, DGAC Ecuador,
//            Aerocivil Colombia, CAAN Nepal)
// OurAirports is a cross-check only. The accuracy suite compares live
// airport objects to these rows. Every value carries source + effective date.

struct FaaEnd {
    std::string icao;
    std::string ident;
    double lat = 0;
    double lon = 0;
    double elev_ft = 0;
    double true_brg_deg = 0; // WGS-84 geodesic, not the integer NASR TRUE_ALIGNMENT
    double length_ft = 0;
    double width_ft = 0;
    double displaced_ft = 0;
    std::string als;  // NONE | ALSF-2 | ALSF-1 | MALSR | MALSF | MALS | SALS | RLLS | CAT-I | CAT-II
    std::string papi; // P4L | P4R | P4L+P4L
    double papi_deg = 3.0;
    double vgsi_tch_ft = 0; // published VGSI TCH; 0 = default 50 ft
    double ils_tch_ft = 0;  // plate ILS TCH; 0 = generic 1000 ft aim
    double papi_rotate_deg = 0;       // + = right of CL (13L +10)
    double papi_usable_left_deg = 15; // 0 = unusable left of CL
    double papi_usable_right_deg = 15;
    bool rlls_lead_in = false; // Chart Supplement LDIN (13L add-on / 13R)
    bool reil = false;
    bool tdz = false;
    bool emas = false;
    double emas_len_ft = 0;
    double emas_wid_ft = 0;
    std::string source;
    std::string effective; // NASR / AIP cycle
    std::string retrieved;
};

struct FaaAirportRef {
    std::string icao;
    std::string name;
    double arp_lat = 0;
    double arp_lon = 0;
    double elev_ft = 0;
    std::string source;
    std::string effective;
    std::string retrieved;
    std::vector<FaaEnd> ends;
};

const FaaAirportRef& faa_kjfk();
const FaaAirportRef& faa_kewr();
const FaaAirportRef& faa_ksea();
const FaaAirportRef& faa_ksfo();
const FaaAirportRef& faa_klas();
const FaaAirportRef& faa_klax();
const FaaAirportRef& faa_kbos();
const FaaAirportRef& faa_kiad();
const FaaAirportRef& faa_kmco();
const FaaAirportRef& faa_kmia();
const FaaAirportRef& faa_krno();
const FaaAirportRef& faa_eddf();
const FaaAirportRef& faa_egll();
const FaaAirportRef& faa_lebl();
const FaaAirportRef& faa_seqm();
const FaaAirportRef& faa_skbo();
const FaaAirportRef& faa_skrg();
const FaaAirportRef& faa_vnkt();

const FaaAirportRef* faa_airport(const std::string& icao);
inline const FaaAirportRef* official_airport(const std::string& icao) {
    return faa_airport(icao);
}
std::vector<const FaaAirportRef*> faa_all();

// Short authority for doctor / accuracy labels: FAA, DFS, NATS, ENAIRE,
// DGAC Ecuador, Aerocivil, CAAN. Never call a non-US AIP "FAA".
std::string official_authority(const FaaAirportRef& a);
// True when `effective` is more than `max_months` before `retrieved`
// (default 24). Used to warn that SKBO 2016 / VNKT 2024 are stale.
bool official_source_stale(const FaaAirportRef& a, int max_months = 24);

const FaaEnd* faa_end(const FaaAirportRef& a, const std::string& ident);

} // namespace s376
