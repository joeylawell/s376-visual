#pragma once

namespace s376::geo {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kFtToM = 0.3048;
constexpr double kMToFt = 1.0 / kFtToM;
constexpr double kNmToM = 1852.0;
constexpr double kMToNm = 1.0 / kNmToM;
constexpr double kWgsA = 6378137.0;
constexpr double kWgsB = 6356752.314245;

inline double deg2rad(double d) {
    return d * kDeg;
}
inline double rad2deg(double r) {
    return r / kDeg;
}

struct Ecef {
    double x = 0, y = 0, z = 0;
};

struct Enu {
    double e = 0, n = 0, u = 0;
};

Ecef lla_to_ecef(double lat_deg, double lon_deg, double alt_m);
void ecef_to_lla(const Ecef& p, double& lat_deg, double& lon_deg, double& alt_m);
void ecef_to_enu_axes(double lat_deg, double lon_deg, Ecef& east, Ecef& north, Ecef& up);

enum class GeoidModel { Egm96, Egm2008 };

// Geoid undulation N (metres): ellipsoid height = orthometric + N.
// Coarse public-sample interpolation (NGA EGM96/EGM2008 published points).
double geoid_undulation_m(double lat_deg, double lon_deg, GeoidModel model = GeoidModel::Egm96);
double orthometric_to_ellipsoid_m(double orthometric_m, double lat_deg, double lon_deg,
                                  GeoidModel model = GeoidModel::Egm96);
double ellipsoid_to_orthometric_m(double ellipsoid_m, double lat_deg, double lon_deg,
                                  GeoidModel model = GeoidModel::Egm96);

// Vincenty inverse on WGS84. Returns distance metres; heading LE→HE clockwise from north.
double inverse_wgs84(double lat1, double lon1, double lat2, double lon2, double* heading_deg = nullptr);
// Vincenty direct: walk `dist_m` along `heading_deg` (true) on WGS-84.
void direct_wgs84(double lat1, double lon1, double heading_deg, double dist_m, double& lat2,
                  double& lon2, double* heading2_deg = nullptr);
double ground_distance_m(double lat1, double lon1, double lat2, double lon2);

struct Utm {
    int zone = 0;
    bool north = true;
    double e = 0;
    double n = 0;
};
Utm wgs84_to_utm(double lat_deg, double lon_deg);
void utm_to_wgs84(const Utm& u, double& lat_deg, double& lon_deg);

double wrap360(double deg);
double wrap180(double deg);
double dms_to_deg(int d, int m, double s, bool west_or_south = false);

// Body axes: x forward, y right, z down. Heading clockwise from north.
void body_to_enu(double heading_deg, double pitch_deg, double roll_deg, Enu& fwd, Enu& right,
                 Enu& down);

// Sun unit vector in ECEF from an ENU elevation / azimuth (az clockwise
// from north). Used by the visual TOD path and by sun_ecef.
Ecef sun_from_elev_az(double lat_deg, double lon_deg, double elev_deg, double az_deg);

// Astronomical sun (date + longitude). tod_hours is treated as UTC and
// longitude is added to the hour angle. Opt-in via render.solar_astronomy;
// the default IG path uses visual_sun_elev_deg so host dusk is dusk.
Ecef sun_ecef(double lat_deg, double lon_deg, double tod_hours, int month, int day,
              double* elev_deg_out = nullptr);

int day_of_year(int year, int month, int day);

// Shared-eyepoint channel view: the same geodetic eye, camera rotated by the
// channel heading/pitch/roll offsets. Heading offset is added to true
// heading (clockwise from north): Left −50°, Right +50°. Pitch/roll
// offsets stay 0 unless the config says otherwise. Symmetric frustum
// from HFOV×VFOV; the Raster Left flip is not applied here.
struct ChannelView {
    Ecef eye;
    Ecef forward;
    Ecef up;
    double heading_deg = 0;
    double vfov_deg = 32.5;
    double hfov_deg = 50.0;
    double aspect = 16.0 / 9.0;
    double near_m = 3.0;
    double far_m = 2.0e7;
};

ChannelView make_channel_view(double lat_deg, double lon_deg, double alt_m, double true_hdg_deg,
                              double pitch_deg, double roll_deg, double heading_offset_deg,
                              double pitch_offset_deg, double roll_offset_deg, double hfov_deg,
                              double vfov_deg, int width, int height, double eye_fwd_m = 0,
                              double eye_right_m = 0, double eye_up_m = 0);

// Project a world point through the channel frustum. NDC is OpenGL
// clip: x/y in [−1, 1] is inside the frame. pix origin is top-left.
struct ProjectedPoint {
    double ndc_x = 0;
    double ndc_y = 0;
    double depth_m = 0;
    double pix_x = 0;
    double pix_y = 0;
    bool in_front = false;
    bool in_frame = false;
};
ProjectedPoint project_channel(const ChannelView& v, const Ecef& world, int width, int height);
ProjectedPoint project_lla(const ChannelView& v, double lat_deg, double lon_deg, double alt_m,
                           int width, int height);

// 0 = ocean, 1 = land. Soft edges so coasts are not stair-step texels.
// Public geography only (coarse continents + a few US coastal cutouts).
float land_coverage(double lat_deg, double lon_deg);

} // namespace s376::geo
