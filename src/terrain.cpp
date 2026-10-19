#include "s376/terrain.hpp"
#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef S376_HAS_GDAL
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <cpl_string.h>
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "psapi.lib")
#endif
#endif

namespace s376 {
namespace {

constexpr char kMagic[] = "S376DEM1";

void ensure_parent(const std::string& path) {
    const auto p = std::filesystem::path(path).parent_path();
    if (!p.empty())
        std::filesystem::create_directories(p);
}

} // namespace

bool DemGrid::in_bounds(double lat, double lon) const {
    return !empty() && lat >= south && lat <= north && lon >= west && lon <= east;
}

bool DemGrid::sample_m(double lat, double lon, double& z_m_out) const {
    if (!in_bounds(lat, lon))
        return false;
    const double u = (lon - west) / (east - west) * (w - 1);
    const double v = (north - lat) / (north - south) * (h - 1);
    const int x0 = std::max(0, std::min(w - 2, static_cast<int>(std::floor(u))));
    const int y0 = std::max(0, std::min(h - 2, static_cast<int>(std::floor(v))));
    const double tx = u - x0;
    const double ty = v - y0;
    const float a = z_m[static_cast<std::size_t>(y0 * w + x0)];
    const float b = z_m[static_cast<std::size_t>(y0 * w + x0 + 1)];
    const float c = z_m[static_cast<std::size_t>((y0 + 1) * w + x0)];
    const float d = z_m[static_cast<std::size_t>((y0 + 1) * w + x0 + 1)];
    z_m_out = a * (1 - tx) * (1 - ty) + b * tx * (1 - ty) + c * (1 - tx) * ty + d * tx * ty;
    return std::isfinite(z_m_out);
}

bool DemGrid::sample_ft(double lat, double lon, double& z_ft_out) const {
    double m = 0;
    if (!sample_m(lat, lon, m))
        return false;
    z_ft_out = m * geo::kMToFt;
    return true;
}

DemGrid DemGrid::synth_ksea(int w, int h) {
    DemGrid d;
    d.w = std::max(8, w);
    d.h = std::max(8, h);
    // ~25 km box around Sea-Tac. Public ARP 47.4479, -122.3103, 433 ft.
    d.west = -122.48;
    d.east = -122.14;
    d.south = 47.32;
    d.north = 47.58;
    d.z_m.assign(static_cast<std::size_t>(d.w * d.h), 132.f);
    const double field_lat = 47.4479;
    const double field_lon = -122.3103;
    for (int y = 0; y < d.h; ++y) {
        const double lat = d.north - (d.north - d.south) * y / (d.h - 1);
        for (int x = 0; x < d.w; ++x) {
            const double lon = d.west + (d.east - d.west) * x / (d.w - 1);
            const double dlat = (lat - field_lat) * 111320.0;
            const double dlon = (lon - field_lon) * 111320.0 * std::cos(geo::deg2rad(field_lat));
            const double r = std::sqrt(dlat * dlat + dlon * dlon);
            float z = 132.f;
            // Puget Sound to the west a bit lower; Cascades foothills to the east.
            z += static_cast<float>(std::max(0.0, (lon + 122.31) * 400.0));
            z += static_cast<float>(20.0 * std::sin(dlat * 0.001) * std::cos(dlon * 0.001));
            if (r < 2500.0)
                z = 132.f; // flatten the airport
            else if (r < 4000.0)
                z = 132.f + (z - 132.f) * static_cast<float>((r - 2500.0) / 1500.0);
            d.z_m[static_cast<std::size_t>(y * d.w + x)] = z;
        }
    }
    return d;
}

void DemGrid::save(const std::string& path) const {
    ensure_parent(path);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        if (!o)
            throw std::runtime_error("cannot write " + tmp);
        o.write(kMagic, 8);
        o.write(reinterpret_cast<const char*>(&w), 4);
        o.write(reinterpret_cast<const char*>(&h), 4);
        o.write(reinterpret_cast<const char*>(&west), 8);
        o.write(reinterpret_cast<const char*>(&south), 8);
        o.write(reinterpret_cast<const char*>(&east), 8);
        o.write(reinterpret_cast<const char*>(&north), 8);
        o.write(reinterpret_cast<const char*>(z_m.data()),
                static_cast<std::streamsize>(z_m.size() * 4));
        o.flush();
        if (!o)
            throw std::runtime_error("failed writing " + tmp);
    }
    std::filesystem::rename(tmp, path);
}

