#include "nut_protocol.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace esphome::nut_server {

using ups_hid::UpsCommand;
using ups_hid::UpsState;

static constexpr int MAX_ARGS = 8;

void NutWriter::write_str(const char *str) { this->write(str, strlen(str)); }

void NutWriter::printf(const char *format, ...) {
  char buf[192];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (len < 0)
    return;
  this->write(buf, static_cast<size_t>(len) < sizeof(buf) ? static_cast<size_t>(len) : sizeof(buf) - 1);
}

void NutWriter::write_quoted(const char *value) {
  this->write("\"", 1);
  const char *run = value;
  for (const char *p = value; *p != '\0'; p++) {
    if (*p == '"' || *p == '\\') {
      this->write(run, p - run);
      this->write("\\", 1);
      run = p;
    }
  }
  this->write(run, strlen(run));
  this->write("\"", 1);
}

int split_args(char *line, char **argv, int max_args) {
  int argc = 0;
  char *p = line;
  while (true) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '\0')
      return argc;
    if (argc == max_args)
      return -1;
    char *out = p;
    argv[argc++] = out;
    if (*p == '"') {
      p++;
      while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0')
          p++;
        *out++ = *p++;
      }
      if (*p != '"')
        return -1;
      p++;
    } else {
      while (*p != '\0' && *p != ' ' && *p != '\t') {
        if (*p == '\\' && p[1] != '\0')
          p++;
        *out++ = *p++;
      }
    }
    char next = *p;
    *out = '\0';
    if (next == '\0')
      return argc;
    p++;
  }
}

/// Compare two strings in time that depends only on the length of the expected one.
static bool credential_equal(const char *given, const char *expected) {
  size_t given_len = strlen(given);
  size_t expected_len = strlen(expected);
  uint8_t diff = given_len != expected_len;
  for (size_t i = 0; i != expected_len; i++)
    diff |= static_cast<uint8_t>(expected[i] ^ (i < given_len ? given[i] : 0));
  return diff == 0;
}

static bool word_is(const char *word, const char *expected) { return strcasecmp(word, expected) == 0; }

void NutSession::begin(NutHost *host, const char *peer) {
  this->host_ = host;
  snprintf(this->peer_, sizeof(this->peer_), "%s", peer);
  this->username_[0] = '\0';
  this->password_[0] = '\0';
  this->username_set_ = false;
  this->password_set_ = false;
  this->logged_in_ = false;
}

bool NutSession::handle_line(char *line, NutWriter &out) {
  char *argv[MAX_ARGS];
  int argc = split_args(line, argv, MAX_ARGS);
  if (argc < 0) {
    out.write_str("ERR INVALID-ARGUMENT\n");
    return true;
  }
  if (argc == 0)
    return true;
  const char *command = argv[0];
  if (word_is(command, "LIST")) {
    this->handle_list_(argc, argv, out);
  } else if (word_is(command, "GET")) {
    this->handle_get_(argc, argv, out);
  } else if (word_is(command, "INSTCMD")) {
    this->handle_instcmd_(argc, argv, out);
  } else if (word_is(command, "USERNAME")) {
    if (argc != 2) {
      out.write_str("ERR INVALID-ARGUMENT\n");
    } else {
      this->handle_credential_(this->username_, this->username_set_, argv[1], "ALREADY-SET-USERNAME", out);
    }
  } else if (word_is(command, "PASSWORD")) {
    if (argc != 2) {
      out.write_str("ERR INVALID-ARGUMENT\n");
    } else {
      this->handle_credential_(this->password_, this->password_set_, argv[1], "ALREADY-SET-PASSWORD", out);
    }
  } else if (word_is(command, "LOGIN")) {
    this->handle_login_(argc, argv, out);
  } else if (word_is(command, "LOGOUT")) {
    out.write_str("OK Goodbye\n");
    return false;
  } else if (word_is(command, "VER")) {
    out.write_str("Network UPS Tools upsd 2.8.2 - ESPHome nut_server\n");
  } else if (word_is(command, "NETVER")) {
    out.write_str("1.3\n");
  } else if (word_is(command, "HELP")) {
    out.write_str("Commands: HELP VER GET LIST SET INSTCMD LOGIN LOGOUT USERNAME PASSWORD STARTTLS\n");
  } else if (word_is(command, "STARTTLS")) {
    out.write_str("ERR FEATURE-NOT-CONFIGURED\n");
  } else if (word_is(command, "SET")) {
    if (argc < 5 || !word_is(argv[1], "VAR")) {
      out.write_str("ERR INVALID-ARGUMENT\n");
    } else if (this->check_ups_(argv[2], out)) {
      out.write_str(UpsState::has_var(argv[3]) ? "ERR READONLY\n" : "ERR VAR-NOT-SUPPORTED\n");
    }
  } else if (word_is(command, "PRIMARY") || word_is(command, "MASTER") || word_is(command, "FSD")) {
    // Shutdown coordination for upsmon primaries is not offered.
    out.write_str("ERR ACCESS-DENIED\n");
  } else {
    out.write_str("ERR UNKNOWN-COMMAND\n");
  }
  return true;
}

