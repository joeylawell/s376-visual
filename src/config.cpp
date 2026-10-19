#include "s376/config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

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

std::string unquote(const std::string& s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

bool parse_bool(const std::string& s, bool& out) {
    if (s == "true" || s == "1") {
        out = true;
        return true;
    }
    if (s == "false" || s == "0") {
        out = false;
        return true;
    }
    return false;
}

struct Toml {
    std::unordered_map<std::string, std::string> kv;
    std::vector<std::unordered_map<std::string, std::string>> tables; // [[channels]]
};

Toml parse_toml(const std::string& text, const std::string& path) {
    Toml t;
    std::string section;
    std::unordered_map<std::string, std::string>* cur = &t.kv;
    std::istringstream in(text);
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;
        if (line.size() >= 4 && line.rfind("[[", 0) == 0 && line.size() >= 4 &&
            line.substr(line.size() - 2) == "]]") {
            const std::string name = trim(line.substr(2, line.size() - 4));
            if (name != "channels")
                throw std::runtime_error(path + ":" + std::to_string(lineno) +
                                         ": unknown table array [[" + name + "]]");
            t.tables.emplace_back();
            cur = &t.tables.back();
            section = "channels";
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            cur = &t.kv;
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error(path + ":" + std::to_string(lineno) +
                                     ": expected key = value");
        const std::string key = trim(line.substr(0, eq));
        const std::string val = unquote(trim(line.substr(eq + 1)));
        const std::string full =
            section.empty() || section == "channels" ? key : section + "." + key;
        (*cur)[section == "channels" ? key : full] = val;
    }
    return t;
}

} // namespace

xt4::WordOrder parse_word_order(const std::string& s) {
    if (s == "auto" || s.empty())
        return xt4::WordOrder::Auto;
    if (s == "le")
        return xt4::WordOrder::Le;
    if (s == "be")
        return xt4::WordOrder::Be;
    throw std::runtime_error("word_order must be auto|le|be, got " + s);
}

xt4::FloatFmt parse_float_fmt(const std::string& s) {
    if (s == "auto" || s.empty())
        return xt4::FloatFmt::Auto;
    if (s == "vax")
        return xt4::FloatFmt::Vax;
    if (s == "ieee")
        return xt4::FloatFmt::Ieee;
    throw std::runtime_error("float_format must be auto|vax|ieee, got " + s);
}

void apply_quality_preset(RenderConfig& r, const std::string& name) {
    r.quality = name;
    if (name == "rtx") {
        r.msaa = std::max(r.msaa, 4);
        r.aniso = std::max(r.aniso, 16);
        r.hdr = true;
        r.atmosphere = true;
        r.water = true;
        r.markings = true;
        r.detail_textures = true;
        r.buildings = true;
        r.ssao = true;
        r.shadows = true;
        r.bloom = true;
        r.fxaa = true;
        r.taa = true;
        r.clustered = true;
        r.lod_fade = true;
        r.filmic = true;
        r.exposure = 1.05f;
        r.ram_mb = std::max(r.ram_mb, 3072);
        r.vram_mb = std::max(r.vram_mb, 10240);
        r.mesh_n = std::max(r.mesh_n, 128);
        r.auto_drop = false;
        r.gpu_budget_ms = 14.00f;
        return;
    }
    // gtx980ti (default): locked 60 Hz × 1 on 6 GB Maxwell (one projector PC).
    // TAA replaces MSAA (GTA V). Cheap 2-cascade CSM stays on until autodrop.
    r.msaa = 0;
    r.aniso = 16;
    r.hdr = true;
    r.atmosphere = true;
    r.water = true;
    r.markings = true;
    r.detail_textures = true;
    r.buildings = true;
    r.ssao = true;
    r.shadows = true;
    r.bloom = true;
    r.fxaa = true;
    r.taa = true;
    r.clustered = true;
    r.lod_fade = true;
    r.filmic = true;
    r.exposure = 1.0f;
    r.ram_mb = 2048;
    r.vram_mb = 5120;
    r.mesh_n = 96;
    r.auto_drop = true;
    r.gpu_budget_ms = 12.20f;
}

