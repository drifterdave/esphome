#include "ups_data.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace esphome::ups_hid {

namespace {

constexpr UpsCommandInfo COMMANDS[UPS_COMMAND_COUNT] = {
    {"beeper.enable", "Enable the UPS beeper", UPS_FIELD_BEEPER, 2},
    {"beeper.disable", "Disable the UPS beeper", UPS_FIELD_BEEPER, 1},
    {"beeper.mute", "Temporarily mute the UPS beeper", UPS_FIELD_BEEPER, 3},
    {"test.battery.start.quick", "Start a quick battery test", UPS_FIELD_TEST, 1},
    {"test.battery.start.deep", "Start a deep battery test", UPS_FIELD_TEST, 2},
    {"test.battery.stop", "Stop the battery test", UPS_FIELD_TEST, 3},
    {"test.panel.start", "Start testing the UPS panel", UPS_FIELD_PANEL_TEST, 1},
    {"test.panel.stop", "Stop a UPS panel test", UPS_FIELD_PANEL_TEST, 0},
    {"load.off.delay", "Turn off the load with a delay (seconds)", UPS_FIELD_DELAY_SHUTDOWN,
     COMMAND_VALUE_SHUTDOWN_DELAY},
    {"shutdown.reboot", "Shut down the load briefly while rebooting the UPS", UPS_FIELD_DELAY_REBOOT,
     COMMAND_VALUE_SHUTDOWN_DELAY},
    {"shutdown.stop", "Stop a shutdown in progress", UPS_FIELD_DELAY_SHUTDOWN, -1},
};

enum VarFormat : uint8_t {
  VAR_FORMAT_INTEGER,
  VAR_FORMAT_DECIMAL,
  VAR_FORMAT_BEEPER,
  VAR_FORMAT_TEST_RESULT,
  VAR_FORMAT_BATTERY_DATE,
  VAR_FORMAT_MANUFACTURER,
  VAR_FORMAT_MODEL,
  VAR_FORMAT_SERIAL,
  VAR_FORMAT_FIRMWARE,
  VAR_FORMAT_FIRMWARE_AUX,
  VAR_FORMAT_VENDOR_ID,
  VAR_FORMAT_PRODUCT_ID,
  VAR_FORMAT_STATUS,
  VAR_FORMAT_SHUTDOWN_DELAY,
  VAR_FORMAT_DEVICE_TYPE,
  VAR_FORMAT_DRIVER_NAME,
};

struct VarSpec {
  const char *name;
  VarFormat format;
  UpsField field;  // only used by the numeric and lookup formats
};

// Sorted by name, as upsd lists them.
constexpr VarSpec VARS[] = {
    {"battery.charge", VAR_FORMAT_INTEGER, UPS_FIELD_BATTERY_CHARGE},
    {"battery.charge.low", VAR_FORMAT_INTEGER, UPS_FIELD_BATTERY_CHARGE_LOW},
    {"battery.mfr.date", VAR_FORMAT_BATTERY_DATE, UPS_FIELD_BATTERY_DATE},
    {"battery.runtime", VAR_FORMAT_INTEGER, UPS_FIELD_BATTERY_RUNTIME},
    {"battery.runtime.low", VAR_FORMAT_INTEGER, UPS_FIELD_BATTERY_RUNTIME_LOW},
    {"battery.voltage", VAR_FORMAT_DECIMAL, UPS_FIELD_BATTERY_VOLTAGE},
    {"battery.voltage.nominal", VAR_FORMAT_DECIMAL, UPS_FIELD_BATTERY_VOLTAGE_NOMINAL},
    {"device.mfr", VAR_FORMAT_MANUFACTURER, UPS_FIELD_COUNT},
    {"device.model", VAR_FORMAT_MODEL, UPS_FIELD_COUNT},
    {"device.serial", VAR_FORMAT_SERIAL, UPS_FIELD_COUNT},
    {"device.type", VAR_FORMAT_DEVICE_TYPE, UPS_FIELD_COUNT},
    {"driver.name", VAR_FORMAT_DRIVER_NAME, UPS_FIELD_COUNT},
    {"input.transfer.high", VAR_FORMAT_INTEGER, UPS_FIELD_INPUT_TRANSFER_HIGH},
    {"input.transfer.low", VAR_FORMAT_INTEGER, UPS_FIELD_INPUT_TRANSFER_LOW},
    {"input.voltage", VAR_FORMAT_DECIMAL, UPS_FIELD_INPUT_VOLTAGE},
    {"input.voltage.nominal", VAR_FORMAT_INTEGER, UPS_FIELD_INPUT_VOLTAGE_NOMINAL},
    {"output.voltage", VAR_FORMAT_DECIMAL, UPS_FIELD_OUTPUT_VOLTAGE},
    {"ups.beeper.status", VAR_FORMAT_BEEPER, UPS_FIELD_BEEPER},
    {"ups.delay.shutdown", VAR_FORMAT_SHUTDOWN_DELAY, UPS_FIELD_DELAY_SHUTDOWN},
    {"ups.firmware", VAR_FORMAT_FIRMWARE, UPS_FIELD_COUNT},
    {"ups.firmware.aux", VAR_FORMAT_FIRMWARE_AUX, UPS_FIELD_COUNT},
    {"ups.load", VAR_FORMAT_INTEGER, UPS_FIELD_LOAD},
    {"ups.mfr", VAR_FORMAT_MANUFACTURER, UPS_FIELD_COUNT},
    {"ups.model", VAR_FORMAT_MODEL, UPS_FIELD_COUNT},
    {"ups.productid", VAR_FORMAT_PRODUCT_ID, UPS_FIELD_COUNT},
    {"ups.realpower.nominal", VAR_FORMAT_INTEGER, UPS_FIELD_REALPOWER_NOMINAL},
    {"ups.serial", VAR_FORMAT_SERIAL, UPS_FIELD_COUNT},
    {"ups.status", VAR_FORMAT_STATUS, UPS_FIELD_COUNT},
    {"ups.test.result", VAR_FORMAT_TEST_RESULT, UPS_FIELD_TEST},
    {"ups.timer.reboot", VAR_FORMAT_INTEGER, UPS_FIELD_DELAY_REBOOT},
    {"ups.timer.shutdown", VAR_FORMAT_INTEGER, UPS_FIELD_DELAY_SHUTDOWN},
    {"ups.vendorid", VAR_FORMAT_VENDOR_ID, UPS_FIELD_COUNT},
};
constexpr size_t VAR_COUNT = sizeof(VARS) / sizeof(VARS[0]);

const char *beeper_text(int value) {
  switch (value) {
    case 1:
      return "disabled";
    case 2:
      return "enabled";
    case 3:
      return "muted";
    default:
      return nullptr;
  }
}

const char *test_result_text(int value) {
  switch (value) {
    case 1:
      return "Done and passed";
    case 2:
      return "Done and warning";
    case 3:
      return "Done and error";
    case 4:
      return "Aborted";
    case 5:
      return "In progress";
    case 6:
      return "No test initiated";
    case 7:
      return "Test scheduled";
    default:
      return nullptr;
  }
}

/// Copy a string, trimming surrounding spaces; never overflows dst.
void copy_trimmed(char *dst, size_t dst_size, const char *src, size_t src_len) {
  while (src_len != 0 && *src == ' ') {
    src++;
    src_len--;
  }
  while (src_len != 0 && src[src_len - 1] == ' ')
    src_len--;
  if (src_len >= dst_size)
    src_len = dst_size - 1;
  memcpy(dst, src, src_len);
  dst[src_len] = '\0';
}

bool copy_string(char *buf, size_t len, const char *value) {
  if (value[0] == '\0')
    return false;
  snprintf(buf, len, "%s", value);
  return true;
}

}  // namespace

