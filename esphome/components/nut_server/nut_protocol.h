#pragma once

#include "esphome/components/ups_hid/ups_data.h"

#include <cstddef>
#include <cstdint>

namespace esphome::nut_server {

/// Longest request line accepted, including the terminator.
static constexpr size_t NUT_LINE_MAX = 256;
static constexpr size_t NUT_PEER_MAX = 48;
static constexpr size_t NUT_CREDENTIAL_MAX = 64;

/// Sink for protocol replies.
class NutWriter {
 public:
  virtual void write(const char *data, size_t len) = 0;
  void write_str(const char *str);
  void printf(const char *format, ...) __attribute__((format(printf, 2, 3)));
  /// Write a value in double quotes, escaping '"' and '\' as upsd does.
  void write_quoted(const char *value);

 protected:
  ~NutWriter() = default;
};

struct NutConfig {
  const char *ups_name{"ups"};
  const char *description{""};
  /// nullptr when no user is configured; instant commands and LOGIN are then refused.
  const char *username{nullptr};
  const char *password{nullptr};
};

class NutSession;

/// The server side state a session needs: configuration, the UPS, and the other sessions.
class NutHost {
 public:
  virtual const NutConfig &get_nut_config() const = 0;
  virtual ups_hid::UpsDevice *get_ups_device() = 0;
  /// Number of session slots; get_session() returns nullptr for an unused slot.
  virtual size_t get_session_count() const = 0;
  virtual const NutSession *get_session(size_t index) const = 0;

 protected:
  ~NutHost() = default;
};

/// One client connection's protocol state. Has no network dependencies.
class NutSession {
 public:
  void begin(NutHost *host, const char *peer);
  /// Handle one request line without its line ending; the line is modified in place.
  /// Returns false when the client should be disconnected after the reply is sent.
  bool handle_line(char *line, NutWriter &out);
  bool is_logged_in() const { return this->logged_in_; }
  const char *get_peer() const { return this->peer_; }

 protected:
  void handle_list_(int argc, char **argv, NutWriter &out);
  void handle_get_(int argc, char **argv, NutWriter &out);
  void handle_instcmd_(int argc, char **argv, NutWriter &out);
  void handle_login_(int argc, char **argv, NutWriter &out);
  void handle_credential_(char *dest, bool &is_set, const char *value, const char *already_set_error, NutWriter &out);
  /// Writes the matching ERR line and returns false unless the session holds the configured credentials.
  bool check_credentials_(NutWriter &out) const;
  /// Writes ERR UNKNOWN-UPS and returns false when name is not the served UPS.
  bool check_ups_(const char *name, NutWriter &out) const;
  /// Writes ERR DATA-STALE and returns false when the UPS has no current data.
  bool check_data_(NutWriter &out) const;
  size_t count_logins_() const;

  NutHost *host_{nullptr};
  char peer_[NUT_PEER_MAX]{};
  char username_[NUT_CREDENTIAL_MAX]{};
  char password_[NUT_CREDENTIAL_MAX]{};
  bool username_set_{false};
  bool password_set_{false};
  bool logged_in_{false};
};

/// Split a request into words, honouring double quotes and backslash escapes. Modifies line in place.
/// Returns the number of words, or -1 for an unterminated quote or too many words.
int split_args(char *line, char **argv, int max_args);

}  // namespace esphome::nut_server
