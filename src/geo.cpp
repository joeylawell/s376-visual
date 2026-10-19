#include "s376/geo.hpp"

#include <algorithm>
#include <cmath>

namespace s376::geo {

Ecef lla_to_ecef(double lat_deg, double lon_deg, double alt_m) {
    const double lat = deg2rad(lat_deg);
    const double lon = deg2rad(lon_deg);
    const double sl = std::sin(lat), cl = std::cos(lat);
    const double e2 = 1.0 - (kWgsB * kWgsB) / (kWgsA * kWgsA);
    const double n = kWgsA / std::sqrt(1.0 - e2 * sl * sl);
    Ecef p;
    p.x = (n + alt_m) * cl * std::cos(lon);
    p.y = (n + alt_m) * cl * std::sin(lon);
    p.z = (n * (1.0 - e2) + alt_m) * sl;
    return p;
}

void ecef_to_enu_axes(double lat_deg, double lon_deg, Ecef& east, Ecef& north, Ecef& up) {
    const double lat = deg2rad(lat_deg);
    const double lon = deg2rad(lon_deg);
    const double sl = std::sin(lat), cl = std::cos(lat);
    const double so = std::sin(lon), co = std::cos(lon);
    east = {-so, co, 0};
    north = {-sl * co, -sl * so, cl};
    up = {cl * co, cl * so, sl};
}

void body_to_enu(double heading_deg, double pitch_deg, double roll_deg, Enu& fwd, Enu& right,
                 Enu& down) {
    const double h = deg2rad(heading_deg);
    const double p = deg2rad(pitch_deg);
    const double r = deg2rad(roll_deg);
    const double ch = std::cos(h), sh = std::sin(h);
    const double cp = std::cos(p), sp = std::sin(p);
    const double cr = std::cos(r), sr = std::sin(r);
    // Heading clockwise from north, pitch nose-up, roll right-wing-down.
    // Forward (body +X)
    fwd.n = cp * ch;
    fwd.e = cp * sh;
    fwd.u = sp;
    // Right (body +Y)
    right.n = sr * sp * ch - cr * sh;
    right.e = sr * sp * sh + cr * ch;
    right.u = -sr * cp;
    // Down (body +Z)
    down.n = cr * sp * ch + sr * sh;
    down.e = cr * sp * sh - sr * ch;
    down.u = -cr * cp;
}

int day_of_year(int year, int month, int day) {
    static const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int d = day;
    for (int m = 1; m < month && m <= 12; ++m)
        d += mdays[m];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (leap && month > 2)
        ++d;
    if (d < 1)
        d = 1;
    if (d > 366)
        d = 366;
    return d;
}

Ecef sun_from_elev_az(double lat_deg, double lon_deg, double elev_deg, double az_deg) {
    const double el = deg2rad(elev_deg);
    const double az = deg2rad(az_deg);
    Enu s;
    s.n = std::cos(el) * std::cos(az);
    s.e = std::cos(el) * std::sin(az);
    s.u = std::sin(el);
    Ecef east, north, up;
    ecef_to_enu_axes(lat_deg, lon_deg, east, north, up);
    Ecef dir;
    dir.x = s.e * east.x + s.n * north.x + s.u * up.x;
    dir.y = s.e * east.y + s.n * north.y + s.u * up.y;
    dir.z = s.e * east.z + s.n * north.z + s.u * up.z;
    const double mag = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (mag > 0) {
        dir.x /= mag;
        dir.y /= mag;
        dir.z /= mag;
    }
    return dir;
}

double julian_date(int year, int month, int day, double hours_utc) {
    int y = year;
    int m = month;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    const int A = y / 100;
    const int B = 2 - A + A / 4;
    return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + day + B - 1524.5 +
           hours_utc / 24.0;
}

double civil_utc_offset_hours(double lon_deg, int year, int month, int day) {
    // US Eastern when the eye is around NYC / the east coast.
    if (lon_deg > -85.0 && lon_deg < -66.0) {
        auto weekday = [](int y, int m, int d) {
            // Sakamoto: 0 = Sunday.
            static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            if (m < 3)
                y -= 1;
            return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
        };
        auto nth_sunday = [&](int y, int m, int n) {
            int d = 1;
            while (weekday(y, m, d) != 0)
                ++d;
            return d + 7 * (n - 1);
        };
        const int dst_start = nth_sunday(year, 3, 2); // 2nd Sunday March
        const int dst_end = nth_sunday(year, 11, 1);  // 1st Sunday November
        bool dst = false;
        if (month > 3 && month < 11)
            dst = true;
        else if (month == 3 && day >= dst_start)
            dst = true;
        else if (month == 11 && day < dst_end)
            dst = true;
        return dst ? -4.0 : -5.0;
    }
    return std::round(lon_deg / 15.0);
}

namespace {

void solar_elev_az(double lat_deg, double lon_deg, double utc_hours, int month, int day, int year,
                   double& elev_deg, double& az_deg) {
    const int y = year <= 0 ? 2000 : year;
    const int mo = month <= 0 ? 6 : month;
    const int da = day <= 0 ? 21 : day;
    const double jd = julian_date(y, mo, da, utc_hours);
    const double n = jd - 2451545.0;
    const double L = wrap360(280.460 + 0.9856474 * n);
    const double g = deg2rad(wrap360(357.528 + 0.9856003 * n));
    const double lambda = deg2rad(wrap360(L + 1.915 * std::sin(g) + 0.020 * std::sin(2.0 * g)));
    const double eps = deg2rad(23.439 - 0.0000004 * n);
    const double ra = rad2deg(std::atan2(std::cos(eps) * std::sin(lambda), std::cos(lambda)));
    const double dec = std::asin(std::sin(eps) * std::sin(lambda));
    const double gmst = wrap360(15.0 * (18.697374558 + 24.06570982441908 * n));
    const double lst = wrap360(gmst + lon_deg);
    const double ha = deg2rad(wrap180(lst - ra));
    const double lat = deg2rad(lat_deg);
    const double sin_el =
        std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
    const double el = std::asin(std::max(-1.0, std::min(1.0, sin_el)));
    double az = 0;
    const double cos_el = std::cos(el);
    if (std::fabs(cos_el) > 1e-6) {
        const double cos_az =
            (std::sin(dec) - std::sin(el) * std::sin(lat)) / (cos_el * std::cos(lat));
        az = std::acos(std::max(-1.0, std::min(1.0, cos_az)));
        if (ha > 0)
            az = 2.0 * kPi - az;
    }
    elev_deg = rad2deg(el);
    az_deg = rad2deg(az);
}

void lunar_elev_az(double lat_deg, double lon_deg, double utc_hours, int month, int day, int year,
                   double& elev_deg, double& az_deg) {
    const int y = year <= 0 ? 2000 : year;
    const int mo = month <= 0 ? 6 : month;
    const int da = day <= 0 ? 21 : day;
    const double jd = julian_date(y, mo, da, utc_hours);
    const double d = jd - 2451545.0;
    const double L = wrap360(218.316 + 13.176396 * d);
    const double M = deg2rad(wrap360(134.963 + 13.064993 * d));
    const double F = deg2rad(wrap360(93.272 + 13.229350 * d));
    const double lon_ecl = deg2rad(wrap360(L + 6.289 * std::sin(M)));
    const double lat_ecl = deg2rad(5.128 * std::sin(F));
    const double eps = deg2rad(23.439 - 0.0000004 * d);
    const double dec = std::asin(std::sin(eps) * std::cos(lat_ecl) * std::sin(lon_ecl) +
                                 std::cos(eps) * std::sin(lat_ecl));
    const double ra = rad2deg(std::atan2(
        std::cos(eps) * std::sin(lon_ecl) - std::tan(lat_ecl) * std::sin(eps), std::cos(lon_ecl)));
    const double gmst = wrap360(15.0 * (18.697374558 + 24.06570982441908 * d));
    const double lst = wrap360(gmst + lon_deg);
    const double ha = deg2rad(wrap180(lst - ra));
    const double lat = deg2rad(lat_deg);
    const double sin_el =
        std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
    const double el = std::asin(std::max(-1.0, std::min(1.0, sin_el)));
    double az = 0;
    const double cos_el = std::cos(el);
    if (std::fabs(cos_el) > 1e-6) {
        const double cos_az =
            (std::sin(dec) - std::sin(el) * std::sin(lat)) / (cos_el * std::cos(lat));
        az = std::acos(std::max(-1.0, std::min(1.0, cos_az)));
        if (ha > 0)
            az = 2.0 * kPi - az;
    }
    elev_deg = rad2deg(el);
    az_deg = rad2deg(az);
}

} // namespace

Ecef sun_ecef(double lat_deg, double lon_deg, double tod_hours, int month, int day,
              double* elev_deg_out) {
    // Legacy: host TOD treated as UTC, year unused (kept for existing tests).
    const int doy = day_of_year(2000, month <= 0 ? 6 : month, day <= 0 ? 21 : day);
    const double decl = deg2rad(23.44 * std::sin(2.0 * kPi * (doy - 81) / 365.0));
    const double hour_angle = deg2rad(15.0 * (tod_hours - 12.0) + lon_deg);
    const double lat = deg2rad(lat_deg);
    const double sin_el =
        std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(hour_angle);
    const double el = std::asin(std::max(-1.0, std::min(1.0, sin_el)));
    if (elev_deg_out)
        *elev_deg_out = rad2deg(el);
    const double cos_el = std::cos(el);
    double az = 0;
    if (std::fabs(cos_el) > 1e-6) {
        const double cos_az =
            (std::sin(decl) - std::sin(el) * std::sin(lat)) / (cos_el * std::cos(lat));
        az = std::acos(std::max(-1.0, std::min(1.0, cos_az)));
        if (hour_angle > 0)
            az = 2.0 * kPi - az;
    }
    return sun_from_elev_az(lat_deg, lon_deg, rad2deg(el), rad2deg(az));
}

Ecef sun_ecef_civil(double lat_deg, double lon_deg, double local_hours, int month, int day, int year,
                    double* elev_deg_out) {
    const double utc = local_hours - civil_utc_offset_hours(lon_deg, year, month, day);
    double el = 0, az = 0;
    solar_elev_az(lat_deg, lon_deg, utc, month, day, year, el, az);
    if (elev_deg_out)
        *elev_deg_out = el;
    return sun_from_elev_az(lat_deg, lon_deg, el, az);
}

Ecef moon_ecef(double lat_deg, double lon_deg, double utc_hours, int month, int day, int year,
               double* elev_deg_out) {
    double el = 0, az = 0;
    lunar_elev_az(lat_deg, lon_deg, utc_hours, month, day, year, el, az);
    if (elev_deg_out)
        *elev_deg_out = el;
    return sun_from_elev_az(lat_deg, lon_deg, el, az);
}

Ecef moon_ecef_civil(double lat_deg, double lon_deg, double local_hours, int month, int day,
                     int year, double* elev_deg_out) {
    const double utc = local_hours - civil_utc_offset_hours(lon_deg, year, month, day);
    return moon_ecef(lat_deg, lon_deg, utc, month, day, year, elev_deg_out);
}

ChannelView make_channel_view(double lat_deg, double lon_deg, double alt_m, double true_hdg_deg,
                              double pitch_deg, double roll_deg, double heading_offset_deg,
                              double pitch_offset_deg, double roll_offset_deg, double hfov_deg,
                              double vfov_deg, int width, int height, double eye_fwd_m,
                              double eye_right_m, double eye_up_m) {
    ChannelView v;
    v.eye = lla_to_ecef(lat_deg, lon_deg, alt_m);
    if (eye_fwd_m != 0.0 || eye_right_m != 0.0 || eye_up_m != 0.0) {
        Enu bf, br, bd;
        body_to_enu(true_hdg_deg, pitch_deg, roll_deg, bf, br, bd);
        Ecef east, north, up;
        ecef_to_enu_axes(lat_deg, lon_deg, east, north, up);
        auto add = [&](const Enu& b, double s) {
            v.eye.x += s * (b.e * east.x + b.n * north.x + b.u * up.x);
            v.eye.y += s * (b.e * east.y + b.n * north.y + b.u * up.y);
            v.eye.z += s * (b.e * east.z + b.n * north.z + b.u * up.z);
        };
        add(bf, eye_fwd_m);
        add(br, eye_right_m);
        add(bd, -eye_up_m);
    }
    const double hdg = true_hdg_deg + heading_offset_deg;
    const double pit = pitch_deg + pitch_offset_deg;
    const double rol = roll_deg + roll_offset_deg;
    v.heading_deg = hdg;
    Enu fwd, right, down;
    body_to_enu(hdg, pit, rol, fwd, right, down);
    Ecef east, north, up;
    ecef_to_enu_axes(lat_deg, lon_deg, east, north, up);
    auto to_ecef = [&](const Enu& b) {
        Ecef o;
        o.x = b.e * east.x + b.n * north.x + b.u * up.x;
        o.y = b.e * east.y + b.n * north.y + b.u * up.y;
        o.z = b.e * east.z + b.n * north.z + b.u * up.z;
        return o;
    };
    v.forward = to_ecef(fwd);
    const Ecef d = to_ecef(down);
    v.up = {-d.x, -d.y, -d.z};
    v.hfov_deg = hfov_deg;
    v.vfov_deg = vfov_deg;
    v.aspect = static_cast<double>(std::max(1, width)) / static_cast<double>(std::max(1, height));
    v.near_m = std::max(1.0, std::min(40.0, alt_m * 0.008));
    v.far_m = 2.0e7;
    return v;
}

ProjectedPoint project_channel(const ChannelView& v, const Ecef& world, int width, int height) {
    ProjectedPoint o;
    const double dx = world.x - v.eye.x;
    const double dy = world.y - v.eye.y;
    const double dz = world.z - v.eye.z;
    const double fwd = dx * v.forward.x + dy * v.forward.y + dz * v.forward.z;
    o.depth_m = fwd;
    o.in_front = fwd > v.near_m;
    if (!(fwd > 1e-6))
        return o;
    const double rx = v.forward.y * v.up.z - v.forward.z * v.up.y;
    const double ry = v.forward.z * v.up.x - v.forward.x * v.up.z;
    const double rz = v.forward.x * v.up.y - v.forward.y * v.up.x;
    const double rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (!(rlen > 1e-12))
        return o;
    const double right = (dx * rx + dy * ry + dz * rz) / rlen;
    const double up = dx * v.up.x + dy * v.up.y + dz * v.up.z;
    const double th = std::tan(deg2rad(std::max(1.0, v.hfov_deg) * 0.5));
    const double tv = std::tan(deg2rad(std::max(1.0, v.vfov_deg) * 0.5));
    o.ndc_x = right / (fwd * th);
    o.ndc_y = up / (fwd * tv);
    const int w = std::max(1, width);
    const int h = std::max(1, height);
    o.pix_x = (o.ndc_x * 0.5 + 0.5) * static_cast<double>(w);
    o.pix_y = (0.5 - o.ndc_y * 0.5) * static_cast<double>(h); // top-left origin
    o.in_frame = o.in_front && std::fabs(o.ndc_x) <= 1.0 && std::fabs(o.ndc_y) <= 1.0;
    return o;
}

ProjectedPoint project_lla(const ChannelView& v, double lat_deg, double lon_deg, double alt_m,
                           int width, int height) {
    return project_channel(v, lla_to_ecef(lat_deg, lon_deg, alt_m), width, height);
}

namespace {

float clampf(float x, float a, float b) {
    return x < a ? a : (x > b ? b : x);
}

float smooth01(float e0, float e1, float x) {
    if (e1 <= e0)
        return x >= e1 ? 1.f : 0.f;
    const float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float soft_box(double lat, double lon, double lat0, double lat1, double lon0, double lon1,
               double fade) {
    const float a = smooth01(static_cast<float>(lat0 - fade), static_cast<float>(lat0),
                             static_cast<float>(lat)) *
                    (1.f - smooth01(static_cast<float>(lat1), static_cast<float>(lat1 + fade),
                                    static_cast<float>(lat)));
    const float b = smooth01(static_cast<float>(lon0 - fade), static_cast<float>(lon0),
                             static_cast<float>(lon)) *
                    (1.f - smooth01(static_cast<float>(lon1), static_cast<float>(lon1 + fade),
                                    static_cast<float>(lon)));
    return a * b;
}

double lerp_coast(double lat, const double* lats, const double* lons, int n) {
    if (lat <= lats[0])
        return lons[0];
    if (lat >= lats[n - 1])
        return lons[n - 1];
    for (int i = 0; i < n - 1; ++i) {
        if (lat <= lats[i + 1]) {
            const double t = (lat - lats[i]) / (lats[i + 1] - lats[i]);
            return lons[i] + t * (lons[i + 1] - lons[i]);
        }
    }
    return lons[n - 1];
}

} // namespace

float land_coverage(double lat, double lon) {
    float land = 0.f;
    land = std::max(land, soft_box(lat, lon, -55.0, 14.0, -82.0, -34.0, 0.7)); // S America
    land = std::max(land, soft_box(lat, lon, 35.0, 71.5, -10.0, 40.0, 0.5));   // Europe
    land = std::max(land, soft_box(lat, lon, -35.0, 37.0, -18.0, 52.0, 0.6));  // Africa
    land = std::max(land, soft_box(lat, lon, 5.0, 75.0, 40.0, 180.0, 0.6));    // Asia
    land = std::max(land, soft_box(lat, lon, -45.0, -10.0, 112.0, 154.0, 0.5)); // Australia
    land = std::max(land, soft_box(lat, lon, 50.0, 72.0, -140.0, -55.0, 0.8)); // Canada shield

    // North America west of a soft east-coast polyline (so the Atlantic is ocean).
    static const double clat[] = {15.0, 24.5, 29.8, 33.0, 35.6, 38.6, 40.15, 40.85, 42.35, 44.6, 47.2, 51.0};
    static const double clon[] = {-97.8, -80.5, -81.5, -79.1, -75.5, -75.05, -74.08, -73.92, -70.75, -67.9, -67.6, -55.0};
    if (lat > 14.0 && lat < 73.0 && lon > -170.0 && lon < -50.0) {
        const double coast = lerp_coast(lat, clat, clon, 12);
        float na = 1.f - smooth01(static_cast<float>(coast - 0.14), static_cast<float>(coast + 0.14),
                                  static_cast<float>(lon));
        na *= smooth01(15.f, 16.5f, static_cast<float>(lat)) *
              (1.f - smooth01(71.5f, 73.5f, static_cast<float>(lat)));
        na *= smooth01(-168.f, -164.f, static_cast<float>(lon));
        land = std::max(land, na);
    }

    // Long Island + Rockaway barrier (KJFK photos). Soft so coasts are not stairs.
    land = std::max(land, soft_box(lat, lon, 40.58, 41.18, -74.06, -71.86, 0.03));
    land = std::min(land, 1.f - 0.96f * soft_box(lat, lon, 40.598, 40.652, -73.900, -73.778, 0.008));
    // South/east Jamaica Bay — 04L/04R short final sits over water, not the LI box.
    land = std::min(land, 1.f - 0.96f * soft_box(lat, lon, 40.588, 40.625, -73.812, -73.742, 0.005));
    land = std::max(land, soft_box(lat, lon, 40.545, 40.588, -73.96, -73.73, 0.008)); // Rockaway
    land = std::max(land, soft_box(lat, lon, 40.626, 40.662, -73.828, -73.752, 0.006)); // JFK field
    land = std::max(land, soft_box(lat, lon, 41.52, 42.10, -70.62, -69.90, 0.04));     // Cape Cod
    land = std::max(land, soft_box(lat, lon, 42.20, 42.48, -71.20, -70.85, 0.03));     // Boston neck

    // West-coast bays (keep the airport pads as land).
    float sf = soft_box(lat, lon, 37.48, 38.10, -122.53, -122.20, 0.025);
    sf *= 1.f - soft_box(lat, lon, 37.598, 37.640, -122.402, -122.348, 0.008);
    land = std::min(land, 1.f - 0.88f * sf);
    float ps = soft_box(lat, lon, 47.32, 48.18, -122.58, -122.30, 0.03);
    ps *= 1.f - soft_box(lat, lon, 47.428, 47.478, -122.325, -122.288, 0.008);
    land = std::min(land, 1.f - 0.72f * ps);

    land = std::max(land, smooth01(73.f, 77.f, static_cast<float>(lat)));
    land = std::max(land, 1.f - smooth01(-62.f, -56.f, static_cast<float>(lat)));
    return clampf(land, 0.f, 1.f);
}

void ecef_to_lla(const Ecef& p, double& lat_deg, double& lon_deg, double& alt_m) {
    const double e2 = 1.0 - (kWgsB * kWgsB) / (kWgsA * kWgsA);
    const double ep2 = (kWgsA * kWgsA - kWgsB * kWgsB) / (kWgsB * kWgsB);
    const double pxy = std::sqrt(p.x * p.x + p.y * p.y);
    lon_deg = rad2deg(std::atan2(p.y, p.x));
    double th = std::atan2(p.z * kWgsA, pxy * kWgsB);
    double lat = std::atan2(p.z + ep2 * kWgsB * std::pow(std::sin(th), 3.0),
                           pxy - e2 * kWgsA * std::pow(std::cos(th), 3.0));
    for (int i = 0; i < 4; ++i) {
        const double sl = std::sin(lat);
        const double n = kWgsA / std::sqrt(1.0 - e2 * sl * sl);
        alt_m = pxy / std::max(1e-15, std::cos(lat)) - n;
        lat = std::atan2(p.z, pxy * (1.0 - e2 * n / (n + alt_m)));
    }
    lat_deg = rad2deg(lat);
    const double sl = std::sin(lat);
    const double n = kWgsA / std::sqrt(1.0 - e2 * sl * sl);
    alt_m = pxy / std::max(1e-15, std::cos(lat)) - n;
}

double wrap360(double deg) {
    while (deg < 0.0)
        deg += 360.0;
    while (deg >= 360.0)
        deg -= 360.0;
    return deg;
}

double wrap180(double deg) {
    deg = wrap360(deg + 180.0);
    return deg - 180.0;
}

double dms_to_deg(int d, int m, double s, bool west_or_south) {
    double v = std::fabs(static_cast<double>(d)) + m / 60.0 + s / 3600.0;
    if (d < 0 || west_or_south)
        v = -v;
    return v;
}

namespace {

struct GeoidSample {
    double lat, lon, n96, n08;
};

// Public NGA-style undulation samples (metres). Enough to interpolate
// CONUS airports and a couple of overseas pads.
const GeoidSample kGeoid[] = {
    {40.64, -73.78, -32.05, -31.88}, // KJFK
    {40.69, -74.17, -32.35, -32.18}, // KEWR
    {40.78, -73.87, -31.90, -31.74}, // KLGA
    {42.37, -71.01, -27.60, -27.48}, // KBOS
    {47.45, -122.31, -22.85, -22.70}, // KSEA
    {37.62, -122.38, -32.40, -32.20}, // KSFO
    {33.94, -118.41, -35.10, -34.90}, // KLAX
    {36.08, -115.15, -21.40, -21.20}, // KLAS
    {51.47, -0.45, 45.40, 45.55},     // EGLL
    {50.04, 8.56, 47.80, 47.95},      // EDDF
    {0.0, 0.0, 17.20, 17.40},
    {90.0, 0.0, 13.60, 14.90},
};

double interp_geoid(double lat, double lon, bool egm08) {
    double wsum = 0, nsum = 0;
    for (const auto& s : kGeoid) {
        const double dlat = (lat - s.lat) * 111.32;
        const double dlon = (lon - s.lon) * 111.32 * std::max(0.2, std::cos(deg2rad(lat)));
        const double d2 = dlat * dlat + dlon * dlon;
        const double w = 1.0 / (d2 + 0.25);
        nsum += w * (egm08 ? s.n08 : s.n96);
        wsum += w;
    }
    return nsum / std::max(wsum, 1e-12);
}

} // namespace

double geoid_undulation_m(double lat_deg, double lon_deg, GeoidModel model) {
    return interp_geoid(lat_deg, lon_deg, model == GeoidModel::Egm2008);
}

double orthometric_to_ellipsoid_m(double orthometric_m, double lat_deg, double lon_deg,
                                  GeoidModel model) {
    return orthometric_m + geoid_undulation_m(lat_deg, lon_deg, model);
}

double ellipsoid_to_orthometric_m(double ellipsoid_m, double lat_deg, double lon_deg,
                                  GeoidModel model) {
    return ellipsoid_m - geoid_undulation_m(lat_deg, lon_deg, model);
}

double inverse_wgs84(double lat1, double lon1, double lat2, double lon2, double* heading_deg) {
    // Vincenty inverse on WGS84.
    const double f = (kWgsA - kWgsB) / kWgsA;
    const double L = deg2rad(lon2 - lon1);
    const double u1 = std::atan((1.0 - f) * std::tan(deg2rad(lat1)));
    const double u2 = std::atan((1.0 - f) * std::tan(deg2rad(lat2)));
    const double su1 = std::sin(u1), cu1 = std::cos(u1);
    const double su2 = std::sin(u2), cu2 = std::cos(u2);
    double lam = L, lam_prev = 0, sin_s = 0, cos_s = 0, sig = 0, sin_a = 0, cos2a = 0,
           cos2sm = 0;
    for (int i = 0; i < 80; ++i) {
        const double sl = std::sin(lam), cl = std::cos(lam);
        sin_s = std::sqrt((cu2 * sl) * (cu2 * sl) +
                          (cu1 * su2 - su1 * cu2 * cl) * (cu1 * su2 - su1 * cu2 * cl));
        if (sin_s < 1e-14) {
            if (heading_deg)
                *heading_deg = 0;
            return 0;
        }
        cos_s = su1 * su2 + cu1 * cu2 * cl;
        sig = std::atan2(sin_s, cos_s);
        sin_a = cu1 * cu2 * sl / sin_s;
        cos2a = 1.0 - sin_a * sin_a;
        cos2sm = (cos2a > 1e-14) ? (cos_s - 2.0 * su1 * su2 / cos2a) : 0.0;
        const double c = f / 16.0 * cos2a * (2.0 + f * (4.0 - 3.0 * cos2a));
        lam_prev = lam;
        lam = L + (1.0 - c) * f * sin_a *
                      (sig + c * sin_s * (cos2sm + c * cos_s * (-1.0 + 2.0 * cos2sm * cos2sm)));
        if (std::fabs(lam - lam_prev) < 1e-12)
            break;
    }
    const double u2s = cos2a * (kWgsA * kWgsA - kWgsB * kWgsB) / (kWgsB * kWgsB);
    const double a = 1.0 + u2s / 16384.0 * (4096.0 + u2s * (-768.0 + u2s * (320.0 - 175.0 * u2s)));
    const double b = u2s / 1024.0 * (256.0 + u2s * (-128.0 + u2s * (74.0 - 47.0 * u2s)));
    const double dsig =
        b * sin_s *
        (cos2sm +
         b / 4.0 *
             (cos_s * (-1.0 + 2.0 * cos2sm * cos2sm) -
              b / 6.0 * cos2sm * (-3.0 + 4.0 * sin_s * sin_s) * (-3.0 + 4.0 * cos2sm * cos2sm)));
    const double dist = kWgsB * a * (sig - dsig);
    if (heading_deg) {
        const double sl = std::sin(lam), cl = std::cos(lam);
        *heading_deg = wrap360(rad2deg(std::atan2(cu2 * sl, cu1 * su2 - su1 * cu2 * cl)));
    }
    return dist;
}

void direct_wgs84(double lat1, double lon1, double heading_deg, double dist_m, double& lat2,
                  double& lon2, double* heading2_deg) {
    if (!std::isfinite(dist_m) || std::fabs(dist_m) < 1e-9) {
        lat2 = lat1;
        lon2 = lon1;
        if (heading2_deg)
            *heading2_deg = wrap360(heading_deg);
        return;
    }
    if (dist_m < 0.0) {
        direct_wgs84(lat1, lon1, wrap360(heading_deg + 180.0), -dist_m, lat2, lon2, heading2_deg);
        if (heading2_deg)
            *heading2_deg = wrap360(*heading2_deg + 180.0);
        return;
    }
    const double f = (kWgsA - kWgsB) / kWgsA;
    const double phi1 = deg2rad(lat1);
    const double alpha1 = deg2rad(heading_deg);
    const double sin_alpha1 = std::sin(alpha1);
    const double cos_alpha1 = std::cos(alpha1);
    const double tan_u1 = (1.0 - f) * std::tan(phi1);
    const double cos_u1 = 1.0 / std::sqrt(1.0 + tan_u1 * tan_u1);
    const double sin_u1 = tan_u1 * cos_u1;
    const double sigma1 = std::atan2(tan_u1, cos_alpha1);
    const double sin_alpha = cos_u1 * sin_alpha1;
    const double cos2_alpha = 1.0 - sin_alpha * sin_alpha;
    const double u2 = cos2_alpha * (kWgsA * kWgsA - kWgsB * kWgsB) / (kWgsB * kWgsB);
    const double A = 1.0 + u2 / 16384.0 * (4096.0 + u2 * (-768.0 + u2 * (320.0 - 175.0 * u2)));
    const double B = u2 / 1024.0 * (256.0 + u2 * (-128.0 + u2 * (74.0 - 47.0 * u2)));
    double sigma = dist_m / (kWgsB * A);
    double sin_sigma = 0, cos_sigma = 0, cos2sm = 0;
    for (int i = 0; i < 80; ++i) {
        const double sigma_prev = sigma;
        cos2sm = std::cos(2.0 * sigma1 + sigma);
        sin_sigma = std::sin(sigma);
        cos_sigma = std::cos(sigma);
        const double dsig =
            B * sin_sigma *
            (cos2sm +
             B / 4.0 *
                 (cos_sigma * (-1.0 + 2.0 * cos2sm * cos2sm) -
                  B / 6.0 * cos2sm * (-3.0 + 4.0 * sin_sigma * sin_sigma) *
                      (-3.0 + 4.0 * cos2sm * cos2sm)));
        sigma = dist_m / (kWgsB * A) + dsig;
        if (std::fabs(sigma - sigma_prev) < 1e-12)
            break;
    }
    const double tmp = sin_u1 * sin_sigma - cos_u1 * cos_sigma * cos_alpha1;
    const double phi2 = std::atan2(sin_u1 * cos_sigma + cos_u1 * sin_sigma * cos_alpha1,
                                   (1.0 - f) * std::sqrt(sin_alpha * sin_alpha + tmp * tmp));
    const double lam = std::atan2(sin_sigma * sin_alpha1,
                                  cos_u1 * cos_sigma - sin_u1 * sin_sigma * cos_alpha1);
    const double C = f / 16.0 * cos2_alpha * (2.0 + f * (4.0 - 3.0 * cos2_alpha));
    const double L = lam - (1.0 - C) * f * sin_alpha *
                               (sigma + C * sin_sigma * (cos2sm + C * cos_sigma * (-1.0 + 2.0 * cos2sm * cos2sm)));
    lat2 = rad2deg(phi2);
    lon2 = lon1 + rad2deg(L);
    while (lon2 < -180.0)
        lon2 += 360.0;
    while (lon2 > 180.0)
        lon2 -= 360.0;
    if (heading2_deg) {
        *heading2_deg = wrap360(rad2deg(std::atan2(sin_alpha, -tmp)));
    }
}

double ground_distance_m(double lat1, double lon1, double lat2, double lon2) {
    return inverse_wgs84(lat1, lon1, lat2, lon2, nullptr);
}

Utm wgs84_to_utm(double lat_deg, double lon_deg) {
    Utm u;
    u.zone = static_cast<int>(std::floor((lon_deg + 180.0) / 6.0)) + 1;
    u.north = lat_deg >= 0;
    const double lat = deg2rad(lat_deg);
    const double lon0 = deg2rad((u.zone - 1) * 6 - 180 + 3);
    const double e2 = 1.0 - (kWgsB * kWgsB) / (kWgsA * kWgsA);
    const double ep2 = e2 / (1.0 - e2);
    const double sl = std::sin(lat), cl = std::cos(lat), tl = std::tan(lat);
    const double n = kWgsA / std::sqrt(1.0 - e2 * sl * sl);
    const double t = tl * tl;
    const double c = ep2 * cl * cl;
    const double A = cl * (deg2rad(lon_deg) - lon0);
    const double m =
        kWgsA *
        ((1.0 - e2 / 4.0 - 3.0 * e2 * e2 / 64.0 - 5.0 * e2 * e2 * e2 / 256.0) * lat -
         (3.0 * e2 / 8.0 + 3.0 * e2 * e2 / 32.0 + 45.0 * e2 * e2 * e2 / 1024.0) * std::sin(2 * lat) +
         (15.0 * e2 * e2 / 256.0 + 45.0 * e2 * e2 * e2 / 1024.0) * std::sin(4 * lat) -
         (35.0 * e2 * e2 * e2 / 3072.0) * std::sin(6 * lat));
    const double k0 = 0.9996;
    u.e = k0 * n *
              (A + (1 - t + c) * A * A * A / 6.0 +
               (5 - 18 * t + t * t + 72 * c - 58 * ep2) * A * A * A * A * A / 120.0) +
          500000.0;
    u.n = k0 * (m + n * tl *
                        (A * A / 2.0 + (5 - t + 9 * c + 4 * c * c) * A * A * A * A / 24.0 +
                         (61 - 58 * t + t * t + 600 * c - 330 * ep2) * A * A * A * A * A * A / 720.0));
    if (!u.north)
        u.n += 10000000.0;
    return u;
}

void utm_to_wgs84(const Utm& u, double& lat_deg, double& lon_deg) {
    const double k0 = 0.9996;
    const double e2 = 1.0 - (kWgsB * kWgsB) / (kWgsA * kWgsA);
    const double ep2 = e2 / (1.0 - e2);
    const double e1 = (1.0 - std::sqrt(1.0 - e2)) / (1.0 + std::sqrt(1.0 - e2));
    double n = u.n;
    if (!u.north)
        n -= 10000000.0;
    const double m = n / k0;
    const double mu =
        m / (kWgsA * (1.0 - e2 / 4.0 - 3.0 * e2 * e2 / 64.0 - 5.0 * e2 * e2 * e2 / 256.0));
    const double j1 = 3.0 * e1 / 2.0 - 27.0 * e1 * e1 * e1 / 32.0;
    const double j2 = 21.0 * e1 * e1 / 16.0 - 55.0 * e1 * e1 * e1 * e1 / 32.0;
    const double j3 = 151.0 * e1 * e1 * e1 / 96.0;
    const double j4 = 1097.0 * e1 * e1 * e1 * e1 / 512.0;
    const double fp = mu + j1 * std::sin(2 * mu) + j2 * std::sin(4 * mu) + j3 * std::sin(6 * mu) +
                      j4 * std::sin(8 * mu);
    const double sl = std::sin(fp), cl = std::cos(fp);
    const double t = std::tan(fp);
    const double c = ep2 * cl * cl;
    const double r = kWgsA * (1.0 - e2) / std::pow(1.0 - e2 * sl * sl, 1.5);
    const double nn = kWgsA / std::sqrt(1.0 - e2 * sl * sl);
    const double d = (u.e - 500000.0) / (nn * k0);
    lat_deg = rad2deg(
        fp - (nn * t / r) *
                 (d * d / 2.0 - (5 + 3 * t * t + 10 * c - 4 * c * c - 9 * ep2) * d * d * d * d / 24.0 +
                  (61 + 90 * t * t + 298 * c + 45 * t * t * t * t - 252 * ep2 - 3 * c * c) *
                      d * d * d * d * d * d / 720.0));
    const double lon0 = (u.zone - 1) * 6 - 180 + 3;
    lon_deg = lon0 + rad2deg((d - (1 + 2 * t * t + c) * d * d * d / 6.0 +
                              (5 - 2 * c + 28 * t * t - 3 * c * c + 8 * ep2 + 24 * t * t * t * t) *
                                  d * d * d * d * d / 120.0) /
                             cl);
}

} // namespace s376::geo