static bool read_dem_header(std::istream& in, DemGrid& d) {
    char mag[8];
    in.read(mag, 8);
    if (!in || std::memcmp(mag, kMagic, 8) != 0)
        return false;
    in.read(reinterpret_cast<char*>(&d.w), 4);
    in.read(reinterpret_cast<char*>(&d.h), 4);
    in.read(reinterpret_cast<char*>(&d.west), 8);
    in.read(reinterpret_cast<char*>(&d.south), 8);
    in.read(reinterpret_cast<char*>(&d.east), 8);
    in.read(reinterpret_cast<char*>(&d.north), 8);
    return static_cast<bool>(in) && d.w > 0 && d.h > 0 && d.w <= 16384 && d.h <= 16384;
}

DemGrid DemGrid::peek(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    DemGrid d;
    if (!read_dem_header(in, d))
        throw std::runtime_error(path + " is not an s376 DEM");
    return d;
}

DemGrid DemGrid::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    DemGrid d;
    if (!read_dem_header(in, d))
        throw std::runtime_error(path + " is not an s376 DEM");
    d.z_m.resize(static_cast<std::size_t>(d.w * d.h));
    in.read(reinterpret_cast<char*>(d.z_m.data()), static_cast<std::streamsize>(d.z_m.size() * 4));
    if (!in)
        throw std::runtime_error(path + " truncated");
    return d;
}

DemGrid DemGrid::load_subsampled(const std::string& path, int max_n) {
    const int cap = std::max(8, max_n);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    DemGrid hdr;
    if (!read_dem_header(in, hdr))
        throw std::runtime_error(path + " is not an s376 DEM");
    if (hdr.w <= cap && hdr.h <= cap)
        return load(path);
    DemGrid d;
    d.w = cap;
    d.h = cap;
    d.west = hdr.west;
    d.south = hdr.south;
    d.east = hdr.east;
    d.north = hdr.north;
    d.z_m.assign(static_cast<std::size_t>(d.w * d.h), 0.f);
    const std::uint64_t header = 48;
    std::vector<float> row(static_cast<std::size_t>(hdr.w));
    for (int y = 0; y < d.h; ++y) {
        const int sy = std::min(hdr.h - 1, y * (hdr.h - 1) / std::max(1, d.h - 1));
        in.seekg(static_cast<std::streamoff>(header + static_cast<std::uint64_t>(sy) *
                                                           static_cast<std::uint64_t>(hdr.w) * 4));
        in.read(reinterpret_cast<char*>(row.data()),
                static_cast<std::streamsize>(hdr.w * 4));
        if (!in)
            throw std::runtime_error(path + " truncated");
        for (int x = 0; x < d.w; ++x) {
            const int sx = std::min(hdr.w - 1, x * (hdr.w - 1) / std::max(1, d.w - 1));
            d.z_m[static_cast<std::size_t>(y * d.w + x)] = row[static_cast<std::size_t>(sx)];
        }
    }
    return d;
}

bool DemGrid::sample_file_m(const std::string& path, double lat, double lon, double& z_m_out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    DemGrid d;
    if (!read_dem_header(in, d))
        return false;
    if (lat < d.south || lat > d.north || lon < d.west || lon > d.east)
        return false;
    const double u = (lon - d.west) / (d.east - d.west) * (d.w - 1);
    const double v = (d.north - lat) / (d.north - d.south) * (d.h - 1);
    const int x0 = std::max(0, std::min(d.w - 2, static_cast<int>(std::floor(u))));
    const int y0 = std::max(0, std::min(d.h - 2, static_cast<int>(std::floor(v))));
    const double tx = u - x0;
    const double ty = v - y0;
    auto rd = [&](int x, int y) -> float {
        const std::uint64_t off =
            48ull + static_cast<std::uint64_t>(y) * static_cast<std::uint64_t>(d.w) * 4ull +
            static_cast<std::uint64_t>(x) * 4ull;
        in.seekg(static_cast<std::streamoff>(off));
        float z = 0;
        in.read(reinterpret_cast<char*>(&z), 4);
        return z;
    };
    const float a = rd(x0, y0);
    const float b = rd(x0 + 1, y0);
    const float c = rd(x0, y0 + 1);
    const float e = rd(x0 + 1, y0 + 1);
    if (!in)
        return false;
    z_m_out = a * (1 - tx) * (1 - ty) + b * tx * (1 - ty) + c * (1 - tx) * ty + e * tx * ty;
    return std::isfinite(z_m_out);
}

#ifdef S376_HAS_GDAL
bool gdal_available() {
    return true;
}