const UpsCommandInfo &get_command_info(UpsCommand command) { return COMMANDS[command]; }

bool find_command(const char *name, UpsCommand &command) {
  for (uint8_t i = 0; i != UPS_COMMAND_COUNT; i++) {
    if (strcmp(COMMANDS[i].name, name) == 0) {
      command = static_cast<UpsCommand>(i);
      return true;
    }
  }
  return false;
}

void UpsState::reset() {
  this->valid_mask_ = 0;
  this->feature_mask_ = 0;
  this->connected_ = false;
}

void UpsState::set_field_map(const HidFieldMap &map) {
  this->feature_mask_ = 0;
  for (uint8_t f = 0; f != UPS_FIELD_COUNT; f++) {
    if (map.has_feature(static_cast<UpsField>(f)))
      this->feature_mask_ |= 1u << f;
  }
}

void UpsState::set_identity(uint16_t vid, uint16_t pid, const char *manufacturer, const char *product,
                            const char *serial) {
  this->vid_ = vid;
  this->pid_ = pid;
  copy_trimmed(this->manufacturer_, sizeof(this->manufacturer_), manufacturer, strlen(manufacturer));
  copy_trimmed(this->serial_, sizeof(this->serial_), serial, strlen(serial));
  // APC products read like "Back-UPS ES 600M1 FW:928.a5 .D USB FW:a5" (NUT apc_format_model).
  size_t product_len = strlen(product);
  this->firmware_[0] = '\0';
  this->firmware_aux_[0] = '\0';
  const char *fw = strstr(product, "FW:");
  if (fw == nullptr) {
    copy_trimmed(this->model_, sizeof(this->model_), product, product_len);
    return;
  }
  copy_trimmed(this->model_, sizeof(this->model_), product, fw - product);
  fw += 3;
  const char *usb_fw = strstr(fw, "USB FW:");
  if (usb_fw == nullptr) {
    copy_trimmed(this->firmware_, sizeof(this->firmware_), fw, strlen(fw));
    return;
  }
  copy_trimmed(this->firmware_, sizeof(this->firmware_), fw, usb_fw - fw);
  usb_fw += 7;
  copy_trimmed(this->firmware_aux_, sizeof(this->firmware_aux_), usb_fw, strlen(usb_fw));
}

