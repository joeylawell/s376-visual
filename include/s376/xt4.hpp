#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace s376::xt4 {

// Packet type numbers the host and IG already share on the wire.
constexpr std::uint32_t kMagic = 0x2D3FAB01;
constexpr std::uint16_t kMagicWord0 = 0x2D3F;
constexpr std::uint16_t kMagicWord1 = 0xAB01;
constexpr int kPreambleWords = 10;

constexpr std::uint32_t kFlagVaxHost = 0x0004;
constexpr std::uint32_t kFlagGouldHost = 0x0002;

constexpr int kTypeDcs = 100;
constexpr int kTypeStormSelect = 105;
constexpr int kTypeAcLights = 109;
constexpr int kTypeLightSwitch = 112;
constexpr int kTypeQuery = 113;
constexpr int kTypeEnv = 124;
constexpr int kTypeLowerCloud = 125;
constexpr int kTypeUpperCloud = 126;
constexpr int kTypeTopCloud = 127;
constexpr int kTypeFog = 128;
constexpr int kTypeDisplay = 131;
// s376-visual extension (Oct 25 demo / future IOS emulator). Not a CAE
// or RSI ICD type — designed here so gear/flaps can reach the IG.
constexpr int kTypeOwnship = 180;
constexpr int kTypeHat = 200;

constexpr int kEyepointCsn = 1;
constexpr int kStormCsn0 = 28; // type-100 CSN 28, 29, 30 position active cells
constexpr int kMaxDefinedStorms = 30;
constexpr int kMaxActiveStorms = 3;
constexpr int kMaxLightSwitches = 256;
constexpr int kMaxCloudLayers = 8;
constexpr float kMaxVisibilityFt = 528000.f;

enum class WordOrder { Auto, Le, Be };
enum class FloatFmt { Auto, Vax, Ieee };

const char* word_order_name(WordOrder o);
const char* float_fmt_name(FloatFmt f);
const char* type_name(int type);

WordOrder detect_word_order(const std::uint8_t* data, std::size_t n);

struct Packet {
    int type = 0;
    std::uint32_t flags = 0;
    std::vector<std::uint16_t> words; // preamble included
    int offset_words = 0;

    const std::uint16_t* body() const {
        return words.size() > static_cast<std::size_t>(kPreambleWords)
                   ? words.data() + kPreambleWords
                   : nullptr;
    }
    int body_words() const { return static_cast<int>(words.size()) - kPreambleWords; }
};

struct Datagram {
    WordOrder word_order = WordOrder::Le;
    FloatFmt float_fmt = FloatFmt::Vax;
    std::vector<Packet> packets;
    int junk_words = 0;
};

struct Dcs {
    int csn = 0;
    int minor_csn = 0;
    int select = 0;
    unsigned count = 0;
    float heading_deg = 0; // as sent (negated true heading)
    float pitch_deg = 0;
    float roll_deg = 0;
    double lat_deg = 0;
    double lon_deg = 0;
    double alt_ft = 0;
    double timestamp = 0;

    double true_heading_deg() const;
    bool is_eyepoint() const { return csn == kEyepointCsn && minor_csn == -1; }
};

struct Lights {
    bool beacon = false;
    bool strobe = false;
    std::uint16_t landing_mask = 0;
    std::uint16_t bright_mask = 0;
};

struct Env {
    int tod_select = 0;
    int precip_type = 0;
    int precip_intensity = 0;
    int month = 1;
    int day = 1;
    int year = 2000;
    int runway_condition = 0;
    int lightning_trigger = 0;
    float tod_hours = 12.f;
    float visible_range_ft = 528000.f;
    float wind_speed_mph = 0.f;
    float wind_dir_deg = 0.f;
};

struct Fog {
    int region = -1;
    int select = -1;
    bool patchy = false;
    int speed_cue = 0;
    float height_ft = 0;
    float rvr_ft = 0;
};

// Types 125 / 126 (lower / upper). Type 127 is region+select only.
struct CloudLayer {
    int region = -1;
    int select = -1; // −1 off; ≥0 catalog / type index
    int scud = 0;
    int alt_transition = -1;
    float ceiling_ft = 0; // MSL base
    float top_ft = 0;     // MSL top
    float vis_above_ft = -1;
    float wind_kt = 0;
    float wind_dir_deg = 0;
    float lower_trans_ft = -1;
    float upper_trans_ft = -1;

    bool on() const { return select >= 0; }
};

struct StormSelect {
    int select = 0; // 0…29 of the IG catalog
    bool active = false;
    int csn = kStormCsn0;
    int precip_type = 0;
    int precip_intensity = 0;
    int lightning = 0; // 0 off; 1 random; >1 flash on change
};

struct StormCell {
    StormSelect cfg;
    Dcs pose;
    bool have_pose = false;
};

struct LightSwitch {
    int switch_num = 0;
    int step = 0; // 0 off .. 5 max
};

struct DisplayControl {
    int channel = -1;
    int mode = 1; // 0 off, 1 on, 2 standby
};

struct Query {
    int query_type = 0;
    int csn = 0;
};