DemGrid DemGrid::load_geotiff(const std::string& path) {
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds)
        throw std::runtime_error("GDAL could not open " + path);
    DemGrid d;
    d.w = ds->GetRasterXSize();
    d.h = ds->GetRasterYSize();
    double gt[6] = {};
    if (ds->GetGeoTransform(gt) != CE_None) {
        GDALClose(ds);
        throw std::runtime_error(path + " has no geotransform");
    }
    d.west = gt[0];
    d.north = gt[3];
    d.east = gt[0] + gt[1] * d.w;
    d.south = gt[3] + gt[5] * d.h;
    if (d.south > d.north)
        std::swap(d.south, d.north);
    d.z_m.resize(static_cast<std::size_t>(d.w * d.h));
    CPLErr e = ds->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, d.w, d.h, d.z_m.data(), d.w, d.h,
                                              GDT_Float32, 0, 0);
    GDALClose(ds);
    if (e != CE_None)
        throw std::runtime_error("GDAL read failed for " + path);
    // If the file looks like feet (typical US NED), leave it — Copernicus is metres.
    return d;
}

void DemGrid::save_geotiff(const std::string& path) const {
    GDALAllRegister();
    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv)
        throw std::runtime_error("GDAL has no GTiff driver");
    ensure_parent(path);
    GDALDataset* ds = drv->Create(path.c_str(), w, h, 1, GDT_Float32, nullptr);
    if (!ds)
        throw std::runtime_error("GDAL could not create " + path);
    double gt[6] = {west, (east - west) / w, 0, north, 0, -(north - south) / h};
    ds->SetGeoTransform(gt);
    ds->SetProjection("GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                      "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");
    CPLErr we = ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, w, h, const_cast<float*>(z_m.data()),
                                               w, h, GDT_Float32, 0, 0);
    GDALClose(ds);
    if (we != CE_None)
        throw std::runtime_error("GDAL write failed for " + path);
}

void ImageRgb::save_geotiff(const std::string& path, const DemGrid& georef) const {
    if (rgb.empty() || w < 1 || h < 1)
        throw std::runtime_error("empty imagery, cannot write " + path);
    GDALAllRegister();
    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv)
        throw std::runtime_error("GDAL has no GTiff driver");
    ensure_parent(path);
    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "DEFLATE");
    GDALDataset* ds = drv->Create(path.c_str(), w, h, 3, GDT_Byte, opts);
    CSLDestroy(opts);
    if (!ds)
        throw std::runtime_error("GDAL could not create " + path);
    const double west = georef.west, east = georef.east, south = georef.south, north = georef.north;
    double gt[6] = {west, (east - west) / w, 0, north, 0, -(north - south) / h};
    ds->SetGeoTransform(gt);
    ds->SetProjection("GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                      "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");
    std::vector<std::uint8_t> band(static_cast<std::size_t>(w * h));
    for (int b = 0; b < 3; ++b) {
        for (int i = 0; i < w * h; ++i)
            band[static_cast<std::size_t>(i)] = rgb[static_cast<std::size_t>(i) * 3 + b];
        CPLErr we = ds->GetRasterBand(b + 1)->RasterIO(GF_Write, 0, 0, w, h, band.data(), w, h,
                                                       GDT_Byte, 0, 0);
        if (we != CE_None) {
            GDALClose(ds);
            throw std::runtime_error("GDAL imagery write failed for " + path);
        }
    }
    GDALClose(ds);
}

bool peek_georef(const std::string& path, double& west, double& south, double& east, double& north,
                 int& w, int& h, int& bands) {
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds)
        return false;
    w = ds->GetRasterXSize();
    h = ds->GetRasterYSize();
    bands = ds->GetRasterCount();
    double gt[6] = {};
    const bool ok = ds->GetGeoTransform(gt) == CE_None && w > 0 && h > 0;
    if (ok) {
        west = gt[0];
        north = gt[3];
        east = gt[0] + gt[1] * w;
        south = gt[3] + gt[5] * h;
        if (south > north)
            std::swap(south, north);
        if (west > east)
            std::swap(west, east);
    }
    GDALClose(ds);
    return ok && east > west && north > south;
}

GeorefRgb load_georef_rgb(const std::string& path) {
    GeorefRgb g;
    int w = 0, h = 0, bands = 0;
    if (!peek_georef(path, g.west, g.south, g.east, g.north, w, h, bands))
        throw std::runtime_error(path + " is not a georeferenced raster GDAL can read");
    g.img = ImageRgb::load_jpeg_or_png(path);
    return g;
}
#else
bool gdal_available() {
    return false;
}

DemGrid DemGrid::load_geotiff(const std::string& path) {
    throw std::runtime_error("GDAL was not built in; cannot read " + path +
                             " (install libgdal-dev and re-cmake)");
}

void DemGrid::save_geotiff(const std::string&) const {
    throw std::runtime_error("GDAL was not built in");
}

void ImageRgb::save_geotiff(const std::string&, const DemGrid&) const {
    throw std::runtime_error("GDAL was not built in");
}