namespace {
const QualityGpuBudget kQualityBudgets[] = {
    {"atmosphere", "render.atmosphere", 0.45f}, // Rayleigh/Mie sky + aerial
    {"water", "render.water", 0.80f},           // Fresnel / glint / waves / foam
    {"detail", "render.detail_textures", 0.45f},
    {"buildings", "render.buildings", 1.00f}, // facades + night windows
    {"runway_pbr", "render.markings", 0.35f}, // asphalt/concrete + wet spec
    {"bloom", "render.bloom", 0.55f},         // light halo + city glow
    {"filmic", "render.filmic", 0.15f},
    {"taa", "render.taa", 0.40f},             // temporal AA (live path)
    {"fxaa", "render.fxaa", 0.20f},           // fallback if TAA drops
    {"clustered", "render.clustered", 0.60f}, // 32 night lights on the ground
    {"shadows", "render.shadows", 0.85f},     // 2-cascade 512 CSM
    {"ssao", "render.ssao", 0.25f},
    {"lod_fade", "render.lod_fade", 0.15f} // page-in fade, no pop
};
} // namespace

const QualityGpuBudget* quality_gpu_budgets(int& n) {
    n = static_cast<int>(sizeof(kQualityBudgets) / sizeof(kQualityBudgets[0]));
    return kQualityBudgets;
}

float quality_gpu_budget_total_ms(bool shadows_on) {
    int n = 0;
    const QualityGpuBudget* b = quality_gpu_budgets(n);
    float s = 6.0f; // base tiles + mesh + lights
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(b[i].name, "shadows") == 0 && !shadows_on)
            continue;
        s += b[i].gpu_ms;
    }
    return s;
}

namespace {
const QualityGpuBudget kAutodropOrder[] = {
    {"ssao", "render.ssao", 0.25f},
    {"shadows", "render.shadows", 0.85f},
    {"bloom", "render.bloom", 0.55f},
    {"fxaa", "render.fxaa", 0.20f},
    {"taa", "render.taa", 0.40f},
    {"lod_fade", "render.lod_fade", 0.15f},
    {"detail", "render.detail_textures", 0.45f},
    {"filmic", "render.filmic", 0.15f},
    {"clustered", "render.clustered", 0.60f},
};

bool* autodrop_flag(RenderConfig& r, const char* name) {
    if (std::strcmp(name, "ssao") == 0)
        return &r.ssao;
    if (std::strcmp(name, "bloom") == 0)
        return &r.bloom;
    if (std::strcmp(name, "fxaa") == 0)
        return &r.fxaa;
    if (std::strcmp(name, "taa") == 0)
        return &r.taa;
    if (std::strcmp(name, "clustered") == 0)
        return &r.clustered;
    if (std::strcmp(name, "lod_fade") == 0)
        return &r.lod_fade;
    if (std::strcmp(name, "detail") == 0)
        return &r.detail_textures;
    if (std::strcmp(name, "filmic") == 0)
        return &r.filmic;
    if (std::strcmp(name, "shadows") == 0)
        return &r.shadows;
    return nullptr;
}
} // namespace

const QualityGpuBudget* quality_autodrop_order(int& n) {
    n = static_cast<int>(sizeof(kAutodropOrder) / sizeof(kAutodropOrder[0]));
    return kAutodropOrder;
}

bool apply_quality_autodrop(RenderConfig& r, float gpu_ms, float budget_ms, std::string* dropped) {
    if (!r.auto_drop || gpu_ms < 0.f || gpu_ms <= budget_ms)
        return false;
    int n = 0;
    const QualityGpuBudget* order = quality_autodrop_order(n);
    for (int i = 0; i < n; ++i) {
        bool* flag = autodrop_flag(r, order[i].name);
        if (!flag || !*flag)
            continue;
        *flag = false;
        if (dropped) {
            if (!dropped->empty())
                *dropped += ",";
            *dropped += order[i].name;
        }
        return true;
    }
    return false;
}

