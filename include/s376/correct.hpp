#pragma once

#include "s376/accuracy.hpp"
#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/image.hpp"
#include "s376/terrain.hpp"

#include <string>
#include <vector>

namespace s376 {

// Deterministic correction layer. Imagery (public or Raster-local) is
// only texture. Official FAA NASR / AIP geometry always wins: we
// repaint runway surface, markings, displaced-threshold arrows/bars,
// and EMAS from the official ends, blend out stale pavement the
// official data says is not there, and log every action.

struct ImageryCorrection {
    std::string icao;
    std::string feature;
    std::string imagery;  // what the photo / Raster pad may show
    std::string official; // FAA / AIP value
    std::string action;
};

struct CorrectionReport {
    std::string icao;
    std::vector<ImageryCorrection> items;
    AccReport imagery;
    int painted = 0;
    bool ok() const { return imagery.fails.empty(); }
};

CorrectionReport plan_corrections(const Airport& a, const FaaAirportRef& faa);
std::string format_correction_report(const CorrectionReport& r);
bool write_correction_report(const std::string& path, const CorrectionReport& r);

// Draw official surface / markings / displaced-threshold / EMAS over a
// north-up RGB box and blend out stale paint or pavement outside the
// official geometry. Used when imagery conflicts with FAA/AIP.
// Returns features written (surfaces + EMAS beds + marking sets + blend).
int apply_correction_overlay(RgbImage& im, const GeoBox& box, const Airport& a);
int apply_correction_overlay(ImageRgb& im, const GeoBox& box, const Airport& a);

// Resolve ICAO (CacheRegion name). 0 if the name is not an official airport.
int apply_correction_overlay_icao(ImageRgb& im, const GeoBox& box, const std::string& icao);

// plan_corrections + painted count → cache/<ICAO>/corrections.txt.
bool write_airport_correction_report(const std::string& dir, const std::string& icao, int painted);

// Doctor / CI: airport vs official, plus imagery geotransform if a box
// is supplied. Flags mismatches above kAccImageryM / kAccEndM.
AccReport check_imagery_vs_official(const Airport& a, const FaaAirportRef& faa,
                                    const GeoBox* box = nullptr, int w = 0, int h = 0);

} // namespace s376