bool peek_georef(const std::string&, double&, double&, double&, double&, int&, int&, int&) {
    return false;
}

GeorefRgb load_georef_rgb(const std::string& path) {
    throw std::runtime_error("GDAL was not built in; cannot read " + path);
}
#endif

bool DemHeightSource::sample_ft(double lat, double lon, double& elev_msl_ft) const {
    if (!dem_)
        return false;
    return dem_->sample_ft(lat, lon, elev_msl_ft);
}

ImageRgb ImageRgb::hillshade(const DemGrid& dem) {
    ImageRgb im;
    im.w = dem.w;
    im.h = dem.h;
    im.rgb.resize(static_cast<std::size_t>(im.w * im.h * 3));
    const double dx = (dem.east - dem.west) / std::max(1, dem.w - 1) * 111320.0 *
                      std::cos(geo::deg2rad((dem.north + dem.south) * 0.5));
    const double dy = (dem.north - dem.south) / std::max(1, dem.h - 1) * 111320.0;
    for (int y = 0; y < im.h; ++y) {
        for (int x = 0; x < im.w; ++x) {
            const int x0 = std::max(0, x - 1), x1 = std::min(im.w - 1, x + 1);
            const int y0 = std::max(0, y - 1), y1 = std::min(im.h - 1, y + 1);
            const float zx0 = dem.z_m[static_cast<std::size_t>(y * dem.w + x0)];
            const float zx1 = dem.z_m[static_cast<std::size_t>(y * dem.w + x1)];
            const float zy0 = dem.z_m[static_cast<std::size_t>(y0 * dem.w + x)];
            const float zy1 = dem.z_m[static_cast<std::size_t>(y1 * dem.w + x)];
            const double dzx = (zx1 - zx0) / std::max(1e-3, dx * (x1 - x0));
            const double dzy = (zy0 - zy1) / std::max(1e-3, dy * (y1 - y0));
            double nx = -dzx, ny = -dzy, nz = 1.0;
            const double mag = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= mag;
            ny /= mag;
            nz /= mag;
            const double shade = std::max(0.0, nx * 0.4 + ny * 0.3 + nz * 0.85);
            const float z = dem.z_m[static_cast<std::size_t>(y * dem.w + x)];
            const bool water = z < 1.0f;
            unsigned char r, g, b;
            if (water) {
                r = 30;
                g = 60;
                b = 110;
            } else {
                r = static_cast<unsigned char>(40 + 80 * shade);
                g = static_cast<unsigned char>(60 + 110 * shade);
                b = static_cast<unsigned char>(40 + 50 * shade);
            }
            const int o = (y * im.w + x) * 3;
            im.rgb[o] = r;
            im.rgb[o + 1] = g;
            im.rgb[o + 2] = b;
        }
    }
    return im;
}

ImageRgb ImageRgb::load_jpeg_or_png(const std::string& path) {
#ifdef S376_HAS_GDAL
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds)
        throw std::runtime_error("GDAL could not open imagery " + path);
    ImageRgb im;
    im.w = ds->GetRasterXSize();
    im.h = ds->GetRasterYSize();
    if (im.w <= 0 || im.h <= 0 || im.w > 32768 || im.h > 32768) {
        GDALClose(ds);
        throw std::runtime_error(path + " has implausible imagery size");
    }
    im.rgb.assign(static_cast<std::size_t>(im.w * im.h * 3), 0);
    const int nb = ds->GetRasterCount();
    if (nb >= 3) {
        int bands[3] = {1, 2, 3};
        CPLErr e = ds->RasterIO(GF_Read, 0, 0, im.w, im.h, im.rgb.data(), im.w, im.h, GDT_Byte, 3,
                                bands, 3, static_cast<GSpacing>(im.w) * 3, 1);
        GDALClose(ds);
        if (e != CE_None)
            throw std::runtime_error("GDAL imagery read failed for " + path);
        return im;
    }
    std::vector<std::uint8_t> gray(static_cast<std::size_t>(im.w * im.h));
    CPLErr e =
        ds->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, im.w, im.h, gray.data(), im.w, im.h, GDT_Byte,
                                       0, 0);
    GDALClose(ds);
    if (e != CE_None)
        throw std::runtime_error("GDAL imagery read failed for " + path);
    for (int i = 0; i < im.w * im.h; ++i) {
        im.rgb[static_cast<std::size_t>(i) * 3 + 0] = gray[static_cast<std::size_t>(i)];
        im.rgb[static_cast<std::size_t>(i) * 3 + 1] = gray[static_cast<std::size_t>(i)];
        im.rgb[static_cast<std::size_t>(i) * 3 + 2] = gray[static_cast<std::size_t>(i)];
    }
    return im;
