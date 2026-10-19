#include "s376/build_info.hpp"
#include "s376/control.hpp"
#include "s376/landmarks.hpp"
#include "s376/log.hpp"
#include "s376/version.hpp"
#include "s376/wizard.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef S376_HAS_WIZARD_UI
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

namespace {

void usage() {
    std::cerr << "s376-wizard " << s376::version()
              << " — operator console (not Raster Wizard, not xT4)\n"
                 "usage: s376-wizard [options]\n"
                 "  -c, --config PATH   wizard TOML (default config/wizard.toml)\n"
                 "  --headless          no GUI; print status / act and exit\n"
                 "  --status            print channel status (implies --headless)\n"
                 "  --refresh           close and reopen channel IGs (clears Classic)\n"
                 "  --launch            start configured IG processes\n"
                 "  --duration SEC      headless / GUI exit after SEC\n"
                 "  -h, --help\n";
}

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int run_headless(s376::WizardConfig cfg, bool do_status, bool do_refresh, bool do_launch,
                 double duration) {
    s376::ChannelSupervisor sup;
    sup.configure(cfg);
    if (do_launch)
        sup.launch("");
    if (do_refresh) {
        if (!sup.refresh_all()) {
            std::cerr << "refresh_all failed\n";
            return 1;
        }
        std::cout << "refreshed " << cfg.targets.size() << " target(s)\n";
    }
    const auto t0 = std::chrono::steady_clock::now();
    do {
        if (do_status || duration >= 0) {
            for (const auto& t : cfg.targets) {
                try {
                    const auto st = s376::poll_target(t, cfg);
                    std::cout << t.name << "  " << t.control << "  ver=" << st.version
                              << "  git=" << st.git << "  era=" << st.era
                              << (st.era_unlocked ? " unlocked" : "") << "  err=" << st.errors
                              << "\n";
                    for (const auto& ch : st.channels) {
                        std::cout << "  " << ch.cfg.name << "  fps=" << ch.fps
                                  << "  gpu=" << ch.gpu_temp_c << "C  vram=" << ch.vram_mb
                                  << "MB  ram=" << ch.ram_mb << "MB  host_age=" << ch.host_age_ms
                                  << "ms  hdg=" << ch.cfg.heading_offset_deg
                                  << "  fov=" << ch.cfg.hfov_deg << "x" << ch.cfg.vfov_deg
                                  << "  " << s376::format_resolution(ch.cfg.width, ch.cfg.height)
                                  << (ch.cfg.flip_vertical ? " flip" : "") << "  pattern="
                                  << (ch.cfg.pattern.empty() ? "none" : ch.cfg.pattern) << "\n";
                    }
                } catch (const std::exception& ex) {
                    std::cout << t.name << "  DOWN  " << ex.what() << "\n";
                }
            }
        }
        if (duration < 0)
            break;
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (elapsed >= duration)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while (true);
    return 0;
}

#ifdef S376_HAS_WIZARD_UI

struct UiState {
    s376::WizardConfig cfg;
    s376::ChannelSupervisor sup;
    s376::EraUnlock unlock;
    std::string toast;
    std::string logs;
    std::string status_err;
    s376::ControlStatus last;
    int sel = 0;
    char warp_buf[8][7][32]{};
    char res_buf[32]{};
    bool warp_init = false;
    float poll_left = 0;
};

void apply_display(UiState& ui, const s376::ChannelLive& edit) {
    if (ui.cfg.targets.empty())
        return;
    s376::ControlMsg req;
    req.op = "set";
    req.set("channel", edit.cfg.name);
    req.set("hdg", edit.cfg.heading_offset_deg);
    req.set("hfov", edit.cfg.hfov_deg);
    req.set("vfov", edit.cfg.vfov_deg);
    req.set("flip", edit.cfg.flip_vertical ? 1 : 0);
    req.set("resolution", s376::format_resolution(edit.cfg.width, edit.cfg.height));
    req.set("width", edit.cfg.width);
    req.set("height", edit.cfg.height);
    req.set("pattern", s376::display_pattern_name(edit.corr.pattern));
    req.set("gamma", edit.corr.gamma);
    req.set("blend_ramp", edit.corr.blend.ramp);
    req.set("blend_l", edit.corr.blend.left_px);
    req.set("blend_r", edit.corr.blend.right_px);
    req.set("blend_t", edit.corr.blend.top_px);
    req.set("blend_b", edit.corr.blend.bottom_px);
    for (int r = 0; r < s376::kWarpRows; ++r) {
        for (int c = 0; c < s376::kWarpCols; ++c) {
            const auto& n = edit.corr.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
            char key[32], val[64];
            std::snprintf(key, sizeof(key), "warp.%d.%d", r, c);
            std::snprintf(val, sizeof(val), "%.3f, %.3f", n.src_h_deg, n.src_v_deg);
            req.set(key, std::string(val));
        }
    }
    for (const auto& t : ui.cfg.targets) {
        try {
            (void)s376::control_request(t.control, req, 400, ui.cfg.allow_sim_lan, ui.cfg.allow_lan);
        } catch (const std::exception& ex) {
            ui.status_err = ex.what();
        }
    }
}

void save_display(UiState& ui) {
    s376::ControlMsg req;
    req.op = "save";
    req.set("config", ui.cfg.ig_config);
    for (const auto& t : ui.cfg.targets) {
        try {
            auto r = s376::control_request(t.control, req, 600, ui.cfg.allow_sim_lan,
                                           ui.cfg.allow_lan);
            if (!r.ok())
                ui.status_err = r.get("err", "save failed");
        } catch (const std::exception& ex) {
            ui.status_err = ex.what();
        }
    }
}

void poll_ui(UiState& ui) {
    ui.last = s376::ControlStatus{};
    ui.status_err.clear();
    for (const auto& t : ui.cfg.targets) {
        try {
            auto st = s376::poll_target(t, ui.cfg);
            if (ui.last.version.empty())
                ui.last = st;
            else {
                for (auto& ch : st.channels)
                    ui.last.channels.push_back(std::move(ch));
            }
        } catch (const std::exception& ex) {
            ui.status_err = ex.what();
            s376::ChannelLive down;
            down.cfg.name = t.name;
            down.last_error = ex.what();
            down.errors = 1;
            ui.last.channels.push_back(down);
        }
    }
    ui.logs = s376::merge_logs(ui.cfg, 160);
}

void draw_ui(UiState& ui) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("s376-wizard", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted("s376-wizard  ·  operator console  ·  local control (not xT4)");
    if (!ui.status_err.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", ui.status_err.c_str());
    }
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Channels")) {
            if (ImGui::BeginTable("ch", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("Channel");
                ImGui::TableSetupColumn("FPS");
                ImGui::TableSetupColumn("GPU °C");
                ImGui::TableSetupColumn("VRAM");
                ImGui::TableSetupColumn("RAM");
                ImGui::TableSetupColumn("Host age");
                ImGui::TableSetupColumn("Errors");
                ImGui::TableSetupColumn("Last error");
                ImGui::TableHeadersRow();
                for (const auto& ch : ui.last.channels) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(ch.cfg.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.1f", ch.fps);
                    ImGui::TableSetColumnIndex(2);
                    if (ch.gpu_temp_c < 0)
                        ImGui::TextUnformatted("—");
                    else
                        ImGui::Text("%.0f", ch.gpu_temp_c);
                    ImGui::TableSetColumnIndex(3);
                    if (ch.vram_mb < 0)
                        ImGui::TextUnformatted("—");
                    else
                        ImGui::Text("%.0f MiB", ch.vram_mb);
                    ImGui::TableSetColumnIndex(4);
                    if (ch.ram_mb < 0)
                        ImGui::TextUnformatted("—");
                    else
                        ImGui::Text("%.0f MiB", ch.ram_mb);
                    ImGui::TableSetColumnIndex(5);
                    if (ch.host_age_ms < 0)
                        ImGui::TextUnformatted("no host");
                    else
                        ImGui::Text("%.0f ms", ch.host_age_ms);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%d", ch.errors);
                    ImGui::TableSetColumnIndex(7);
                    ImGui::TextUnformatted(ch.last_error.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            if (ImGui::Button("Refresh channels", ImVec2(220, 36))) {
                ui.sup.configure(ui.cfg);
                if (!ui.sup.refresh_all())
                    ui.status_err = "refresh_all failed";
                else
                    ui.toast = "Channels refreshed (session reset)";
            }
            ImGui::SameLine();
            ImGui::TextWrapped("Closes and reopens every channel IG. Clears session "
                               "state, including the Classic NYC skyline.");
            if (ImGui::Button("Launch IGs")) {
                ui.sup.configure(ui.cfg);
                ui.sup.launch("");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Display")) {
            if (ui.last.channels.empty())
                ImGui::TextUnformatted("No channels reporting.");
            else {
                if (ui.sel < 0 || ui.sel >= static_cast<int>(ui.last.channels.size()))
                    ui.sel = 0;
                auto& edit = ui.last.channels[static_cast<std::size_t>(ui.sel)];
                if (ImGui::BeginCombo("Channel", edit.cfg.name.c_str())) {
                    for (int i = 0; i < static_cast<int>(ui.last.channels.size()); ++i) {
                        const bool sel = i == ui.sel;
                        if (ImGui::Selectable(ui.last.channels[static_cast<std::size_t>(i)]
                                                  .cfg.name.c_str(),
                                              sel)) {
                            ui.sel = i;
                            ui.warp_init = false;
                        }
                    }
                    ImGui::EndCombo();
                }
                float hdg = static_cast<float>(edit.cfg.heading_offset_deg);
                float hf = static_cast<float>(edit.cfg.hfov_deg);
                float vf = static_cast<float>(edit.cfg.vfov_deg);
                if (ImGui::SliderFloat("Heading offset (deg, +CCW)", &hdg, -60.f, 60.f, "%.3f"))
                    edit.cfg.heading_offset_deg = hdg;
                if (ImGui::SliderFloat("HFOV (deg)", &hf, 20.f, 80.f, "%.2f"))
                    edit.cfg.hfov_deg = hf;
                if (ImGui::SliderFloat("VFOV (deg)", &vf, 15.f, 50.f, "%.2f"))
                    edit.cfg.vfov_deg = vf;
                if (!ui.warp_init) {
                    std::snprintf(ui.res_buf, sizeof(ui.res_buf), "%s",
                                  s376::format_resolution(edit.cfg.width, edit.cfg.height).c_str());
                }
                if (ImGui::InputText("Resolution (WxH)", ui.res_buf, sizeof(ui.res_buf))) {
                    int rw = edit.cfg.width, rh = edit.cfg.height;
                    if (s376::parse_resolution(ui.res_buf, rw, rh)) {
                        edit.cfg.width = rw;
                        edit.cfg.height = rh;
                    }
                }
                ImGui::Checkbox("Flip vertical", &edit.cfg.flip_vertical);
                const char* pats[] = {"none", "grid5", "white70", "black"};
                int pi = 0;
                const char* cur = s376::display_pattern_name(edit.corr.pattern);
                for (int i = 0; i < 4; ++i) {
                    if (std::strcmp(pats[i], cur) == 0)
                        pi = i;
                }
                if (ImGui::Combo("Test pattern", &pi, pats, 4))
                    edit.corr.pattern = s376::parse_display_pattern(pats[pi]);
                ImGui::SliderFloat("Gamma", &edit.corr.gamma, 0.5f, 2.5f, "%.2f");
                ImGui::SliderFloat("Edge ramp", &edit.corr.blend.ramp, 0.1f, 1.5f, "%.2f");
                ImGui::SliderFloat("Blend left px", &edit.corr.blend.left_px, 0, 256);
                ImGui::SliderFloat("Blend right px", &edit.corr.blend.right_px, 0, 256);
                ImGui::SliderFloat("Blend top px", &edit.corr.blend.top_px, 0, 256);
                ImGui::SliderFloat("Blend bottom px", &edit.corr.blend.bottom_px, 0, 256);
                ImGui::Separator();
                ImGui::TextUnformatted("Warp grid 7×5 (src heading, pitch)");
                if (!ui.warp_init) {
                    for (int r = 0; r < s376::kWarpRows; ++r)
                        for (int c = 0; c < s376::kWarpCols; ++c) {
                            const auto& n =
                                edit.corr.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
                            std::snprintf(ui.warp_buf[r][c], 32, "%.2f, %.2f", n.src_h_deg,
                                          n.src_v_deg);
                        }
                    ui.warp_init = true;
                }
                if (ImGui::BeginTable("warp", s376::kWarpCols, ImGuiTableFlags_Borders)) {
                    for (int r = s376::kWarpRows - 1; r >= 0; --r) {
                        ImGui::TableNextRow();
                        for (int c = 0; c < s376::kWarpCols; ++c) {
                            ImGui::TableSetColumnIndex(c);
                            ImGui::PushID(r * 10 + c);
                            ImGui::SetNextItemWidth(-1);
                            if (ImGui::InputText("##w", ui.warp_buf[r][c], 32)) {
                                float hh = s376::warp_h_deg(c), vv = s376::warp_v_deg(r);
                                std::sscanf(ui.warp_buf[r][c], "%f , %f", &hh, &vv);
                                edit.corr.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]
                                    .src_h_deg = hh;
                                edit.corr.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]
                                    .src_v_deg = vv;
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndTable();
                }
                if (ImGui::Button("Apply to IGs"))
                    apply_display(ui, edit);
                ImGui::SameLine();
                if (ImGui::Button("Save to config")) {
                    apply_display(ui, edit);
                    save_display(ui);
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Info")) {
            const std::string build = std::string("s376-visual ") + s376::version() + "  " +
                                      (ui.last.git.empty() ? s376::git_describe() : ui.last.git) +
                                      "  " + (ui.last.build_date.empty() ? s376::build_date()
                                                                         : ui.last.build_date);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.18f, 1));
            if (ImGui::Button(build.c_str(), ImVec2(-1, 36))) {
                ui.unlock.click(now_ms());
                ui.toast = ui.unlock.toast;
                s376::ControlMsg click;
                click.op = "era_click";
                click.set("t_ms", static_cast<int>(now_ms() & 0x7fffffff));
                for (const auto& t : ui.cfg.targets) {
                    try {
                        auto r = s376::control_request(t.control, click, 300, ui.cfg.allow_sim_lan,
                                                       ui.cfg.allow_lan);
                        if (!r.get("toast").empty())
                            ui.toast = r.get("toast");
                    } catch (...) {
                    }
                }
                if (ui.unlock.unlocked) {
                    s376::ControlMsg un;
                    un.op = "unlock";
                    for (const auto& t : ui.cfg.targets) {
                        try {
                            (void)s376::control_request(t.control, un, 300, ui.cfg.allow_sim_lan,
                                                        ui.cfg.allow_lan);
                        } catch (...) {
                        }
                    }
                }
            }
            ImGui::PopStyleColor();
            if (!ui.toast.empty())
                ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s", ui.toast.c_str());
            if (ui.unlock.unlocked || ui.last.era_unlocked) {
                const bool classic = ui.last.era == "classic";
                if (ImGui::RadioButton("Current", !classic)) {
                    s376::ControlMsg m;
                    m.op = "set_era";
                    m.set("era", "current");
                    for (const auto& t : ui.cfg.targets) {
                        try {
                            (void)s376::control_request(t.control, m, 300, ui.cfg.allow_sim_lan,
                                                        ui.cfg.allow_lan);
                        } catch (...) {
                        }
                    }
                    ui.last.era = "current";
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Classic (pre-2001)", classic)) {
                    s376::ControlMsg m;
                    m.op = "set_era";
                    m.set("era", "classic");
                    for (const auto& t : ui.cfg.targets) {
                        try {
                            (void)s376::control_request(t.control, m, 300, ui.cfg.allow_sim_lan,
                                                        ui.cfg.allow_lan);
                        } catch (...) {
                        }
                    }
                    ui.last.era = "classic";
                }
                ImGui::TextUnformatted("Missiles (visual only)");
                if (ImGui::RadioButton("Missiles Off", !ui.last.missiles)) {
                    s376::ControlMsg m;
                    m.op = "set_missiles";
                    m.set("on", "0");
                    for (const auto& t : ui.cfg.targets) {
                        try {
                            (void)s376::control_request(t.control, m, 300, ui.cfg.allow_sim_lan,
                                                        ui.cfg.allow_lan);
                        } catch (...) {
                        }
                    }
                    ui.last.missiles = false;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Missiles On", ui.last.missiles)) {
                    s376::ControlMsg m;
                    m.op = "set_missiles";
                    m.set("on", "1");
                    for (const auto& t : ui.cfg.targets) {
                        try {
                            (void)s376::control_request(t.control, m, 300, ui.cfg.allow_sim_lan,
                                                        ui.cfg.allow_lan);
                        } catch (...) {
                        }
                    }
                    ui.last.missiles = true;
                }
            }
            ImGui::Separator();
            ImGui::Text("GPU: %s", ui.last.gpu.empty() ? "—" : ui.last.gpu.c_str());
            ImGui::Text("Driver: %s", ui.last.driver.empty() ? "—" : ui.last.driver.c_str());
            ImGui::Text("Airports: %s", ui.last.airports.empty() ? "—" : ui.last.airports.c_str());
            ImGui::TextWrapped("Control is UDP %s on loopback by default. Not the host "
                               "LAN protocol. Simulator LANs are refused unless allow_sim_lan "
                               "is set on a production box.",
                               ui.cfg.targets.empty() ? "127.0.0.1:2376"
                                                      : ui.cfg.targets.front().control.c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Logs")) {
            if (ImGui::Button("Reload"))
                ui.logs = s376::merge_logs(ui.cfg, 200);
            ImGui::BeginChild("log", ImVec2(0, 0), true);
            ImGui::TextUnformatted(ui.logs.empty() ? "(no log yet)" : ui.logs.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

int run_gui(s376::WizardConfig cfg, double duration) {
    if (!glfwInit()) {
        std::cerr << "glfwInit failed — use --headless\n";
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    GLFWwindow* win = glfwCreateWindow(1280, 800, "s376-wizard", nullptr, nullptr);
    if (!win) {
        glfwTerminate();
        std::cerr << "glfwCreateWindow failed\n";
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    UiState ui;
    ui.cfg = std::move(cfg);
    ui.sup.configure(ui.cfg);
    poll_ui(ui);
    const auto t0 = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(win)) {
        if (duration >= 0) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= duration)
                break;
        }
        glfwPollEvents();
        ui.poll_left -= ImGui::GetIO().DeltaTime;
        if (ui.poll_left <= 0) {
            poll_ui(ui);
            ui.poll_left = 0.5f;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        draw_ui(ui);
        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.09f, 0.11f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}

#endif // S376_HAS_WIZARD_UI

} // namespace

int main(int argc, char** argv) {
    std::string config_path;
    bool headless = false;
    bool do_status = false;
    bool do_refresh = false;
    bool do_launch = false;
    double duration = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << flag << " needs an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        }
        if (a == "-c" || a == "--config")
            config_path = need(a.c_str());
        else if (a == "--headless")
            headless = true;
        else if (a == "--status") {
            do_status = true;
            headless = true;
        } else if (a == "--refresh") {
            do_refresh = true;
            headless = true;
        } else if (a == "--launch")
            do_launch = true;
        else if (a == "--duration")
            duration = std::stod(need(a.c_str()));
        else {
            std::cerr << "unknown option " << a << "\n";
            usage();
            return 2;
        }
    }

    s376::log::Options lo;
    lo.log_dir = "logs";
    lo.log_file = "logs/s376-wizard.log";
    s376::log::setup(lo, "wizard");

    s376::WizardConfig cfg = s376::default_wizard_config();
    if (config_path.empty()) {
        std::ifstream probe("config/wizard.toml");
        if (probe)
            config_path = "config/wizard.toml";
    }
    if (!config_path.empty()) {
        try {
            cfg = s376::load_wizard_config(config_path);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            s376::log::shutdown();
            return 2;
        }
    }

    int rc = 0;
    if (headless
#ifndef S376_HAS_WIZARD_UI
        || true
#endif
    ) {
#ifndef S376_HAS_WIZARD_UI
        if (!headless)
            std::cerr << "s376-wizard built without GLFW/ImGui — headless only\n";
#endif
        if (!do_status && !do_refresh && !do_launch)
            do_status = true;
        rc = run_headless(cfg, do_status, do_refresh, do_launch, duration);
    }
#ifdef S376_HAS_WIZARD_UI
    else {
        rc = run_gui(std::move(cfg), duration);
    }
#endif
    s376::log::shutdown();
    return rc;
}