AppConfig default_config() {
    AppConfig c;
    // Four projector PCs, Raster's 3 collimated channels:
    // captain sees far_left+mid_left; FO sees mid_right+far_right.
    // Both middles are the same front frustum (duplicate).
    ChannelConfig fl, ml, mr, fr;
    fl.name = "far_left"; // Raster left
    fl.heading_offset_deg = -50.0;
    fl.pitch_offset_deg = 0;
    fl.roll_offset_deg = 0;
    fl.raster_ch = 1;
    fl.monitor = 0;
    fl.flip_vertical = true; // projector framebuffer mirror only
    ml.name = "mid_left"; // Raster front (captain)
    ml.heading_offset_deg = 0;
    ml.pitch_offset_deg = 0;
    ml.roll_offset_deg = 0;
    ml.raster_ch = 0;
    ml.monitor = 1;
    mr.name = "mid_right"; // Raster front (FO duplicate)
    mr.heading_offset_deg = 0;
    mr.pitch_offset_deg = 0;
    mr.roll_offset_deg = 0;
    mr.raster_ch = 0;
    mr.monitor = 2;
    fr.name = "far_right"; // Raster right
    fr.heading_offset_deg = 50.0;
    fr.pitch_offset_deg = 0;
    fr.roll_offset_deg = 0;
    fr.raster_ch = 2;
    fr.monitor = 3;
    c.channels = {fl, ml, mr, fr};
    return c;
}

static void fill_host(HostConfig& h, const std::unordered_map<std::string, std::string>& kv) {
    if (kv.count("host.bind"))
        h.bind = kv.at("host.bind");
    if (kv.count("host.reply_port"))
        h.reply_port = static_cast<std::uint16_t>(std::stoi(kv.at("host.reply_port")));
    if (kv.count("host.word_order"))
        h.word_order = parse_word_order(kv.at("host.word_order"));
    if (kv.count("host.float_format"))
        h.float_fmt = parse_float_fmt(kv.at("host.float_format"));
    if (kv.count("host.hat_responder"))
        parse_bool(kv.at("host.hat_responder"), h.hat_responder);
    if (kv.count("host.reply_master"))
        h.reply_master = kv.at("host.reply_master");
    if (kv.count("host.allow_sim_lan"))
        parse_bool(kv.at("host.allow_sim_lan"), h.allow_sim_lan);
    if (kv.count("host.allow_lan"))
        parse_bool(kv.at("host.allow_lan"), h.allow_lan);
    if (kv.count("host.reply_to"))
        h.reply_to = kv.at("host.reply_to");
}

static void fill_render(RenderConfig& r, const std::unordered_map<std::string, std::string>& kv) {
    if (kv.count("render.engine"))
        r.engine = kv.at("render.engine");
    if (kv.count("render.earth_file"))
        r.earth_file = kv.at("render.earth_file");
    if (kv.count("render.cache_dir"))
        r.cache_dir = kv.at("render.cache_dir");
    if (kv.count("render.frame_rate"))
        r.frame_rate = std::stod(kv.at("render.frame_rate"));
    if (kv.count("render.window_title"))
        r.window_title = kv.at("render.window_title");
    if (kv.count("render.vsync"))
        parse_bool(kv.at("render.vsync"), r.vsync);
    if (kv.count("render.quality")) {
        r.quality = kv.at("render.quality");
        if (r.quality != "custom")
            apply_quality_preset(r, r.quality);
    }
    if (kv.count("render.msaa"))
        r.msaa = std::stoi(kv.at("render.msaa"));
    if (kv.count("render.aniso"))
        r.aniso = std::stoi(kv.at("render.aniso"));
    if (kv.count("render.gl_debug"))
        parse_bool(kv.at("render.gl_debug"), r.gl_debug);
    if (kv.count("render.hdr"))
        parse_bool(kv.at("render.hdr"), r.hdr);
    if (kv.count("render.atmosphere"))
        parse_bool(kv.at("render.atmosphere"), r.atmosphere);
    if (kv.count("render.water"))
        parse_bool(kv.at("render.water"), r.water);
    if (kv.count("render.markings"))
        parse_bool(kv.at("render.markings"), r.markings);
    if (kv.count("render.detail_textures"))
        parse_bool(kv.at("render.detail_textures"), r.detail_textures);
    if (kv.count("render.buildings"))
        parse_bool(kv.at("render.buildings"), r.buildings);
    if (kv.count("render.ssao"))
        parse_bool(kv.at("render.ssao"), r.ssao);
    if (kv.count("render.shadows"))
        parse_bool(kv.at("render.shadows"), r.shadows);
    if (kv.count("render.bloom"))
        parse_bool(kv.at("render.bloom"), r.bloom);
    if (kv.count("render.fxaa"))
        parse_bool(kv.at("render.fxaa"), r.fxaa);
    if (kv.count("render.taa"))
        parse_bool(kv.at("render.taa"), r.taa);
    if (kv.count("render.clustered"))
        parse_bool(kv.at("render.clustered"), r.clustered);
    if (kv.count("render.lod_fade"))
        parse_bool(kv.at("render.lod_fade"), r.lod_fade);
    if (kv.count("render.filmic"))
        parse_bool(kv.at("render.filmic"), r.filmic);
    if (kv.count("render.solar_astronomy"))
        parse_bool(kv.at("render.solar_astronomy"), r.solar_astronomy);
    if (kv.count("render.exposure"))
        r.exposure = static_cast<float>(std::stod(kv.at("render.exposure")));
    if (kv.count("render.sync_to_host"))
        parse_bool(kv.at("render.sync_to_host"), r.sync_to_host);
    if (kv.count("render.ram_mb"))
        r.ram_mb = std::stoi(kv.at("render.ram_mb"));
    if (kv.count("render.vram_mb"))
        r.vram_mb = std::stoi(kv.at("render.vram_mb"));
    if (kv.count("render.mesh_n"))
        r.mesh_n = std::stoi(kv.at("render.mesh_n"));
    if (kv.count("render.auto_drop"))
        parse_bool(kv.at("render.auto_drop"), r.auto_drop);
    if (kv.count("render.gpu_budget_ms"))
        r.gpu_budget_ms = static_cast<float>(std::stod(kv.at("render.gpu_budget_ms")));
    if (kv.count("render.offscreen"))
        parse_bool(kv.at("render.offscreen"), r.offscreen);
}

