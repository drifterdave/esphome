#pragma once

#include "hid_report_parser.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::ups_hid {

enum UpsCommand : uint8_t {
  UPS_COMMAND_BEEPER_ENABLE,
  UPS_COMMAND_BEEPER_DISABLE,
  UPS_COMMAND_BEEPER_MUTE,
  UPS_COMMAND_TEST_BATTERY_START_QUICK,
  UPS_COMMAND_TEST_BATTERY_START_DEEP,
  UPS_COMMAND_TEST_BATTERY_STOP,
  UPS_COMMAND_TEST_PANEL_START,
  UPS_COMMAND_TEST_PANEL_STOP,
  UPS_COMMAND_LOAD_OFF_DELAY,
  UPS_COMMAND_SHUTDOWN_REBOOT,
  UPS_COMMAND_SHUTDOWN_STOP,
  UPS_COMMAND_COUNT,
};

/// Command value placeholder replaced by the configured shutdown delay.
static constexpr int32_t COMMAND_VALUE_SHUTDOWN_DELAY = INT32_MIN;

struct UpsCommandInfo {
  const char *name;  // NUT instant command name
  const char *description;
  UpsField field;
  int32_t value;  // physical value written to the field
};

const UpsCommandInfo &get_command_info(UpsCommand command);
/// Look up a command by its NUT name; returns false when unknown.
bool find_command(const char *name, UpsCommand &command);

/// Big enough for any formatted NUT variable value.
static constexpr size_t UPS_VALUE_BUFFER_SIZE = 72;

/// Decoded UPS data and the NUT view of it. Has no hardware dependencies.
class UpsState {
 public:
  /// Forget all values, e.g. when the device is unplugged.
  void reset();
  /// Record which fields the device has, which decides the supported commands.
  void set_field_map(const HidFieldMap &map);
  void set_identity(uint16_t vid, uint16_t pid, const char *manufacturer, const char *product, const char *serial);
  void set_value(UpsField field, float value);
  void set_connected(bool connected) { this->connected_ = connected; }
  void set_shutdown_delay(uint32_t seconds) { this->shutdown_delay_ = seconds; }

  /// True once the device is connected and at least one value has been read.
  bool has_data() const { return this->connected_ && this->valid_mask_ != 0; }
  bool has(UpsField field) const { return (this->valid_mask_ >> field) & 1; }
  float get(UpsField field) const { return this->values_[field]; }
  /// A status bit: true only when known and set.
  bool flag(UpsField field) const { return this->has(field) && this->values_[field] != 0.0f; }
  bool command_supported(UpsCommand command) const;
  uint32_t get_shutdown_delay() const { return this->shutdown_delay_; }

  uint16_t get_vid() const { return this->vid_; }
  uint16_t get_pid() const { return this->pid_; }
  const char *get_manufacturer() const { return this->manufacturer_; }
  const char *get_model() const { return this->model_; }
  const char *get_serial() const { return this->serial_; }
  const char *get_firmware() const { return this->firmware_; }

  /// NUT ups.status, e.g. "OL CHRG". Empty when unknown.
  void format_status(char *buf, size_t len) const;
  /// NUT ups.test.result text, or nullptr when unknown.
  const char *test_result() const;

  /// Number of entries in the NUT variable table; iterate with format_var(index, ...).
  static size_t var_count();
  /// Format the variable at index. Returns its name, or nullptr when it has no value now.
  const char *format_var(size_t index, char *buf, size_t len) const;
  /// Format the named variable; returns false when unknown or without a value.
  bool format_var(const char *name, char *buf, size_t len) const;
  /// True when the name is in the variable table, whether or not it has a value now.
  static bool has_var(const char *name);
  /// True when the named variable is a number (NUT GET TYPE).
  static bool var_is_number(const char *name);

 protected:
  bool format_entry_(size_t index, char *buf, size_t len) const;

  std::array<float, UPS_FIELD_COUNT> values_{};
  uint32_t valid_mask_{0};
  uint32_t feature_mask_{0};
  uint32_t shutdown_delay_{20};
  uint16_t vid_{0};
  uint16_t pid_{0};
  bool connected_{false};
  char manufacturer_[48]{};
  char model_[48]{};
  char serial_[32]{};
  char firmware_[24]{};
  char firmware_aux_[16]{};
};
static_assert(UPS_FIELD_COUNT <= 32, "valid_mask_ holds one bit per field");

/// What the NUT server needs from a UPS driver.
class UpsDevice {
 public:
  virtual const UpsState &get_state() const = 0;
  /// Queue a command for the device; returns false when it cannot be sent.
  virtual bool run_command(UpsCommand command) = 0;

 protected:
  ~UpsDevice() = default;
};

}  // namespace esphome::ups_hid