void UpsState::set_value(UpsField field, float value) {
  // Some CyberPower units report a charge above 100 % (NUT cps_battcharge_fun)
  if (field == UPS_FIELD_BATTERY_CHARGE && value > 100.0f)
    value = 100.0f;
  this->values_[field] = value;
  this->valid_mask_ |= 1u << field;
}

bool UpsState::command_supported(UpsCommand command) const {
  return (this->feature_mask_ >> COMMANDS[command].field) & 1;
}

void UpsState::format_status(char *buf, size_t len) const {
  size_t pos = 0;
  buf[0] = '\0';
  auto add = [&](const char *word) {
    int written = snprintf(buf + pos, len - pos, pos == 0 ? "%s" : " %s", word);
    if (written > 0 && pos + written < len)
      pos += written;
  };
  if (this->has(UPS_FIELD_AC_PRESENT))
    add(this->flag(UPS_FIELD_AC_PRESENT) ? "OL" : "OB");
  if (this->flag(UPS_FIELD_DISCHARGING))
    add("DISCHRG");
  if (this->flag(UPS_FIELD_CHARGING))
    add("CHRG");
  if (this->flag(UPS_FIELD_BELOW_CAPACITY_LIMIT) || this->flag(UPS_FIELD_TIME_LIMIT_EXPIRED) ||
      this->flag(UPS_FIELD_SHUTDOWN_IMMINENT))
    add("LB");
  if (this->flag(UPS_FIELD_OVERLOAD))
    add("OVER");
  if (this->flag(UPS_FIELD_NEED_REPLACEMENT) ||
      (this->has(UPS_FIELD_BATTERY_PRESENT) && !this->flag(UPS_FIELD_BATTERY_PRESENT)))
    add("RB");
}

const char *UpsState::test_result() const {
  if (!this->has(UPS_FIELD_TEST))
    return nullptr;
  return test_result_text(static_cast<int>(this->values_[UPS_FIELD_TEST]));
}

size_t UpsState::var_count() { return VAR_COUNT; }

const char *UpsState::format_var(size_t index, char *buf, size_t len) const {
  if (index >= VAR_COUNT || !this->format_entry_(index, buf, len))
    return nullptr;
  return VARS[index].name;
}

bool UpsState::format_var(const char *name, char *buf, size_t len) const {
  for (size_t i = 0; i != VAR_COUNT; i++) {
    if (strcmp(VARS[i].name, name) == 0)
      return this->format_entry_(i, buf, len);
  }
  return false;
}