#else
    (void)path;
    return {};
#endif
}

CacheRegion generate_ksea_cache() {
    CacheRegion r;
    r.name = "ksea";
    r.dem = DemGrid::synth_ksea(128, 128);
    r.imagery = ImageRgb::hillshade(r.dem);
    return r;
}

void write_cache(const CacheRegion& r, const std::string& dir) {
    ensure_parent(dir + "/elev.s376dem");
    r.dem.save(dir + "/elev.s376dem");
    if (!r.imagery.rgb.empty())
        dxt::write_dds_bc1(dir + "/imagery.dds", r.imagery.rgb.data(), r.imagery.w, r.imagery.h,
                           true);
    std::ofstream t(dir + "/region.toml");
    t << "name = \"" << r.name << "\"\n";
    t << "west = " << r.dem.west << "\n";
    t << "south = " << r.dem.south << "\n";
    t << "east = " << r.dem.east << "\n";
    t << "north = " << r.dem.north << "\n";
    t << "width = " << r.dem.w << "\n";
    t << "height = " << r.dem.h << "\n";
    t << "elev = \"elev.s376dem\"\n";
    t << "imagery = \"imagery.dds\"\n";
    t << "format = \"bc1\"\n";
    if (gdal_available()) {
        try {
            r.dem.save_geotiff(dir + "/elev.tif");
            t << "elev_tif = \"elev.tif\"\n";
            if (!r.imagery.rgb.empty()) {
                r.imagery.save_geotiff(dir + "/imagery.tif", r.dem);
                t << "imagery_tif = \"imagery.tif\"\n";
            }
        } catch (const std::exception& ex) {
            S376_LOG_WARN("cache", "GeoTIFF sidecar: %s", ex.what());
        }
    }
    const std::size_t raw = static_cast<std::size_t>(r.dem.w * r.dem.h) * 4 +
                            static_cast<std::size_t>(r.imagery.w * r.imagery.h) * 3;
    const std::size_t packed = static_cast<std::size_t>(r.dem.w * r.dem.h) * 4 + dxt::bc1_size(r.imagery.w, r.imagery.h);
    t << "# packed " << packed << " bytes vs raw " << raw << " (plus RSI CERTS was 857 GB uncompressed)\n";
}

HatSample sample_hat(const HeightSource* src, double lat_deg, double lon_deg, double alt_ft) {
    HatSample s;
    double z = 0;
    if (src && src->sample_ft(lat_deg, lon_deg, z)) {
        s.covered = true;
        s.elev_ft = static_cast<float>(z);
        s.hat_ft = static_cast<float>(alt_ft - z);
        return s;
    }
    s.covered = false;
    s.elev_ft = 0;
    s.hat_ft = static_cast<float>(alt_ft);
    return s;
}

bool dem_file_complete(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec || sz < 48)
        return false;
    try {
        const DemGrid hdr = DemGrid::peek(path);
        const std::uint64_t need =
            48ull + static_cast<std::uint64_t>(hdr.w) * static_cast<std::uint64_t>(hdr.h) * 4ull;
        return sz >= need;
    } catch (...) {
        return false;
    }
}

std::shared_ptr<DemGrid> load_or_synth_dem(const std::string& cache_dir) {
    if (!cache_dir.empty()) {
        if (dem_file_complete(cache_dir + "/elev.s376dem")) {
            try {
                return std::make_shared<DemGrid>(DemGrid::load(cache_dir + "/elev.s376dem"));
            } catch (const std::exception&) {
            }
        }
        if (dem_file_complete(cache_dir + "/lod0/elev.s376dem")) {
            try {
                return std::make_shared<DemGrid>(DemGrid::load(cache_dir + "/lod0/elev.s376dem"));
            } catch (const std::exception&) {
            }
        }
        const auto tiles = scan_cache_tiles(cache_dir);
        for (const auto& t : tiles) {
            const std::string elev = t.path + "/elev.s376dem";
            if (!dem_file_complete(elev))
                continue;
            try {
                return std::make_shared<DemGrid>(DemGrid::load_subsampled(elev, 128));
            } catch (const std::exception&) {
            }
        }
    }
    return std::make_shared<DemGrid>(DemGrid::synth_ksea());
}

CacheRegion load_cache(const std::string& dir) {
    CacheRegion r;
    r.dir = dir;
    r.dem = DemGrid::load(dir + "/elev.s376dem");
    r.imagery_dds = dir + "/imagery.dds";
    // Do not hillshade the full DEM here — a 4000² tile is 48 MB of RGB
    // and was a large part of the KJFK OOM. Texture comes from imagery.dds.
    return r;
}

