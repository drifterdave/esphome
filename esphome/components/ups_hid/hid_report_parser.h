#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::ups_hid {

/// A full HID usage: (usage page << 16) | usage id.
constexpr uint32_t hid_usage(uint16_t page, uint16_t id) { return (static_cast<uint32_t>(page) << 16) | id; }

namespace usage {
// Power Device page (0x84)
static constexpr uint32_t PRESENT_STATUS = hid_usage(0x84, 0x02);
static constexpr uint32_t UPS = hid_usage(0x84, 0x04);
static constexpr uint32_t BATTERY_SYSTEM = hid_usage(0x84, 0x10);
static constexpr uint32_t BATTERY = hid_usage(0x84, 0x12);
static constexpr uint32_t POWER_CONVERTER = hid_usage(0x84, 0x16);
static constexpr uint32_t INPUT = hid_usage(0x84, 0x1A);
static constexpr uint32_t OUTPUT = hid_usage(0x84, 0x1C);
static constexpr uint32_t POWER_SUMMARY = hid_usage(0x84, 0x24);
static constexpr uint32_t VOLTAGE = hid_usage(0x84, 0x30);
static constexpr uint32_t PERCENT_LOAD = hid_usage(0x84, 0x35);
static constexpr uint32_t CONFIG_VOLTAGE = hid_usage(0x84, 0x40);
static constexpr uint32_t CONFIG_ACTIVE_POWER = hid_usage(0x84, 0x44);
static constexpr uint32_t LOW_VOLTAGE_TRANSFER = hid_usage(0x84, 0x53);
static constexpr uint32_t HIGH_VOLTAGE_TRANSFER = hid_usage(0x84, 0x54);
static constexpr uint32_t DELAY_BEFORE_REBOOT = hid_usage(0x84, 0x55);
static constexpr uint32_t DELAY_BEFORE_SHUTDOWN = hid_usage(0x84, 0x57);
static constexpr uint32_t TEST = hid_usage(0x84, 0x58);
static constexpr uint32_t AUDIBLE_ALARM_CONTROL = hid_usage(0x84, 0x5A);
static constexpr uint32_t OVERLOAD = hid_usage(0x84, 0x65);
static constexpr uint32_t SHUTDOWN_IMMINENT = hid_usage(0x84, 0x69);
// Battery System page (0x85)
static constexpr uint32_t REMAINING_CAPACITY_LIMIT = hid_usage(0x85, 0x29);
static constexpr uint32_t REMAINING_TIME_LIMIT = hid_usage(0x85, 0x2A);
static constexpr uint32_t BELOW_REMAINING_CAPACITY_LIMIT = hid_usage(0x85, 0x42);
static constexpr uint32_t REMAINING_TIME_LIMIT_EXPIRED = hid_usage(0x85, 0x43);
static constexpr uint32_t CHARGING = hid_usage(0x85, 0x44);
static constexpr uint32_t DISCHARGING = hid_usage(0x85, 0x45);
static constexpr uint32_t NEED_REPLACEMENT = hid_usage(0x85, 0x4B);
static constexpr uint32_t REMAINING_CAPACITY = hid_usage(0x85, 0x66);
static constexpr uint32_t RUN_TIME_TO_EMPTY = hid_usage(0x85, 0x68);
static constexpr uint32_t MANUFACTURER_DATE = hid_usage(0x85, 0x85);
static constexpr uint32_t AC_PRESENT = hid_usage(0x85, 0xD0);
static constexpr uint32_t BATTERY_PRESENT = hid_usage(0x85, 0xD1);
// APC vendor page (0xFF86), names as used by NUT's apc-hid subdriver
static constexpr uint32_t APC_GENERAL_COLLECTION = hid_usage(0xFF86, 0x05);
static constexpr uint32_t APC_BATT_REPLACE_DATE = hid_usage(0xFF86, 0x16);
static constexpr uint32_t APC_PANEL_TEST = hid_usage(0xFF86, 0x72);
static constexpr uint32_t APC_DELAY_BEFORE_REBOOT = hid_usage(0xFF86, 0x7C);
static constexpr uint32_t APC_DELAY_BEFORE_SHUTDOWN = hid_usage(0xFF86, 0x7D);
}  // namespace usage