static ChannelConfig fill_channel(const std::unordered_map<std::string, std::string>& kv) {
    ChannelConfig c;
    if (kv.count("name"))
        c.name = kv.at("name");
    auto num = [&](const char* k, double& d) {
        if (kv.count(k))
            d = std::stod(kv.at(k));
    };
    auto inum = [&](const char* k, int& d) {
        if (kv.count(k))
            d = std::stoi(kv.at(k));
    };
    num("heading_offset_deg", c.heading_offset_deg);
    num("pitch_offset_deg", c.pitch_offset_deg);
    num("roll_offset_deg", c.roll_offset_deg);
    num("hfov_deg", c.hfov_deg);
    num("vfov_deg", c.vfov_deg);
    num("eye_fwd_m", c.eye_fwd_m);
    num("eye_right_m", c.eye_right_m);
    num("eye_up_m", c.eye_up_m);
    inum("monitor", c.monitor);
    inum("width", c.width);
    inum("height", c.height);
    if (kv.count("resolution")) {
        int rw = c.width, rh = c.height;
        if (parse_resolution(kv.at("resolution"), rw, rh)) {
            c.width = rw;
            c.height = rh;
        }
    }
    inum("window_x", c.window_x);
    inum("window_y", c.window_y);
    if (kv.count("fullscreen"))
        parse_bool(kv.at("fullscreen"), c.fullscreen);
    if (kv.count("enabled"))
        parse_bool(kv.at("enabled"), c.enabled);
    inum("raster_ch", c.raster_ch);
    if (kv.count("flip_vertical"))
        parse_bool(kv.at("flip_vertical"), c.flip_vertical);
    if (kv.count("role"))
        c.role = kv.at("role");
    if (kv.count("correction_file"))
        c.correction_file = kv.at("correction_file");
    if (kv.count("pattern"))
        c.pattern = kv.at("pattern");
    return c;
}