CacheRegion load_cache_mesh(const std::string& dir, int max_mesh_n) {
    CacheRegion r;
    r.dir = dir;
    r.dem = DemGrid::load_subsampled(dir + "/elev.s376dem", max_mesh_n);
    r.imagery_dds = dir + "/imagery.dds";
    return r;
}

bool cache_dir_looks_packed(const std::string& dir) {
    if (dir.empty())
        return false;
    return std::filesystem::exists(dir + "/elev.s376dem") ||
           std::filesystem::exists(dir + "/lod0/elev.s376dem") ||
           std::filesystem::exists(dir + "/imagery.dds") ||
           std::filesystem::is_directory(dir + "/lod0");
}

std::string resolve_cache_dir(const std::string& configured, const std::string& icao) {
    if (!configured.empty() && cache_dir_looks_packed(configured))
        return configured;
    if (!configured.empty() && std::filesystem::is_directory(configured))
        return configured;
    if (icao.empty())
        return configured;
    std::string id = icao;
    std::string low = icao;
    for (char& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : low)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string cands[] = {"cache/" + id, "cache/" + low, id, low};
    for (const auto& c : cands) {
        if (cache_dir_looks_packed(c))
            return c;
    }
    return configured;
}

double tile_center_lat(const CacheTileRef& t) {
    return 0.5 * (t.south + t.north);
}
double tile_center_lon(const CacheTileRef& t) {
    return 0.5 * (t.west + t.east);
}

std::uint64_t estimate_tile_cpu_bytes(const CacheTileRef& t, int max_mesh_n) {
    const int n = std::max(8, max_mesh_n);
    // verts + norms + uvs + indices, after the full raster is discarded
    const std::uint64_t mesh = static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(n) * 48ull;
    return mesh + 8192ull + std::min<std::uint64_t>(t.dds_bytes, 256ull * 1024ull);
}

std::vector<CacheTileRef> scan_cache_tiles(const std::string& dir) {
    std::vector<CacheTileRef> out;
    if (dir.empty() || !std::filesystem::is_directory(dir))
        return out;
    auto add = [&](const std::string& path, int lod) {
        const std::string elev = path + "/elev.s376dem";
        if (!dem_file_complete(elev))
            return;
        try {
            CacheTileRef t;
            // generic_string() keeps `/` on Windows so tests and
            // `path + "/elev.s376dem"` stay comparable with POSIX joins.
            t.path = std::filesystem::path(path).generic_string();
            t.lod = lod;
            const auto hdr = DemGrid::peek(elev);
            t.west = hdr.west;
            t.south = hdr.south;
            t.east = hdr.east;
            t.north = hdr.north;
            t.dem_w = hdr.w;
            t.dem_h = hdr.h;
            t.dem_bytes = std::filesystem::file_size(elev);
            const std::string dds = path + "/imagery.dds";
            if (std::filesystem::exists(dds))
                t.dds_bytes = std::filesystem::file_size(dds);
            out.push_back(std::move(t));
        } catch (...) {
        }
    };
    bool any_leaf = false;
    for (int lod = 0; lod <= 2; ++lod) {
        const std::string base = dir + "/lod" + std::to_string(lod);
        std::vector<std::string> leaves;
        if (std::filesystem::is_directory(base)) {
            for (const auto& e : std::filesystem::directory_iterator(base)) {
                if (!e.is_directory())
                    continue;
                const std::string name = e.path().filename().string();
                if (name.size() < 2 || name[0] != 't')
                    continue;
                leaves.push_back(e.path().generic_string());
            }
        }
        if (!leaves.empty()) {
            for (const auto& p : leaves)
                add(p, lod);
            any_leaf = true;
        } else {
            add(base, lod); // untiled lodN/ (synth packs)
        }
    }
    // Root elev.s376dem is HAT-only when a tiled pyramid exists. Paging it
    // in as a 96² mesh over the whole field hides the near tY_X tiles.
    if (!any_leaf)
        add(dir, 1);
    return out;
}

