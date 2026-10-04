#include "nut_server.h"

#if defined(USE_SOCKET_IMPL_LWIP_TCP) || defined(USE_SOCKET_IMPL_LWIP_SOCKETS) || defined(USE_SOCKET_IMPL_BSD_SOCKETS)
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace esphome::nut_server {

static const char *const TAG = "nut_server";

void NutServer::setup() {
  this->listen_ = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (this->listen_ == nullptr) {
    ESP_LOGE(TAG, "Could not create socket");
    this->mark_failed();
    return;
  }
  int enable = 1;
  this->listen_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  struct sockaddr_storage server;
  socklen_t len = socket::set_sockaddr_any(reinterpret_cast<struct sockaddr *>(&server), sizeof(server), this->port_);
  if (this->listen_->setblocking(false) != 0 || len == 0 ||
      this->listen_->bind(reinterpret_cast<struct sockaddr *>(&server), len) != 0 ||
      this->listen_->listen(NUT_SERVER_MAX_CLIENTS) != 0) {
    ESP_LOGE(TAG, "Could not listen on port %u: errno %d", this->port_, errno);
    this->listen_.reset();
    this->mark_failed();
  }
}

void NutServer::loop() {
  if (this->listen_ == nullptr)
    return;
  const uint32_t now = App.get_loop_component_start_time();
  if (this->listen_->ready())
    this->accept_(now);
  for (auto &client : this->clients_) {
    if (client.socket == nullptr)
      continue;
    if (client.tx_len != 0)
      client.flush();
    if (client.socket->ready() && !client.close_after_flush)
      this->read_(client, now);
    if (client.failed || (client.close_after_flush && client.tx_len == 0) ||
        now - client.last_activity > NUT_IDLE_TIMEOUT_MS)
      this->close_(client);
  }
}

void NutServer::dump_config() {
  ESP_LOGCONFIG(TAG,
                "NUT server:\n"
                "  Port: %u\n"
                "  UPS name: %s\n"
                "  Max clients: %u\n"
                "  Instant commands: %s",
                this->port_, this->config_.ups_name, static_cast<unsigned>(NUT_SERVER_MAX_CLIENTS),
                this->config_.username != nullptr ? LOG_STR_LITERAL("enabled") : LOG_STR_LITERAL("disabled"));
#ifdef NUT_SERVER_ALLOWED_IPS_COUNT
  for (const auto &net : this->allowed_) {
    const auto *addr = reinterpret_cast<const uint8_t *>(&net.addr);
    const auto *mask = reinterpret_cast<const uint8_t *>(&net.mask);
    ESP_LOGCONFIG(TAG, "  Allowed: %u.%u.%u.%u/%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3], mask[0], mask[1],
                  mask[2], mask[3]);
  }
#endif
}

void NutServer::on_shutdown() {
  for (auto &client : this->clients_) {
    if (client.socket != nullptr)
      this->close_(client);
  }
  this->listen_.reset();
}

void NutServer::accept_(uint32_t now) {
  while (true) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    auto sock = this->listen_->accept_loop_monitored(reinterpret_cast<struct sockaddr *>(&addr), &addr_len);
    if (sock == nullptr)
      return;
    char peer[socket::SOCKADDR_STR_LEN];
    sock->getpeername_to(peer);
#ifdef NUT_SERVER_ALLOWED_IPS_COUNT
    if (!this->is_allowed_(addr)) {
      ESP_LOGW(TAG, "Rejected %s: not in allowed_ips", peer);
      continue;
    }
#endif
    Client *slot = nullptr;
    for (auto &client : this->clients_) {
      if (client.socket == nullptr) {
        slot = &client;
        break;
      }
    }
    if (slot == nullptr) {
      ESP_LOGW(TAG, "Max clients (%u), rejecting %s", static_cast<unsigned>(NUT_SERVER_MAX_CLIENTS), peer);
      continue;
    }
    if (sock->setblocking(false) != 0) {
      ESP_LOGW(TAG, "Could not set %s non-blocking", peer);
      continue;
    }
    ESP_LOGD(TAG, "Accepted %s", peer);
    slot->socket = std::move(sock);
    slot->session.begin(this, peer);
    slot->last_activity = now;
    slot->line_len = 0;
    slot->tx_len = 0;
    slot->discarding = false;
    slot->close_after_flush = false;
    slot->failed = false;
  }
}