bool UpsState::has_var(const char *name) {
  for (const auto &var : VARS) {
    if (strcmp(var.name, name) == 0)
      return true;
  }
  return false;
}

bool UpsState::var_is_number(const char *name) {
  for (const auto &var : VARS) {
    if (strcmp(var.name, name) == 0) {
      return var.format == VAR_FORMAT_INTEGER || var.format == VAR_FORMAT_DECIMAL ||
             var.format == VAR_FORMAT_SHUTDOWN_DELAY;
    }
  }
  return false;
}

bool UpsState::format_entry_(size_t index, char *buf, size_t len) const {
  const VarSpec &var = VARS[index];
  switch (var.format) {
    case VAR_FORMAT_INTEGER:
      if (!this->has(var.field))
        return false;
      snprintf(buf, len, "%.0f", this->values_[var.field]);
      return true;
    case VAR_FORMAT_DECIMAL:
      if (!this->has(var.field))
        return false;
      snprintf(buf, len, "%.1f", this->values_[var.field]);
      return true;
    case VAR_FORMAT_BEEPER: {
      const char *text = this->has(var.field) ? beeper_text(static_cast<int>(this->values_[var.field])) : nullptr;
      return text != nullptr && copy_string(buf, len, text);
    }
    case VAR_FORMAT_TEST_RESULT: {
      const char *text = this->test_result();
      return text != nullptr && copy_string(buf, len, text);
    }
    case VAR_FORMAT_BATTERY_DATE: {
      int year, month, day;
      if (this->has(UPS_FIELD_BATTERY_DATE)) {
        // HID date: days in bits 0-4, months in bits 5-8, years since 1980 above
        auto value = static_cast<uint32_t>(this->values_[UPS_FIELD_BATTERY_DATE]);
        year = 1980 + static_cast<int>(value >> 9);
        month = static_cast<int>((value >> 5) & 0x0F);
        day = static_cast<int>(value & 0x1F);
      } else if (this->has(UPS_FIELD_BATTERY_DATE_APC)) {
        // APC writes the date as hex digits, 0x102202 = 2002/10/22
        auto value = static_cast<uint32_t>(this->values_[UPS_FIELD_BATTERY_DATE_APC]);
        if (value == 0)
          return copy_string(buf, len, "not set");
        year = static_cast<int>((value & 0x0F) + 10 * ((value >> 4) & 0x0F));
        month = static_cast<int>(((value >> 16) & 0x0F) + 10 * ((value >> 20) & 0x0F));
        day = static_cast<int>(((value >> 8) & 0x0F) + 10 * ((value >> 12) & 0x0F));
        year += year >= 70 ? 1900 : 2000;
      } else {
        return false;
      }
      snprintf(buf, len, "%04d/%02d/%02d", year, month, day);
      return true;
    }
    case VAR_FORMAT_MANUFACTURER:
      return copy_string(buf, len, this->manufacturer_);
    case VAR_FORMAT_MODEL:
      return copy_string(buf, len, this->model_);
    case VAR_FORMAT_SERIAL:
      return copy_string(buf, len, this->serial_);
    case VAR_FORMAT_FIRMWARE:
      return copy_string(buf, len, this->firmware_);
    case VAR_FORMAT_FIRMWARE_AUX:
      return copy_string(buf, len, this->firmware_aux_);
    case VAR_FORMAT_VENDOR_ID:
      snprintf(buf, len, "%04" PRIx16, this->vid_);
      return this->vid_ != 0;
    case VAR_FORMAT_PRODUCT_ID:
      snprintf(buf, len, "%04" PRIx16, this->pid_);
      return this->vid_ != 0;
    case VAR_FORMAT_STATUS:
      this->format_status(buf, len);
      return buf[0] != '\0';
    case VAR_FORMAT_SHUTDOWN_DELAY:
      if (!((this->feature_mask_ >> var.field) & 1))
        return false;
      snprintf(buf, len, "%" PRIu32, this->shutdown_delay_);
      return true;
    case VAR_FORMAT_DEVICE_TYPE:
      return copy_string(buf, len, "ups");
    case VAR_FORMAT_DRIVER_NAME:
      return copy_string(buf, len, "esphome-ups_hid");
  }
  return false;
}

}  // namespace esphome::ups_hid