AppConfig load_config(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("config file not found: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto t = parse_toml(ss.str(), path);
    AppConfig cfg = default_config();
    fill_host(cfg.host, t.kv);
    fill_render(cfg.render, t.kv);
    if (t.kv.count("log.verbose"))
        parse_bool(t.kv.at("log.verbose"), cfg.log.verbose);
    if (t.kv.count("log.debug"))
        parse_bool(t.kv.at("log.debug"), cfg.log.debug);
    if (t.kv.count("log.dir"))
        cfg.log.dir = t.kv.at("log.dir");
    if (t.kv.count("control.bind"))
        cfg.control.bind = t.kv.at("control.bind");
    if (t.kv.count("control.enabled"))
        parse_bool(t.kv.at("control.enabled"), cfg.control.enabled);
    if (t.kv.count("egg.ptt_source"))
        cfg.egg.ptt_source = t.kv.at("egg.ptt_source");
    if (t.kv.count("egg.ptt_switch"))
        cfg.egg.ptt_switch = std::stoi(t.kv.at("egg.ptt_switch"));
    if (t.kv.count("egg.ptt_landing_bit"))
        cfg.egg.ptt_landing_bit = std::stoi(t.kv.at("egg.ptt_landing_bit"));
    if (t.kv.count("egg.ptt_key"))
        cfg.egg.ptt_key = t.kv.at("egg.ptt_key");
    if (!t.tables.empty()) {
        cfg.channels.clear();
        for (const auto& tab : t.tables)
            cfg.channels.push_back(fill_channel(tab));
        // Old 3-channel files may omit reply_master; don't inherit mid_left
        // from default_config() if that name is not in this file.
        if (!cfg.host.reply_master.empty()) {
            bool known = false;
            for (const auto& ch : cfg.channels) {
                if (ch.name == cfg.host.reply_master)
                    known = true;
            }
            if (!known)
                cfg.host.reply_master.clear();
        }
    }
    return cfg;
}

std::string dump_config(const AppConfig& cfg) {
    return dump_config(cfg, {});
}

std::string dump_config(const AppConfig& cfg, const std::vector<std::string>& selected) {
    std::ostringstream o;
    o << "host.bind=" << cfg.host.bind << "\n";
    o << "host.reply_port=" << cfg.host.reply_port << "\n";
    o << "host.word_order=" << xt4::word_order_name(cfg.host.word_order) << "\n";
    o << "host.float_format=" << xt4::float_fmt_name(cfg.host.float_fmt) << "\n";
    o << "host.hat_responder=" << (cfg.host.hat_responder ? "true" : "false") << "\n";
    o << "host.reply_master=" << cfg.host.reply_master << "\n";
    o << "host.allow_sim_lan=" << (cfg.host.allow_sim_lan ? "true" : "false") << "\n";
    o << "host.reply_to=" << cfg.host.reply_to << "\n";
    o << "control.bind=" << cfg.control.bind << "\n";
    o << "control.enabled=" << (cfg.control.enabled ? "true" : "false") << "\n";
    o << "egg.ptt_source=" << cfg.egg.ptt_source << "\n";
    o << "egg.ptt_switch=" << cfg.egg.ptt_switch << "\n";
    o << "egg.ptt_landing_bit=" << cfg.egg.ptt_landing_bit << "\n";
    o << "egg.ptt_key=" << cfg.egg.ptt_key << "\n";
    o << "render.engine=" << cfg.render.engine << "\n";
    o << "render.earth_file=" << cfg.render.earth_file << "\n";
    o << "render.cache_dir=" << cfg.render.cache_dir << "\n";
    o << "render.frame_rate=" << cfg.render.frame_rate << "\n";
    o << "render.msaa=" << cfg.render.msaa << "\n";
    o << "render.aniso=" << cfg.render.aniso << "\n";
    o << "render.gl_debug=" << (cfg.render.gl_debug ? "true" : "false") << "\n";
    o << "render.quality=" << cfg.render.quality << "\n";
    o << "render.hdr=" << (cfg.render.hdr ? "true" : "false") << "\n";
    o << "render.atmosphere=" << (cfg.render.atmosphere ? "true" : "false") << "\n";
    o << "render.water=" << (cfg.render.water ? "true" : "false") << "\n";
    o << "render.markings=" << (cfg.render.markings ? "true" : "false") << "\n";
    o << "render.detail_textures=" << (cfg.render.detail_textures ? "true" : "false") << "\n";
    o << "render.buildings=" << (cfg.render.buildings ? "true" : "false") << "\n";
    o << "render.ssao=" << (cfg.render.ssao ? "true" : "false") << "\n";
    o << "render.shadows=" << (cfg.render.shadows ? "true" : "false") << "\n";
    o << "render.bloom=" << (cfg.render.bloom ? "true" : "false") << "\n";
    o << "render.fxaa=" << (cfg.render.fxaa ? "true" : "false") << "\n";
    o << "render.taa=" << (cfg.render.taa ? "true" : "false") << "\n";
    o << "render.clustered=" << (cfg.render.clustered ? "true" : "false") << "\n";
    o << "render.lod_fade=" << (cfg.render.lod_fade ? "true" : "false") << "\n";
    o << "render.filmic=" << (cfg.render.filmic ? "true" : "false") << "\n";
    o << "render.solar_astronomy=" << (cfg.render.solar_astronomy ? "true" : "false") << "\n";
    o << "render.sync_to_host=" << (cfg.render.sync_to_host ? "true" : "false") << "\n";
    o << "render.ram_mb=" << cfg.render.ram_mb << "\n";
    o << "render.vram_mb=" << cfg.render.vram_mb << "\n";
    o << "render.mesh_n=" << cfg.render.mesh_n << "\n";
    o << "render.auto_drop=" << (cfg.render.auto_drop ? "true" : "false") << "\n";
    o << "render.gpu_budget_ms=" << cfg.render.gpu_budget_ms << "\n";
    o << "render.offscreen=" << (cfg.render.offscreen ? "true" : "false") << "\n";
    if (!selected.empty()) {
        o << "selected=";
        for (std::size_t i = 0; i < selected.size(); ++i) {
            if (i)
                o << ",";
            o << selected[i];
        }
        o << "\n";
    }
    for (const auto& ch : cfg.channels) {
        if (!selected.empty()) {
            bool want = false;
            for (const auto& n : selected) {
                if (n == ch.name)
                    want = true;
            }
            if (!want)
                continue;
        }
        o << "channel " << ch.name << " hdg=" << ch.heading_offset_deg << " fov=" << ch.hfov_deg
          << "x" << ch.vfov_deg << " " << ch.width << "x" << ch.height;
        if (ch.raster_ch >= 0)
            o << " raster_ch=" << ch.raster_ch;
        if (ch.flip_vertical)
            o << " flip";
        if (ch.role != "channel")
            o << " role=" << ch.role;
        if (ch.eye_right_m != 0.0 || ch.eye_fwd_m != 0.0 || ch.eye_up_m != 0.0)
            o << " eye=" << ch.eye_fwd_m << "," << ch.eye_right_m << "," << ch.eye_up_m;
        o << (ch.enabled ? "" : " disabled") << "\n";
    }
    return o.str();
}

static std::string resolve_channel_alias(const std::string& name) {
    if (name == "left")
        return "far_left";
    if (name == "front")
        return "mid_left";
    if (name == "right")
        return "far_right";
    if (name == "front_fo" || name == "fo_front")
        return "mid_right";
    return name;
}

bool parse_resolution(const std::string& s, int& w, int& h) {
    if (s.empty())
        return false;
    std::size_t sep = s.find_first_of("xX*");
    std::size_t seplen = 1;
    if (sep == std::string::npos) {
        sep = s.find("\xC3\x97"); // UTF-8 ×
        seplen = 2;
    }
    if (sep == std::string::npos || sep == 0 || sep + seplen >= s.size())
        return false;
    try {
        const int a = std::stoi(s.substr(0, sep));
        const int b = std::stoi(s.substr(sep + seplen));
        if (a < 8 || b < 8 || a > 16384 || b > 16384)
            return false;
        w = a;
        h = b;
        return true;
    } catch (...) {
        return false;
    }
}

std::string format_resolution(int w, int h) {
    return std::to_string(std::max(8, w)) + "x" + std::to_string(std::max(8, h));
}

int channel_tile_order(const ChannelConfig& ch) {
    if (ch.name == "far_left" || ch.name == "left")
        return 0;
    if (ch.name == "mid_left" || ch.name == "front")
        return 1;
    if (ch.name == "mid_right" || ch.name == "front_fo" || ch.name == "fo_front")
        return 2;
    if (ch.name == "far_right" || ch.name == "right")
        return 3;
    if (ch.raster_ch == 1)
        return 0;
    if (ch.raster_ch == 0)
        return 1;
    if (ch.raster_ch == 2)
        return 3;
    return 8;
}

std::vector<ChannelConfig> wrap_channels(const std::vector<ChannelConfig>& channels) {
    // Unique frusta, left-to-right by heading. Channel count is whatever
    // the TOML enabled — never a hardcoded 3. Duplicate headings (FO
    // front = captain front) collapse so a wrap strip is one view each.
    std::vector<ChannelConfig> enabled;
    for (const auto& ch : channels) {
        if (!ch.enabled)
            continue;
        if (ch.role == "host_io" || ch.name == "ios")
            continue;
        enabled.push_back(ch);
    }
    std::sort(enabled.begin(), enabled.end(), [](const ChannelConfig& a, const ChannelConfig& b) {
        if (std::fabs(a.heading_offset_deg - b.heading_offset_deg) > 0.05)
            return a.heading_offset_deg < b.heading_offset_deg;
        return channel_tile_order(a) < channel_tile_order(b);
    });
    std::vector<ChannelConfig> out;
    for (const auto& ch : enabled) {
        if (!out.empty() && std::fabs(out.back().heading_offset_deg - ch.heading_offset_deg) < 0.05)
            continue;
        out.push_back(ch);
    }
    return out;
}

void wrap_strip_size(const std::vector<ChannelConfig>& channels, int& w, int& h) {
    w = 0;
    h = 0;
    for (const auto& ch : channels) {
        w += std::max(8, ch.width);
        h = std::max(h, std::max(8, ch.height));
    }
}

std::vector<ChannelConfig> select_channels(const AppConfig& cfg, const std::string& name) {
    std::vector<ChannelConfig> out;
    if (name.empty() || name == "all") {
        for (const auto& ch : cfg.channels) {
            if (ch.enabled)
                out.push_back(ch);
        }
        if (out.empty() && !cfg.channels.empty())
            out.push_back(cfg.channels.front());
        return out;
    }
    const std::string want = resolve_channel_alias(name);
    for (const auto& ch : cfg.channels) {
        if (ch.name == want || ch.name == name)
            out.push_back(ch);
    }
    return out;
}

std::vector<WindowPlacement> layout_channels(const std::vector<ChannelConfig>& channels,
                                             int num_screens, int screen_w, int screen_h) {
    std::vector<WindowPlacement> out;
    if (channels.empty())
        return out;
    const int ns = std::max(1, num_screens);
    const int sw = std::max(320, screen_w);
    const int sh = std::max(240, screen_h);

    bool any_explicit = false;
    for (const auto& ch : channels) {
        if (ch.window_x >= 0 && ch.window_y >= 0)
            any_explicit = true;
    }

    const bool tile = !any_explicit && ns <= 1 && channels.size() > 1;
    std::vector<ChannelConfig> ordered = channels;
    if (tile) {
        std::stable_sort(ordered.begin(), ordered.end(), [](const ChannelConfig& a, const ChannelConfig& b) {
            return channel_tile_order(a) < channel_tile_order(b);
        });
    }

    const int n = static_cast<int>(ordered.size());
    int cell_w = sw / n;
    int cell_h = sh;
    if (tile) {
        // Keep the configured aspect, shrink to fit the tile.
        const double aspect =
            static_cast<double>(ordered[0].width) / static_cast<double>(std::max(1, ordered[0].height));
        cell_w = sw / n;
        cell_h = static_cast<int>(cell_w / std::max(aspect, 0.1));
        if (cell_h > sh) {
            cell_h = sh;
            cell_w = static_cast<int>(cell_h * aspect);
        }
    }

    for (int i = 0; i < n; ++i) {
        const auto& ch = ordered[static_cast<std::size_t>(i)];
        WindowPlacement p;
        p.name = ch.name;
        p.fullscreen = ch.fullscreen && !tile;
        if (ch.window_x >= 0 && ch.window_y >= 0) {
            p.screen = std::max(0, std::min(ch.monitor, ns - 1));
            p.x = ch.window_x;
            p.y = ch.window_y;
            p.w = ch.width;
            p.h = ch.height;
        } else if (tile) {
            p.screen = 0;
            p.x = i * (sw / n);
            p.y = 0;
            p.w = cell_w;
            p.h = cell_h;
            p.fullscreen = false;
        } else {
            p.screen = std::max(0, std::min(ch.monitor, ns - 1));
            p.x = 0;
            p.y = 0;
            p.w = ch.width;
            p.h = ch.height;
        }
        out.push_back(p);
    }
    return out;
}

bool hat_reply_enabled(const HostConfig& h, const std::vector<ChannelConfig>& selected) {
    if (!h.hat_responder)
        return false;
    if (h.reply_master.empty())
        return true;
    for (const auto& ch : selected) {
        if (ch.name == h.reply_master)
            return true;
    }
    return false;
}

std::string default_shot_channel(const AppConfig& cfg) {
    const char* prefer[] = {"mid_left", "mid_right", "front", nullptr};
    for (int i = 0; prefer[i]; ++i) {
        for (const auto& ch : cfg.channels) {
            if (ch.enabled && ch.name == prefer[i])
                return ch.name;
        }
    }
    for (const auto& ch : cfg.channels) {
        if (ch.enabled)
            return ch.name;
    }
    return {};
}

bool patch_channel_display(const std::string& config_path, const ChannelConfig& ch,
                           std::string& err) {
    auto trim_l = [](const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
            ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
            --b;
        return s.substr(a, b - a);
    };
    auto unquote_l = [](const std::string& s) {
        if (s.size() >= 2 &&
            ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
            return s.substr(1, s.size() - 2);
        return s;
    };
    std::ifstream in(config_path);
    if (!in) {
        err = "config not found: " + config_path;
        return false;
    }
    std::ostringstream raw;
    raw << in.rdbuf();
    const std::string text = raw.str();
    std::istringstream src(text);
    std::ostringstream out;
    std::string line;
    bool in_block = false;
    bool matched = false;
    bool wrote_hdg = false, wrote_hfov = false, wrote_vfov = false, wrote_flip = false,
         wrote_pat = false, wrote_res = false, wrote_w = false, wrote_h = false;
    auto flush_missing = [&]() {
        if (!in_block)
            return;
        if (!wrote_hdg)
            out << "heading_offset_deg = " << ch.heading_offset_deg << "\n";
        if (!wrote_hfov)
            out << "hfov_deg = " << ch.hfov_deg << "\n";
        if (!wrote_vfov)
            out << "vfov_deg = " << ch.vfov_deg << "\n";
        if (!wrote_flip)
            out << "flip_vertical = " << (ch.flip_vertical ? "true" : "false") << "\n";
        if (!wrote_res && !wrote_w && !wrote_h)
            out << "resolution = \"" << format_resolution(ch.width, ch.height) << "\"\n";
        if (!wrote_pat && !ch.pattern.empty())
            out << "pattern = \"" << ch.pattern << "\"\n";
    };
    while (std::getline(src, line)) {
        const std::string trimmed = trim_l(line);
        if (trimmed.rfind("[[", 0) == 0 && trimmed.size() >= 4 &&
            trimmed.substr(trimmed.size() - 2) == "]]") {
            flush_missing();
            in_block = false;
            wrote_hdg = wrote_hfov = wrote_vfov = wrote_flip = wrote_pat = wrote_res =
                wrote_w = wrote_h = false;
            out << line << "\n";
            continue;
        }
        if (in_block) {
            auto eq = trimmed.find('=');
            if (eq != std::string::npos) {
                const std::string key = trim_l(trimmed.substr(0, eq));
                auto replace = [&](const std::string& v) {
                    out << key << " = " << v << "\n";
                };
                if (key == "heading_offset_deg") {
                    replace(std::to_string(ch.heading_offset_deg));
                    wrote_hdg = true;
                    continue;
                }
                if (key == "hfov_deg") {
                    replace(std::to_string(ch.hfov_deg));
                    wrote_hfov = true;
                    continue;
                }
                if (key == "vfov_deg") {
                    replace(std::to_string(ch.vfov_deg));
                    wrote_vfov = true;
                    continue;
                }
                if (key == "flip_vertical") {
                    replace(ch.flip_vertical ? "true" : "false");
                    wrote_flip = true;
                    continue;
                }
                if (key == "resolution") {
                    replace(std::string("\"") + format_resolution(ch.width, ch.height) + "\"");
                    wrote_res = true;
                    continue;
                }
                if (key == "width") {
                    replace(std::to_string(ch.width));
                    wrote_w = true;
                    continue;
                }
                if (key == "height") {
                    replace(std::to_string(ch.height));
                    wrote_h = true;
                    continue;
                }
                if (key == "pattern") {
                    replace(std::string("\"") + (ch.pattern.empty() ? "none" : ch.pattern) + "\"");
                    wrote_pat = true;
                    continue;
                }
            }
        } else if (trimmed.rfind("name", 0) == 0 && trimmed.find('=') != std::string::npos) {
            const auto eq = trimmed.find('=');
            const std::string val = unquote_l(trim_l(trimmed.substr(eq + 1)));
            if (val == ch.name) {
                in_block = true;
                matched = true;
            }
        }
        out << line << "\n";
    }
    flush_missing();
    if (!matched) {
        err = "channel " + ch.name + " not in " + config_path;
        return false;
    }
    std::ofstream dest(config_path);
    if (!dest) {
        err = "cannot write " + config_path;
        return false;
    }
    dest << out.str();
    return true;
}

} // namespace s376