#ifdef NUT_SERVER_ALLOWED_IPS_COUNT
bool NutServer::is_allowed_(const struct sockaddr_storage &peer) const {
  uint32_t addr;
  if (peer.ss_family == AF_INET) {
    addr = reinterpret_cast<const struct sockaddr_in *>(&peer)->sin_addr.s_addr;
#if USE_NETWORK_IPV6
  } else if (peer.ss_family == AF_INET6) {
    // Only IPv4 clients reaching an IPv6 socket (::ffff:a.b.c.d) can match an IPv4 network
    static constexpr uint8_t V4_MAPPED_PREFIX[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    const uint8_t *bytes = reinterpret_cast<const struct sockaddr_in6 *>(&peer)->sin6_addr.s6_addr;
    if (memcmp(bytes, V4_MAPPED_PREFIX, sizeof(V4_MAPPED_PREFIX)) != 0)
      return false;
    memcpy(&addr, bytes + sizeof(V4_MAPPED_PREFIX), sizeof(addr));
#endif
  } else {
    return false;
  }
  for (const auto &net : this->allowed_) {
    if ((addr & net.mask) == net.addr)
      return true;
  }
  return false;
}
#endif

void NutServer::read_(Client &client, uint32_t now) {
  char buf[128];
  // Read until the socket would block, as the socket ready() contract requires
  while (true) {
    ssize_t received = client.socket->read(buf, sizeof(buf));
    if (received == 0) {
      client.failed = true;  // closed by the peer
      return;
    }
    if (received < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        client.failed = true;
      return;
    }
    client.last_activity = now;
    for (ssize_t i = 0; i != received && !client.close_after_flush && !client.failed; i++) {
      char c = buf[i];
      if (c != '\n') {
        if (client.line_len + 1 < NUT_LINE_MAX) {
          client.line[client.line_len++] = c;
        } else {
          client.discarding = true;
        }
        continue;
      }
      if (client.discarding) {
        client.write_str("ERR INVALID-ARGUMENT\n");
      } else {
        if (client.line_len != 0 && client.line[client.line_len - 1] == '\r')
          client.line_len--;
        client.line[client.line_len] = '\0';
        if (!client.session.handle_line(client.line, client))
          client.close_after_flush = true;
      }
      client.line_len = 0;
      client.discarding = false;
    }
    if (client.tx_len != 0)
      client.flush();
  }
}

void NutServer::close_(Client &client) {
  ESP_LOGD(TAG, "Closing %s", client.session.get_peer());
  client.socket.reset();
  client.tx_len = 0;
  client.line_len = 0;
}

void NutServer::Client::write(const char *data, size_t len) {
  while (len != 0 && !this->failed) {
    if (this->tx_len == sizeof(this->tx)) {
      this->flush();
      if (this->tx_len == sizeof(this->tx)) {
        ESP_LOGW(TAG, "%s is not reading replies, disconnecting", this->session.get_peer());
        this->failed = true;
        return;
      }
    }
    size_t chunk = std::min(len, sizeof(this->tx) - this->tx_len);
    memcpy(this->tx + this->tx_len, data, chunk);
    this->tx_len += chunk;
    data += chunk;
    len -= chunk;
  }
}

void NutServer::Client::flush() {
  while (this->tx_len != 0) {
    ssize_t sent = this->socket->write(this->tx, this->tx_len);
    if (sent < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        this->failed = true;
      return;
    }
    if (sent == 0)
      return;
    memmove(this->tx, this->tx + sent, this->tx_len - sent);
    this->tx_len -= sent;
  }
}

}  // namespace esphome::nut_server

#endif