void score_tiles_for_view(const std::vector<CacheTileRef>& index, double elat, double elon,
                          double tlat, double tlon, double hdg_true, bool have_eye,
                          bool coarse_only, std::vector<TilePageWant>& want) {
    want.clear();
    want.reserve(index.size());
    for (const auto& t : index) {
        if (coarse_only && t.lod == 0)
            continue;
        const auto km_to = [&](double lat, double lon) {
            const double dlat = (tile_center_lat(t) - lat) * 111.32;
            const double dlon = (tile_center_lon(t) - lon) * 111.32 *
                                std::max(0.2, std::cos(geo::deg2rad(lat)));
            return std::sqrt(dlat * dlat + dlon * dlon);
        };
        const double km = std::min(km_to(elat, elon), km_to(tlat, tlon));
        if (t.lod == 0 && have_eye) {
            const double dlat = (tile_center_lat(t) - elat) * 111.32;
            const double dlon = (tile_center_lon(t) - elon) * 111.32 *
                                std::max(0.2, std::cos(geo::deg2rad(elat)));
            const double brg = std::atan2(dlon, dlat) * (180.0 / 3.14159265358979323846);
            double dh = brg - hdg_true;
            while (dh > 180.0)
                dh -= 360.0;
            while (dh < -180.0)
                dh += 360.0;
            if (km > 18.0 && std::fabs(dh) > 90.0)
                continue;
        }
        if (t.lod >= 1 && km > kFarFieldKm + 8.0)
            continue;
        const bool reserve = t.lod >= 1 && km <= kFarFieldKm;
        const double w = reserve ? (t.lod == 1 ? 0.18 : 0.22)
                                 : (t.lod == 0 ? (km < 4.0 ? 0.12 : 0.35) : 1.40);
        TilePageWant s;
        s.t = &t;
        s.km = km;
        s.score = km * w;
        s.reserve_coarse = reserve;
        want.push_back(s);
    }
    std::sort(want.begin(), want.end(),
              [](const TilePageWant& a, const TilePageWant& b) { return a.score < b.score; });
}

void kjfk_subset_land_rgb(double lat, double lon, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b,
                          bool& water) {
    const float cover = geo::land_coverage(lat, lon);
    water = cover < 0.35f;
    const double n = std::fmod(std::sin(lat * 127.1 + lon * 311.7) * 43758.5453, 1.0);
    const double n2 = std::fmod(std::sin(lat * 19.3 + lon * 47.1 + 3.7) * 24634.857, 1.0);
    auto clamp8 = [](double v) -> std::uint8_t {
        if (v < 0.0)
            return 0;
        if (v > 255.0)
            return 255;
        return static_cast<std::uint8_t>(v + 0.5);
    };
    if (water) {
        const bool atlantic = lat < 40.545 || (lat < 40.575 && lon > -73.84);
        const double base_r = atlantic ? 12.0 : 32.0;
        const double base_g = atlantic ? 40.0 : 74.0;
        const double base_b = atlantic ? 86.0 : 72.0;
        r = clamp8(base_r + (n - 0.5) * 8.0);
        g = clamp8(base_g + (n2 - 0.5) * 10.0);
        b = clamp8(base_b + (n - 0.5) * 12.0);
        return;
    }
    const bool beach = lat > 40.545 && lat < 40.592 && lon > -73.96 && lon < -73.73;
    const bool manhattan = lat > 40.698 && lat < 40.882 && lon > -74.025 && lon < -73.928;
    const bool field = lat > 40.626 && lat < 40.662 && lon > -73.828 && lon < -73.752;
    const bool urban =
        !field && lat > 40.598 && lat < 40.820 && lon > -74.060 && lon < -73.740;
    const bool marsh = cover >= 0.35f && cover < 0.52f;
    double br = 78, bg = 98, bb = 52;
    if (beach) {
        br = 188;
        bg = 170;
        bb = 128;
    } else if (manhattan) {
        br = 96;
        bg = 94;
        bb = 92;
    } else if (urban) {
        br = 78;
        bg = 84;
        bb = 76;
    } else if (field) {
        br = 86;
        bg = 100;
        bb = 64;
    } else if (marsh) {
        br = 70;
        bg = 88;
        bb = 50;
    }
    r = clamp8(br + (n - 0.5) * 16.0 + (n2 - 0.5) * 8.0);
    g = clamp8(bg + (n2 - 0.5) * 14.0);
    b = clamp8(bb + (n - 0.5) * 10.0);
}