/// The UPS values this driver understands. Each maps to one HID field per report type.
enum UpsField : uint8_t {
  UPS_FIELD_BATTERY_CHARGE,
  UPS_FIELD_BATTERY_CHARGE_LOW,
  UPS_FIELD_BATTERY_RUNTIME,
  UPS_FIELD_BATTERY_RUNTIME_LOW,
  UPS_FIELD_BATTERY_VOLTAGE,
  UPS_FIELD_BATTERY_VOLTAGE_NOMINAL,
  UPS_FIELD_BATTERY_DATE,
  UPS_FIELD_BATTERY_DATE_APC,
  UPS_FIELD_INPUT_VOLTAGE,
  UPS_FIELD_INPUT_VOLTAGE_NOMINAL,
  UPS_FIELD_INPUT_TRANSFER_LOW,
  UPS_FIELD_INPUT_TRANSFER_HIGH,
  UPS_FIELD_OUTPUT_VOLTAGE,
  UPS_FIELD_LOAD,
  UPS_FIELD_REALPOWER_NOMINAL,
  UPS_FIELD_BEEPER,
  UPS_FIELD_TEST,
  UPS_FIELD_PANEL_TEST,
  UPS_FIELD_DELAY_SHUTDOWN,
  UPS_FIELD_DELAY_REBOOT,
  UPS_FIELD_AC_PRESENT,
  UPS_FIELD_CHARGING,
  UPS_FIELD_DISCHARGING,
  UPS_FIELD_BELOW_CAPACITY_LIMIT,
  UPS_FIELD_TIME_LIMIT_EXPIRED,
  UPS_FIELD_SHUTDOWN_IMMINENT,
  UPS_FIELD_NEED_REPLACEMENT,
  UPS_FIELD_OVERLOAD,
  UPS_FIELD_BATTERY_PRESENT,
  UPS_FIELD_COUNT,
};

enum HidReportType : uint8_t {
  HID_REPORT_TYPE_INPUT = 1,
  HID_REPORT_TYPE_OUTPUT = 2,
  HID_REPORT_TYPE_FEATURE = 3,
};

/// Where one value lives inside a report, and how to scale it.
struct HidField {
  int32_t logical_min{0};
  int32_t logical_max{0};
  int32_t physical_min{0};
  int32_t physical_max{0};
  uint32_t unit{0};
  /// Bit offset from the first data bit of the report, not counting the report ID byte.
  uint16_t bit_offset{0};
  /// Report length in bytes, including the report ID byte when report_id is not 0.
  uint16_t report_bytes{0};
  uint8_t bit_size{0};
  uint8_t report_id{0};
  int8_t unit_exponent{0};
  bool has_physical{false};

  bool present() const { return this->bit_size != 0; }
  /// Read the logical value from a report buffer (starting with the report ID byte when report_id is not 0).
  /// Returns false when the buffer is too short.
  bool extract(const uint8_t *report, size_t len, int32_t &value) const;
  /// Write a logical value into a report buffer laid out as for extract().
  bool insert(uint8_t *report, size_t len, int32_t value) const;
  /// Convert a logical value to physical units, applying the physical range and unit exponent.
  float to_physical(int32_t logical) const;
  /// Inverse of to_physical(); clamped to the logical range only when a physical range is given.
  int32_t to_logical(float physical) const;
};

/// The fields found in a report descriptor, by type.
struct HidFieldMap {
  std::array<HidField, UPS_FIELD_COUNT> input{};
  std::array<HidField, UPS_FIELD_COUNT> feature{};
  /// Longest input report in bytes, for sizing interrupt transfers.
  uint16_t max_input_report_bytes{0};

  const HidField &get(HidReportType type, UpsField field) const {
    return type == HID_REPORT_TYPE_FEATURE ? this->feature[field] : this->input[field];
  }
  bool has_feature(UpsField field) const { return this->feature[field].present(); }
  bool has_any(UpsField field) const { return this->input[field].present() || this->feature[field].present(); }
};

enum HidParseResult : uint8_t {
  HID_PARSE_RESULT_OK,
  HID_PARSE_RESULT_TRUNCATED,
  HID_PARSE_RESULT_TOO_DEEP,
  HID_PARSE_RESULT_TOO_MANY_REPORTS,
};

/// Parse a HID report descriptor and record the location of every known UPS field.
HidParseResult parse_report_descriptor(const uint8_t *desc, size_t len, HidFieldMap &out);

}  // namespace esphome::ups_hid