void NutSession::handle_list_(int argc, char **argv, NutWriter &out) {
  const NutConfig &config = this->host_->get_nut_config();
  if (argc == 2 && word_is(argv[1], "UPS")) {
    out.write_str("BEGIN LIST UPS\n");
    out.printf("UPS %s ", config.ups_name);
    out.write_quoted(config.description);
    out.write_str("\nEND LIST UPS\n");
    return;
  }
  if (argc < 3) {
    out.write_str("ERR INVALID-ARGUMENT\n");
    return;
  }
  const char *type = argv[1];
  const char *ups = argv[2];
  if (argc == 3 && word_is(type, "VAR")) {
    if (!this->check_ups_(ups, out) || !this->check_data_(out))
      return;
    const UpsState &state = this->host_->get_ups_device()->get_state();
    char value[ups_hid::UPS_VALUE_BUFFER_SIZE];
    out.printf("BEGIN LIST VAR %s\n", ups);
    for (size_t i = 0; i != UpsState::var_count(); i++) {
      const char *name = state.format_var(i, value, sizeof(value));
      if (name == nullptr)
        continue;
      out.printf("VAR %s %s ", ups, name);
      out.write_quoted(value);
      out.write_str("\n");
    }
    out.printf("END LIST VAR %s\n", ups);
  } else if (argc == 3 && word_is(type, "CMD")) {
    if (!this->check_ups_(ups, out))
      return;
    const UpsState &state = this->host_->get_ups_device()->get_state();
    out.printf("BEGIN LIST CMD %s\n", ups);
    for (uint8_t i = 0; i != ups_hid::UPS_COMMAND_COUNT; i++) {
      auto command = static_cast<UpsCommand>(i);
      if (state.command_supported(command))
        out.printf("CMD %s %s\n", ups, ups_hid::get_command_info(command).name);
    }
    out.printf("END LIST CMD %s\n", ups);
  } else if (argc == 3 && word_is(type, "RW")) {
    if (this->check_ups_(ups, out))
      out.printf("BEGIN LIST RW %s\nEND LIST RW %s\n", ups, ups);
  } else if (argc == 3 && word_is(type, "CLIENT")) {
    if (!this->check_ups_(ups, out))
      return;
    out.printf("BEGIN LIST CLIENT %s\n", ups);
    for (size_t i = 0; i != this->host_->get_session_count(); i++) {
      const NutSession *session = this->host_->get_session(i);
      if (session != nullptr && session->is_logged_in())
        out.printf("CLIENT %s %s\n", ups, session->get_peer());
    }
    out.printf("END LIST CLIENT %s\n", ups);
  } else if (argc == 4 && (word_is(type, "ENUM") || word_is(type, "RANGE"))) {
    if (!this->check_ups_(ups, out))
      return;
    if (!UpsState::has_var(argv[3])) {
      out.write_str("ERR VAR-NOT-SUPPORTED\n");
      return;
    }
    const char *kind = word_is(type, "ENUM") ? "ENUM" : "RANGE";
    out.printf("BEGIN LIST %s %s %s\nEND LIST %s %s %s\n", kind, ups, argv[3], kind, ups, argv[3]);
  } else {
    out.write_str("ERR INVALID-ARGUMENT\n");
  }
}