bool write_kjfk_farfield_subset(const std::string& dir) {
    if (dir.empty())
        return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    // Joey hangar layout: root + lod0/lod1/lod2 leaves. Public land_coverage
    // paints Jamaica Bay / Rockaway / Queens so a 2.99 nm shot has a far field.
    const double arp_lat = 40.639925;
    const double arp_lon = -73.778694;
    struct LodBox {
        int lod;
        double r_km;
        int n;
    };
    const LodBox lods[] = {{0, 8.0, 64}, {1, 28.0, 48}, {2, kFarFieldKm, 64}};
    auto paint = [&](DemGrid& dem, ImageRgb& img) {
        img.w = dem.w;
        img.h = dem.h;
        img.rgb.assign(static_cast<std::size_t>(dem.w * dem.h * 3), 0);
        for (int y = 0; y < dem.h; ++y) {
            const double lat =
                dem.north - (dem.north - dem.south) * y / std::max(1, dem.h - 1);
            for (int x = 0; x < dem.w; ++x) {
                const double lon =
                    dem.west + (dem.east - dem.west) * x / std::max(1, dem.w - 1);
                std::uint8_t r = 0, g = 0, b = 0;
                bool water = false;
                kjfk_subset_land_rgb(lat, lon, r, g, b, water);
                const bool jfk_pad =
                    lat > 40.626 && lat < 40.662 && lon > -73.828 && lon < -73.752;
                if (jfk_pad && !water)
                    dem.z_m[static_cast<std::size_t>(y * dem.w + x)] = 4.f;
                else
                    dem.z_m[static_cast<std::size_t>(y * dem.w + x)] = water ? 0.f : 4.f;
                const int o = (y * img.w + x) * 3;
                img.rgb[static_cast<std::size_t>(o + 0)] = r;
                img.rgb[static_cast<std::size_t>(o + 1)] = g;
                img.rgb[static_cast<std::size_t>(o + 2)] = b;
            }
        }
    };
    CacheRegion root;
    root.name = "KJFK";
    for (const auto& L : lods) {
        const double rlat = L.r_km / 111.32;
        const double rlon = rlat / std::max(0.2, std::cos(geo::deg2rad(arp_lat)));
        CacheRegion c;
        c.name = "KJFK";
        c.dem.w = L.n;
        c.dem.h = L.n;
        c.dem.west = arp_lon - rlon;
        c.dem.east = arp_lon + rlon;
        c.dem.south = arp_lat - rlat;
        c.dem.north = arp_lat + rlat;
        c.dem.z_m.assign(static_cast<std::size_t>(L.n * L.n), 0.f);
        paint(c.dem, c.imagery);
        const std::string leaf = dir + "/lod" + std::to_string(L.lod) + "/t0_0";
        write_cache(c, leaf);
        if (L.lod == 2)
            root = std::move(c);
    }
    write_cache(root, dir);
    {
        std::ofstream a(dir + "/ATTRIBUTION.txt");
        a << "s376-visual KJFK far-field test subset\n";
        a << "land_coverage (public GLOBE-style land/water) + GLO-30-like DEM\n";
        a << "Not the hangar 12 GB 1 m pack. Same directory layout.\n";
    }
    {
        std::ofstream c(dir + "/coverage.toml");
        c << "icao = \"KJFK\"\n";
        c << "covers_faa = true\n";
        c << "lod0_west = " << (arp_lon - 4.0 / 111.32 / 0.76) << "\n";
        c << "lod0_south = " << (arp_lat - 4.0 / 111.32) << "\n";
        c << "lod0_east = " << (arp_lon + 4.0 / 111.32 / 0.76) << "\n";
        c << "lod0_north = " << (arp_lat + 4.0 / 111.32) << "\n";
        c << "lod2_radius_km = " << kFarFieldKm << "\n";
    }
    return fs::exists(dir + "/lod0/t0_0/elev.s376dem") &&
           fs::exists(dir + "/lod1/t0_0/elev.s376dem") &&
           fs::exists(dir + "/lod2/t0_0/elev.s376dem") &&
           fs::exists(dir + "/ATTRIBUTION.txt") && fs::exists(dir + "/coverage.toml") &&
           fs::exists(dir + "/region.toml");
}

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
static bool win_process_memory(PROCESS_MEMORY_COUNTERS& pmc) {
    pmc = {};
    pmc.cb = sizeof(pmc);
    return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) != 0;
}
#endif

static std::uint64_t proc_status_kb(const char* key) {
#ifndef _WIN32
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key, 0) != 0)
            continue;
        std::size_t i = 0;
        while (i < line.size() && (line[i] < '0' || line[i] > '9'))
            ++i;
        if (i >= line.size())
            return 0;
        return std::strtoull(line.c_str() + i, nullptr, 10);
    }
#else
    (void)key;
#endif
    return 0;
}

std::uint64_t current_rss_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (win_process_memory(pmc))
        return static_cast<std::uint64_t>(pmc.WorkingSetSize);
    return 0;
#else
    return proc_status_kb("VmRSS:") * 1024ull;
#endif
}
std::uint64_t peak_rss_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (win_process_memory(pmc))
        return static_cast<std::uint64_t>(pmc.PeakWorkingSetSize);
    return 0;
#else
    const auto kb = proc_status_kb("VmHWM:");
    return kb * 1024ull;
#endif
}

void limit_gdal_cache_mb(int mb) {
#ifdef S376_HAS_GDAL
    GDALAllRegister();
    const int cap = std::max(16, mb);
    CPLSetConfigOption("GDAL_CACHEMAX", std::to_string(cap).c_str());
#else
    (void)mb;
#endif
}

} // namespace s376
