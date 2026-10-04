#pragma once

#include "esphome/core/defines.h"
#include "esphome/components/socket/socket.h"

#if defined(USE_SOCKET_IMPL_LWIP_TCP) || defined(USE_SOCKET_IMPL_LWIP_SOCKETS) || defined(USE_SOCKET_IMPL_BSD_SOCKETS)
#include "esphome/core/component.h"
#ifdef USE_SOCKET_IPV4_ALLOW
#include "esphome/components/socket/ipv4_allow.h"
#endif
#include "nut_protocol.h"

#include <array>
#include <memory>

namespace esphome::nut_server {

static constexpr size_t NUT_TX_BUFFER_SIZE = 1024;
/// Clients that send nothing for this long are disconnected (upsmon polls every few seconds).
static constexpr uint32_t NUT_IDLE_TIMEOUT_MS = 5 * 60 * 1000;

/// Serves one UPS over the Network UPS Tools protocol (upsd), TCP port 3493 by default.
class NutServer : public Component, public NutHost {
 public:
  explicit NutServer(ups_hid::UpsDevice *device) : device_(device) {}
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_port(uint16_t port) { this->port_ = port; }
  void set_ups_name(const char *name) { this->config_.ups_name = name; }
  void set_description(const char *description) { this->config_.description = description; }
  void set_credentials(const char *username, const char *password) {
    this->config_.username = username;
    this->config_.password = password;
  }
#ifdef USE_SOCKET_IPV4_ALLOW
  void set_allow(const socket::Ipv4AllowEntry *entries, size_t count) { this->allow_.set(entries, count); }
#endif

  const NutConfig &get_nut_config() const override { return this->config_; }
  ups_hid::UpsDevice *get_ups_device() override { return this->device_; }
  size_t get_session_count() const override { return NUT_SERVER_MAX_CLIENTS; }
  const NutSession *get_session(size_t index) const override {
    return this->clients_[index].socket != nullptr ? &this->clients_[index].session : nullptr;
  }

 protected:
  struct Client final : public NutWriter {
    /// Queue reply bytes, sending to the socket whenever the buffer fills.
    void write(const char *data, size_t len) override;
    /// Send as much queued output as the socket takes. Sets failed on a socket error.
    void flush();

    std::unique_ptr<socket::Socket> socket;
    NutSession session;
    uint32_t last_activity{0};
    uint16_t line_len{0};
    uint16_t tx_len{0};
    bool discarding{false};
    bool close_after_flush{false};
    bool failed{false};
    char line[NUT_LINE_MAX];
    char tx[NUT_TX_BUFFER_SIZE];
  };

  void accept_(uint32_t now);
  void read_(Client &client, uint32_t now);
  void close_(Client &client);

  ups_hid::UpsDevice *device_;
  std::unique_ptr<socket::ListenSocket> listen_;
  std::array<Client, NUT_SERVER_MAX_CLIENTS> clients_{};
  NutConfig config_{};
#ifdef USE_SOCKET_IPV4_ALLOW
  socket::Ipv4Allow allow_;
#endif
  uint16_t port_{3493};
};

}  // namespace esphome::nut_server

#endif