void NutSession::handle_get_(int argc, char **argv, NutWriter &out) {
  if (argc < 3) {
    out.write_str("ERR INVALID-ARGUMENT\n");
    return;
  }
  const char *type = argv[1];
  const char *ups = argv[2];
  if (argc == 3 && word_is(type, "UPSDESC")) {
    if (!this->check_ups_(ups, out))
      return;
    out.printf("UPSDESC %s ", ups);
    out.write_quoted(this->host_->get_nut_config().description);
    out.write_str("\n");
  } else if (argc == 3 && word_is(type, "NUMLOGINS")) {
    if (this->check_ups_(ups, out))
      out.printf("NUMLOGINS %s %zu\n", ups, this->count_logins_());
  } else if (argc == 4 && word_is(type, "VAR")) {
    if (!this->check_ups_(ups, out) || !this->check_data_(out))
      return;
    char value[ups_hid::UPS_VALUE_BUFFER_SIZE];
    if (!this->host_->get_ups_device()->get_state().format_var(argv[3], value, sizeof(value))) {
      out.write_str("ERR VAR-NOT-SUPPORTED\n");
      return;
    }
    out.printf("VAR %s %s ", ups, argv[3]);
    out.write_quoted(value);
    out.write_str("\n");
  } else if (argc == 4 && word_is(type, "TYPE")) {
    if (!this->check_ups_(ups, out))
      return;
    if (!UpsState::has_var(argv[3])) {
      out.write_str("ERR VAR-NOT-SUPPORTED\n");
      return;
    }
    out.printf("TYPE %s %s %s\n", ups, argv[3], UpsState::var_is_number(argv[3]) ? "NUMBER" : "STRING:64");
  } else if (argc == 4 && word_is(type, "DESC")) {
    if (!this->check_ups_(ups, out))
      return;
    if (!UpsState::has_var(argv[3])) {
      out.write_str("ERR VAR-NOT-SUPPORTED\n");
      return;
    }
    out.printf("DESC %s %s \"Description unavailable\"\n", ups, argv[3]);
  } else if (argc == 4 && word_is(type, "CMDDESC")) {
    if (!this->check_ups_(ups, out))
      return;
    UpsCommand command;
    if (!ups_hid::find_command(argv[3], command)) {
      out.write_str("ERR CMD-NOT-SUPPORTED\n");
      return;
    }
    out.printf("CMDDESC %s %s ", ups, argv[3]);
    out.write_quoted(ups_hid::get_command_info(command).description);
    out.write_str("\n");
  } else {
    out.write_str("ERR INVALID-ARGUMENT\n");
  }
}

void NutSession::handle_instcmd_(int argc, char **argv, NutWriter &out) {
  // INSTCMD <ups> <command> [<value>]; the optional value is not used by any supported command
  if (argc != 3 && argc != 4) {
    out.write_str("ERR INVALID-ARGUMENT\n");
    return;
  }
  if (!this->check_credentials_(out) || !this->check_ups_(argv[1], out))
    return;
  UpsCommand command;
  ups_hid::UpsDevice *device = this->host_->get_ups_device();
  if (!ups_hid::find_command(argv[2], command) || !device->get_state().command_supported(command)) {
    out.write_str("ERR CMD-NOT-SUPPORTED\n");
    return;
  }
  out.write_str(device->run_command(command) ? "OK\n" : "ERR INSTCMD-FAILED\n");
}

void NutSession::handle_login_(int argc, char **argv, NutWriter &out) {
  if (argc != 2) {
    out.write_str("ERR INVALID-ARGUMENT\n");
    return;
  }
  if (this->logged_in_) {
    out.write_str("ERR ALREADY-LOGGED-IN\n");
    return;
  }
  if (!this->check_credentials_(out) || !this->check_ups_(argv[1], out))
    return;
  this->logged_in_ = true;
  out.write_str("OK\n");
}

void NutSession::handle_credential_(char *dest, bool &is_set, const char *value, const char *already_set_error,
                                    NutWriter &out) {
  if (is_set) {
    out.printf("ERR %s\n", already_set_error);
    return;
  }
  if (strlen(value) >= NUT_CREDENTIAL_MAX) {
    out.write_str("ERR INVALID-ARGUMENT\n");
    return;
  }
  strcpy(dest, value);  // NOLINT(clang-analyzer-security.insecureAPI.strcpy) length checked above
  is_set = true;
  out.write_str("OK\n");
}

bool NutSession::check_credentials_(NutWriter &out) const {
  if (!this->username_set_) {
    out.write_str("ERR USERNAME-REQUIRED\n");
    return false;
  }
  if (!this->password_set_) {
    out.write_str("ERR PASSWORD-REQUIRED\n");
    return false;
  }
  const NutConfig &config = this->host_->get_nut_config();
  // Evaluate both comparisons so the timing does not reveal which one failed
  bool user_ok = config.username != nullptr && credential_equal(this->username_, config.username);
  bool password_ok = config.password != nullptr && credential_equal(this->password_, config.password);
  if (!user_ok || !password_ok) {
    out.write_str("ERR ACCESS-DENIED\n");
    return false;
  }
  return true;
}

bool NutSession::check_ups_(const char *name, NutWriter &out) const {
  if (strcmp(name, this->host_->get_nut_config().ups_name) == 0)
    return true;
  out.write_str("ERR UNKNOWN-UPS\n");
  return false;
}

bool NutSession::check_data_(NutWriter &out) const {
  if (this->host_->get_ups_device()->get_state().has_data())
    return true;
  out.write_str("ERR DATA-STALE\n");
  return false;
}

size_t NutSession::count_logins_() const {
  size_t count = 0;
  for (size_t i = 0; i != this->host_->get_session_count(); i++) {
    const NutSession *session = this->host_->get_session(i);
    if (session != nullptr && session->is_logged_in())
      count++;
  }
  return count;
}

}  // namespace esphome::nut_server
