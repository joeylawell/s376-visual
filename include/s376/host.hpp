#pragma once

#include "s376/config.hpp"
#include "s376/state.hpp"
#include "s376/terrain.hpp"
#include "s376/xt4.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace s376 {

class HostSocket {
  public:
    HostSocket();
    ~HostSocket();

    HostSocket(const HostSocket&) = delete;
    HostSocket& operator=(const HostSocket&) = delete;

    void open(const HostConfig& cfg);
    void close();

    // One datagram. Returns false on timeout / interrupt.
    bool recv(std::vector<std::uint8_t>& buf, std::string& peer_host, std::uint16_t& peer_port,
              int timeout_ms);

    void send_hat(const std::string& dest_host, std::uint16_t dest_port, float hat_ft,
                  float elev_ft, bool valid);

    const HostConfig& config() const { return cfg_; }
    bool is_open() const { return fd_ >= 0; }

  private:
    HostConfig cfg_;
    int fd_ = -1;
#ifdef _WIN32
    bool wsa_ = false;
#endif
};

class HostReceiver {
  public:
    HostReceiver(SharedState& state, HostConfig cfg);
    ~HostReceiver();

    void start();
    void stop();

    void set_heights(std::shared_ptr<HeightSource> h);

    xt4::Decoder& decoder() { return dec_; }
    const xt4::Decoder& decoder() const { return dec_; }

  private:
    void loop();

    SharedState& state_;
    HostConfig cfg_;
    xt4::Decoder dec_;
    HostSocket sock_;
    std::thread th_;
    std::atomic<bool> run_{false};
    std::shared_ptr<HeightSource> heights_;
    bool have_prev_eye_ = false;
    double prev_lat_ = 0;
    double prev_lon_ = 0;
    double prev_alt_ft_ = 0;
};

} // namespace s376