// Type 180 — ownship configuration. s376-visual design, not an ICD copy.
// gear: 0 up, 1 down, 2 in transit. flaps: 737-300 detents 0/1/2/5/10/15/25/30/40.
struct OwnshipConfig {
    int gear = 0;
    int flaps = 0;
};

struct Stats {
    std::uint64_t datagrams = 0;
    std::uint64_t packets = 0;
    std::uint64_t eyepoints = 0;
    std::uint64_t bad_magic = 0;
    std::uint64_t implausible = 0;
    std::uint64_t junk_words = 0;
    std::array<std::uint64_t, 16> by_type{};
    std::uint64_t other_types = 0;
};

class Decoder {
  public:
    explicit Decoder(WordOrder order = WordOrder::Auto, FloatFmt fmt = FloatFmt::Auto);

    std::optional<Datagram> parse(const std::uint8_t* data, std::size_t n, const char* origin = "");
    std::optional<Dcs> eyepoint(Datagram& dg);

    FloatFmt locked_fmt() const { return fmt_; }
    const Stats& stats() const { return stats_; }

  private:
    WordOrder forced_order_;
    FloatFmt forced_fmt_;
    FloatFmt fmt_ = FloatFmt::Auto;
    Stats stats_;
};

// Field codecs (word arrays are already in host 16-bit units).
std::int32_t get_i32(const std::uint16_t* w, int k);
float get_f32(const std::uint16_t* w, int k, FloatFmt fmt);
double get_f64(const std::uint16_t* w, int k, FloatFmt fmt);
void put_i32(std::uint16_t* w, std::int32_t v);
void put_f32(std::uint16_t* w, float x, FloatFmt fmt);
void put_f64(std::uint16_t* w, double x, FloatFmt fmt);

std::int16_t s16(std::uint16_t w);

std::vector<std::uint16_t> words_from_bytes(const std::uint8_t* data, std::size_t n,
                                            WordOrder order);
std::vector<std::uint8_t> bytes_from_words(const std::uint16_t* w, std::size_t nwords,
                                           WordOrder order);

std::vector<Packet> split_packets(const std::uint16_t* words, std::size_t nwords, int& junk);

Dcs decode_dcs(const Packet& p, FloatFmt fmt);
Lights decode_lights(const Packet& p);
Env decode_env(const Packet& p, FloatFmt fmt);
Fog decode_fog(const Packet& p, FloatFmt fmt);
CloudLayer decode_cloud(const Packet& p, FloatFmt fmt);
CloudLayer decode_top_cloud(const Packet& p);
StormSelect decode_storm(const Packet& p);
LightSwitch decode_light_switch(const Packet& p);
DisplayControl decode_display(const Packet& p);
Query decode_query(const Packet& p);
OwnshipConfig decode_ownship(const Packet& p);

bool plausible_dcs(const Dcs& d);

// Builders — synthetic host / unit tests. Never used with captured bytes.
std::vector<std::uint16_t> build_packet(int type, const std::uint16_t* body, int nbody,
                                        std::uint32_t flags = kFlagVaxHost);
std::vector<std::uint16_t> build_dcs(double lat, double lon, double alt_ft, double true_hdg,
                                     float pitch, float roll, unsigned count, FloatFmt fmt,
                                     int csn = kEyepointCsn, int minor = -1, int select = 1);
std::vector<std::uint16_t> build_lights(bool beacon, bool strobe, std::uint16_t landing_mask,
                                        FloatFmt fmt);
std::vector<std::uint16_t> build_env(float tod_hours, float visible_range_ft, int month, int day,
                                     int year, FloatFmt fmt, int tod_select = -1, int precip_type = 0,
                                     int precip_intensity = 0, int lightning = 0,
                                     float wind_mph = 0, float wind_dir = 0);
std::vector<std::uint16_t> build_fog(float height_ft, float rvr_ft, FloatFmt fmt, int region = -1,
                                     int select = 0);
std::vector<std::uint16_t> build_cloud(int type, int select, float ceiling_ft, float top_ft,
                                       FloatFmt fmt, int region = -1, int scud = 3,
                                       float vis_above_ft = -1.f, float wind_kt = 0,
                                       float wind_dir = 0);
std::vector<std::uint16_t> build_top_cloud(int select, int region = -1);
std::vector<std::uint16_t> build_storm(int select, bool active, int csn, int precip_type,
                                       int precip_intensity, int lightning);
std::vector<std::uint16_t> build_light_switch(int switch_num, int step);
std::vector<std::uint16_t> build_display(bool on);
std::vector<std::uint16_t> build_ownship(int gear, int flaps);
std::vector<std::uint16_t> build_hat_query();
std::vector<std::uint8_t> build_datagram(const std::vector<std::vector<std::uint16_t>>& packets,
                                         WordOrder order, int total_words = 300);

// IG → host HAT reply the S376 visual module actually reads:
// 64 bytes, big-endian words, IEEE floats at words 22 and 28.
std::vector<std::uint8_t> build_hat_response(float hat_ft, float terrain_elev_ft,
                                             bool valid = true);

// Mirror of the host's VAX-side conversion (tests only).
bool parse_hat_as_host(const std::uint8_t* data, std::size_t n, int& valid, float& hat,
                       float& elev);

} // namespace s376::xt4
