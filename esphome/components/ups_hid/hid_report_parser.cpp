#include "hid_report_parser.h"

#include <cmath>

namespace esphome::ups_hid {

namespace {

constexpr size_t MAX_COLLECTION_DEPTH = 8;
constexpr size_t MAX_LOCAL_USAGES = 16;
constexpr size_t MAX_GLOBAL_PUSH = 4;
constexpr size_t MAX_REPORTS = 128;
constexpr uint8_t NO_RULE = 0xFF;

// Units whose SI definition carries a base exponent (HID Power Device Class 3.2.3).
constexpr uint32_t UNIT_VOLT = 0x00F0D121;
constexpr uint32_t UNIT_WATT = 0x0000D121;
constexpr int8_t UNIT_VOLT_WATT_EXPONENT = 7;

/// A usage path suffix: outer and parent are the two innermost collections, 0 matches any.
struct FieldRule {
  uint32_t outer;
  uint32_t parent;
  uint32_t usage;
  UpsField field;
};

// Ordered by priority: for each UpsField, the first matching rule wins. Mirrors NUT's apc-hid and
// usbhid-ups mapping tables.
constexpr FieldRule RULES[] = {
    {0, usage::POWER_SUMMARY, usage::REMAINING_CAPACITY, UPS_FIELD_BATTERY_CHARGE},
    {0, usage::BATTERY, usage::REMAINING_CAPACITY, UPS_FIELD_BATTERY_CHARGE},
    {0, usage::POWER_SUMMARY, usage::REMAINING_CAPACITY_LIMIT, UPS_FIELD_BATTERY_CHARGE_LOW},
    {0, usage::BATTERY, usage::REMAINING_CAPACITY_LIMIT, UPS_FIELD_BATTERY_CHARGE_LOW},
    {0, usage::BATTERY, usage::RUN_TIME_TO_EMPTY, UPS_FIELD_BATTERY_RUNTIME},
    {0, usage::POWER_SUMMARY, usage::RUN_TIME_TO_EMPTY, UPS_FIELD_BATTERY_RUNTIME},
    {0, usage::BATTERY, usage::REMAINING_TIME_LIMIT, UPS_FIELD_BATTERY_RUNTIME_LOW},
    {0, usage::POWER_SUMMARY, usage::REMAINING_TIME_LIMIT, UPS_FIELD_BATTERY_RUNTIME_LOW},
    {0, usage::BATTERY, usage::VOLTAGE, UPS_FIELD_BATTERY_VOLTAGE},
    {0, usage::POWER_SUMMARY, usage::VOLTAGE, UPS_FIELD_BATTERY_VOLTAGE},
    {0, usage::BATTERY, usage::CONFIG_VOLTAGE, UPS_FIELD_BATTERY_VOLTAGE_NOMINAL},
    {0, usage::POWER_SUMMARY, usage::CONFIG_VOLTAGE, UPS_FIELD_BATTERY_VOLTAGE_NOMINAL},
    {0, usage::BATTERY, usage::MANUFACTURER_DATE, UPS_FIELD_BATTERY_DATE},
    {0, usage::POWER_SUMMARY, usage::APC_BATT_REPLACE_DATE, UPS_FIELD_BATTERY_DATE_APC},
    {0, usage::INPUT, usage::VOLTAGE, UPS_FIELD_INPUT_VOLTAGE},
    {0, usage::INPUT, usage::CONFIG_VOLTAGE, UPS_FIELD_INPUT_VOLTAGE_NOMINAL},
    {0, usage::OUTPUT, usage::LOW_VOLTAGE_TRANSFER, UPS_FIELD_INPUT_TRANSFER_LOW},
    {0, usage::INPUT, usage::LOW_VOLTAGE_TRANSFER, UPS_FIELD_INPUT_TRANSFER_LOW},
    {0, usage::OUTPUT, usage::HIGH_VOLTAGE_TRANSFER, UPS_FIELD_INPUT_TRANSFER_HIGH},
    {0, usage::INPUT, usage::HIGH_VOLTAGE_TRANSFER, UPS_FIELD_INPUT_TRANSFER_HIGH},
    {0, usage::OUTPUT, usage::VOLTAGE, UPS_FIELD_OUTPUT_VOLTAGE},
    {0, usage::OUTPUT, usage::PERCENT_LOAD, UPS_FIELD_LOAD},
    {0, usage::POWER_CONVERTER, usage::PERCENT_LOAD, UPS_FIELD_LOAD},
    {0, usage::POWER_SUMMARY, usage::PERCENT_LOAD, UPS_FIELD_LOAD},
    {0, usage::POWER_CONVERTER, usage::CONFIG_ACTIVE_POWER, UPS_FIELD_REALPOWER_NOMINAL},
    {0, usage::OUTPUT, usage::CONFIG_ACTIVE_POWER, UPS_FIELD_REALPOWER_NOMINAL},
    {0, usage::POWER_SUMMARY, usage::AUDIBLE_ALARM_CONTROL, UPS_FIELD_BEEPER},
    {0, usage::BATTERY, usage::TEST, UPS_FIELD_TEST},
    {0, 0, usage::APC_PANEL_TEST, UPS_FIELD_PANEL_TEST},
    {0, usage::POWER_SUMMARY, usage::DELAY_BEFORE_SHUTDOWN, UPS_FIELD_DELAY_SHUTDOWN},
    {0, usage::OUTPUT, usage::DELAY_BEFORE_SHUTDOWN, UPS_FIELD_DELAY_SHUTDOWN},
    {0, usage::APC_GENERAL_COLLECTION, usage::APC_DELAY_BEFORE_SHUTDOWN, UPS_FIELD_DELAY_SHUTDOWN},
    {0, usage::POWER_SUMMARY, usage::DELAY_BEFORE_REBOOT, UPS_FIELD_DELAY_REBOOT},
    {0, usage::OUTPUT, usage::DELAY_BEFORE_REBOOT, UPS_FIELD_DELAY_REBOOT},
    {0, usage::APC_GENERAL_COLLECTION, usage::APC_DELAY_BEFORE_REBOOT, UPS_FIELD_DELAY_REBOOT},
    // Status bits: UPS.PowerSummary.PresentStatus.x first, then UPS.PowerSummary.x (Back-UPS 500)
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::AC_PRESENT, UPS_FIELD_AC_PRESENT},
    {0, usage::POWER_SUMMARY, usage::AC_PRESENT, UPS_FIELD_AC_PRESENT},
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::CHARGING, UPS_FIELD_CHARGING},
    {0, usage::POWER_SUMMARY, usage::CHARGING, UPS_FIELD_CHARGING},
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::DISCHARGING, UPS_FIELD_DISCHARGING},
    {0, usage::POWER_SUMMARY, usage::DISCHARGING, UPS_FIELD_DISCHARGING},
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::BELOW_REMAINING_CAPACITY_LIMIT,
     UPS_FIELD_BELOW_CAPACITY_LIMIT},
    {0, usage::POWER_SUMMARY, usage::BELOW_REMAINING_CAPACITY_LIMIT, UPS_FIELD_BELOW_CAPACITY_LIMIT},
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::REMAINING_TIME_LIMIT_EXPIRED, UPS_FIELD_TIME_LIMIT_EXPIRED},
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::SHUTDOWN_IMMINENT, UPS_FIELD_SHUTDOWN_IMMINENT},
    {0, usage::POWER_SUMMARY, usage::SHUTDOWN_IMMINENT, UPS_FIELD_SHUTDOWN_IMMINENT},
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::NEED_REPLACEMENT, UPS_FIELD_NEED_REPLACEMENT},
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::OVERLOAD, UPS_FIELD_OVERLOAD},
    {usage::POWER_SUMMARY, usage::PRESENT_STATUS, usage::BATTERY_PRESENT, UPS_FIELD_BATTERY_PRESENT},
};
static_assert(sizeof(RULES) / sizeof(RULES[0]) < NO_RULE, "rule index must fit in uint8_t");

/// Number of bits needed to hold x: 1 + the position of the highest set bit, or 0.
unsigned hibit(uint32_t x) {
  unsigned res = 0;
  while (x != 0) {
    x >>= 1;
    res++;
  }
  return res;
}

int32_t sign_extend(uint32_t value, uint8_t size) {
  switch (size) {
    case 1:
      return static_cast<int8_t>(value);
    case 2:
      return static_cast<int16_t>(value);
    default:
      return static_cast<int32_t>(value);
  }
}

struct GlobalState {
  uint32_t usage_page{0};
  int32_t logical_min{0};
  int32_t logical_max{0};
  uint32_t logical_max_raw{0};
  int32_t physical_min{0};
  int32_t physical_max{0};
  uint32_t unit{0};
  uint16_t report_count{0};
  uint8_t report_size{0};
  uint8_t report_id{0};
  int8_t unit_exponent{0};
  bool has_physical_min{false};
  bool has_physical_max{false};
};

struct ReportCursor {
  uint16_t bits;
  uint8_t type;
  uint8_t id;
};

class Parser {
 public:
  explicit Parser(HidFieldMap &out) : out_(out) {
    this->best_input_.fill(NO_RULE);
    this->best_feature_.fill(NO_RULE);
  }

  HidParseResult parse(const uint8_t *desc, size_t len) {
    size_t pos = 0;
    while (pos < len) {
      uint8_t prefix = desc[pos++];
      if (prefix == 0xFE) {
        // Long item: size, tag, then data. Not used by power devices; skip it.
        if (pos + 2 > len)
          return HID_PARSE_RESULT_TRUNCATED;
        pos += 2 + desc[pos];
        continue;
      }
      uint8_t size = prefix & 0x03;
      if (size == 3)
        size = 4;
      if (pos + size > len)
        return HID_PARSE_RESULT_TRUNCATED;
      uint32_t value = 0;
      for (uint8_t i = 0; i != size; i++)
        value |= static_cast<uint32_t>(desc[pos + i]) << (8 * i);
      pos += size;
      HidParseResult result = this->item_((prefix >> 2) & 0x03, prefix >> 4, value, size);
      if (result != HID_PARSE_RESULT_OK)
        return result;
    }
    this->finish_();
    return HID_PARSE_RESULT_OK;
  }

 protected:
  HidParseResult item_(uint8_t type, uint8_t tag, uint32_t value, uint8_t size) {
    switch (type) {
      case 0:
        return this->main_item_(tag, value);
      case 1:
        return this->global_item_(tag, value, size);
      case 2:
        this->local_item_(tag, value, size);
        return HID_PARSE_RESULT_OK;
      default:
        return HID_PARSE_RESULT_OK;
    }
  }

  HidParseResult global_item_(uint8_t tag, uint32_t value, uint8_t size) {
    GlobalState &g = this->global_;
    switch (tag) {
      case 0x0:
        g.usage_page = value;
        break;
      case 0x1:
        g.logical_min = sign_extend(value, size);
        break;
      case 0x2:
        g.logical_max = sign_extend(value, size);
        g.logical_max_raw = value;
        break;
      case 0x3:
        g.physical_min = sign_extend(value, size);
        g.has_physical_min = true;
        break;
      case 0x4:
        g.physical_max = sign_extend(value, size);
        g.has_physical_max = true;
        break;
      case 0x5: {
        // 4-bit two's complement nibble per the HID spec
        auto exponent = static_cast<int8_t>(value);
        if (exponent > 7)
          exponent = static_cast<int8_t>(exponent | 0xF0);
        g.unit_exponent = exponent;
        break;
      }
      case 0x6:
        g.unit = value;
        break;
      case 0x7:
        g.report_size = static_cast<uint8_t>(value);
        break;
      case 0x8:
        g.report_id = static_cast<uint8_t>(value);
        break;
      case 0x9:
        g.report_count = static_cast<uint16_t>(value);
        break;
      case 0xA:
        if (this->push_depth_ == MAX_GLOBAL_PUSH)
          return HID_PARSE_RESULT_TOO_DEEP;
        this->push_stack_[this->push_depth_++] = g;
        break;
      case 0xB:
        if (this->push_depth_ != 0)
          g = this->push_stack_[--this->push_depth_];
        break;
      default:
        break;
    }
    return HID_PARSE_RESULT_OK;
  }

  void local_item_(uint8_t tag, uint32_t value, uint8_t size) {
    // A short usage is relative to the current usage page; a 4-byte one carries its own page.
    uint32_t full = size == 4 ? value : hid_usage(this->global_.usage_page, value);
    switch (tag) {
      case 0x0:
        if (this->usage_count_ < MAX_LOCAL_USAGES)
          this->usages_[this->usage_count_++] = full;
        break;
      case 0x1:
        this->usage_min_ = full;
        this->has_usage_range_ = true;
        break;
      case 0x2:
        this->usage_max_ = full;
        break;
      default:
        break;
    }
  }

  HidParseResult main_item_(uint8_t tag, uint32_t value) {
    HidParseResult result = HID_PARSE_RESULT_OK;
    switch (tag) {
      case 0x8:  // Input
        result = this->data_item_(HID_REPORT_TYPE_INPUT, value);
        break;
      case 0x9:  // Output
        result = this->data_item_(HID_REPORT_TYPE_OUTPUT, value);
        break;
      case 0xB:  // Feature
        result = this->data_item_(HID_REPORT_TYPE_FEATURE, value);
        break;
      case 0xA:  // Collection
        if (this->depth_ == MAX_COLLECTION_DEPTH)
          return HID_PARSE_RESULT_TOO_DEEP;
        this->collections_[this->depth_++] = this->usage_at_(0);
        break;
      case 0xC:  // End Collection
        if (this->depth_ != 0)
          this->depth_--;
        break;
      default:
        break;
    }
    this->usage_count_ = 0;
    this->has_usage_range_ = false;
    return result;
  }

  uint32_t usage_at_(uint16_t index) const {
    if (this->usage_count_ != 0)
      return this->usages_[index < this->usage_count_ ? index : this->usage_count_ - 1];
    if (this->has_usage_range_) {
      uint32_t candidate = this->usage_min_ + index;
      return candidate > this->usage_max_ ? this->usage_max_ : candidate;
    }
    return 0;
  }

  HidParseResult data_item_(HidReportType type, uint32_t flags) {
    const GlobalState &g = this->global_;
    ReportCursor *cursor = this->cursor_(type, g.report_id);
    if (cursor == nullptr)
      return HID_PARSE_RESULT_TOO_MANY_REPORTS;
    // Constant fields are padding; array fields carry selectors, not values.
    bool is_value = (flags & 0x01) == 0 && (flags & 0x02) != 0;
    if (is_value && type != HID_REPORT_TYPE_OUTPUT && g.report_size != 0 && g.report_size <= 32) {
      for (uint16_t i = 0; i != g.report_count; i++)
        this->record_(type, this->usage_at_(i), cursor->bits + i * g.report_size);
    }
    cursor->bits += g.report_size * g.report_count;
    return HID_PARSE_RESULT_OK;
  }

  ReportCursor *cursor_(uint8_t type, uint8_t id) {
    for (size_t i = 0; i != this->report_count_; i++) {
      if (this->reports_[i].type == type && this->reports_[i].id == id)
        return &this->reports_[i];
    }
    if (this->report_count_ == MAX_REPORTS)
      return nullptr;
    this->reports_[this->report_count_] = {0, type, id};
    return &this->reports_[this->report_count_++];
  }

  void record_(HidReportType type, uint32_t field_usage, uint32_t bit_offset) {
    uint32_t parent = this->depth_ >= 1 ? this->collections_[this->depth_ - 1] : 0;
    uint32_t outer = this->depth_ >= 2 ? this->collections_[this->depth_ - 2] : 0;
    auto &best = type == HID_REPORT_TYPE_FEATURE ? this->best_feature_ : this->best_input_;
    auto &fields = type == HID_REPORT_TYPE_FEATURE ? this->out_.feature : this->out_.input;
    for (uint8_t r = 0; r != sizeof(RULES) / sizeof(RULES[0]); r++) {
      const FieldRule &rule = RULES[r];
      if (rule.usage != field_usage || (rule.parent != 0 && rule.parent != parent) ||
          (rule.outer != 0 && rule.outer != outer))
        continue;
      if (best[rule.field] <= r)
        return;  // an equal or better rule already claimed this field
      best[rule.field] = r;
      const GlobalState &g = this->global_;
      HidField &f = fields[rule.field];
      f = HidField{};
      f.logical_min = g.logical_min;
      // Devices often encode an unsigned maximum such as 0xFF in one byte; reinterpret it like NUT does.
      f.logical_max = g.logical_max < g.logical_min ? static_cast<int32_t>(g.logical_max_raw) : g.logical_max;
      f.has_physical = g.has_physical_min && g.has_physical_max && (g.physical_min != 0 || g.physical_max != 0);
      f.physical_min = g.physical_min;
      f.physical_max = g.physical_max;
      f.unit = g.unit;
      f.unit_exponent = g.unit_exponent;
      f.bit_offset = static_cast<uint16_t>(bit_offset);
      f.bit_size = g.report_size;
      f.report_id = g.report_id;
      return;
    }
  }

  uint16_t report_bytes_(uint8_t type, uint8_t id) const {
    for (size_t i = 0; i != this->report_count_; i++) {
      if (this->reports_[i].type == type && this->reports_[i].id == id)
        return (this->reports_[i].bits + 7) / 8 + (id != 0 ? 1 : 0);
    }
    return 0;
  }

  void finish_() {
    for (uint8_t f = 0; f != UPS_FIELD_COUNT; f++) {
      if (this->out_.input[f].present())
        this->out_.input[f].report_bytes = this->report_bytes_(HID_REPORT_TYPE_INPUT, this->out_.input[f].report_id);
      if (this->out_.feature[f].present()) {
        this->out_.feature[f].report_bytes =
            this->report_bytes_(HID_REPORT_TYPE_FEATURE, this->out_.feature[f].report_id);
      }
    }
    for (size_t i = 0; i != this->report_count_; i++) {
      if (this->reports_[i].type != HID_REPORT_TYPE_INPUT)
        continue;
      uint16_t bytes = this->report_bytes_(HID_REPORT_TYPE_INPUT, this->reports_[i].id);
      if (bytes > this->out_.max_input_report_bytes)
        this->out_.max_input_report_bytes = bytes;
    }
    // Some APC units (NUT apc_fix_report_desc) give an input voltage range below the high transfer point,
    // which would clamp every reading in 230 V regions. Widen it the same way NUT does.
    const HidField &high = this->out_.feature[UPS_FIELD_INPUT_TRANSFER_HIGH];
    if (!high.present())
      return;
    for (auto *fields : {&this->out_.input, &this->out_.feature}) {
      HidField &voltage = (*fields)[UPS_FIELD_INPUT_VOLTAGE];
      if (voltage.present() && high.logical_max > voltage.logical_max) {
        voltage.logical_min = 0;
        voltage.logical_max = high.logical_max * 2;
      }
      HidField &nominal = (*fields)[UPS_FIELD_INPUT_VOLTAGE_NOMINAL];
      if (nominal.present() && high.logical_max > nominal.logical_max)
        nominal.logical_max = 255;
    }
  }

  HidFieldMap &out_;
  GlobalState global_{};
  std::array<GlobalState, MAX_GLOBAL_PUSH> push_stack_{};
  std::array<uint32_t, MAX_COLLECTION_DEPTH> collections_{};
  std::array<uint32_t, MAX_LOCAL_USAGES> usages_{};
  std::array<ReportCursor, MAX_REPORTS> reports_{};
  std::array<uint8_t, UPS_FIELD_COUNT> best_input_{};
  std::array<uint8_t, UPS_FIELD_COUNT> best_feature_{};
  uint32_t usage_min_{0};
  uint32_t usage_max_{0};
  size_t report_count_{0};
  uint8_t push_depth_{0};
  uint8_t depth_{0};
  uint8_t usage_count_{0};
  bool has_usage_range_{false};
};

int8_t unit_base_exponent(uint32_t unit) {
  return unit == UNIT_VOLT || unit == UNIT_WATT ? UNIT_VOLT_WATT_EXPONENT : 0;
}

}  // namespace

bool HidField::extract(const uint8_t *report, size_t len, int32_t &value) const {
  uint32_t first_bit = (this->report_id != 0 ? 8 : 0) + this->bit_offset;
  if ((first_bit + this->bit_size + 7) / 8 > len)
    return false;
  uint64_t raw = 0;
  for (uint8_t w = 0; w != this->bit_size; w++) {
    uint32_t bit = first_bit + w;
    if ((report[bit >> 3] >> (bit & 7)) & 1)
      raw |= uint64_t{1} << w;
  }
  if (this->logical_min == 0 && this->logical_max == 0) {
    // No range given: take the bits as they are.
    value = static_cast<int32_t>(raw);
    return true;
  }
  // Drop excess high bits and sign-extend based on the logical range (NUT GetValue).
  auto magnitude = [](int32_t v) -> uint32_t {
    return v >= 0 ? static_cast<uint32_t>(v) : static_cast<uint32_t>(-(static_cast<int64_t>(v) + 1));
  };
  uint32_t mag_max = magnitude(this->logical_max);
  uint32_t mag_min = magnitude(this->logical_min);
  uint64_t sign_bit = uint64_t{1} << hibit(mag_max > mag_min ? mag_max : mag_min);
  uint64_t mask = (sign_bit - 1) | (this->logical_min < 0 ? sign_bit : 0);
  raw &= mask;
  int64_t result = static_cast<int64_t>(raw);
  if (this->logical_min < 0 && (raw & sign_bit) != 0)
    result = static_cast<int64_t>(raw | ~mask);
  if (result < this->logical_min) {
    result = this->logical_min;
  } else if (result > this->logical_max) {
    result = this->logical_max;
  }
  value = static_cast<int32_t>(result);
  return true;
}

bool HidField::insert(uint8_t *report, size_t len, int32_t value) const {
  uint32_t first_bit = (this->report_id != 0 ? 8 : 0) + this->bit_offset;
  if ((first_bit + this->bit_size + 7) / 8 > len)
    return false;
  auto bits = static_cast<uint32_t>(value);
  for (uint8_t w = 0; w != this->bit_size; w++) {
    uint32_t bit = first_bit + w;
    auto mask = static_cast<uint8_t>(1 << (bit & 7));
    if ((bits >> w) & 1) {
      report[bit >> 3] |= mask;
    } else {
      report[bit >> 3] &= static_cast<uint8_t>(~mask);
    }
  }
  return true;
}

float HidField::to_physical(int32_t logical) const {
  double value = logical;
  if (this->has_physical && this->physical_max > this->physical_min && this->logical_max > this->logical_min) {
    value = this->physical_min + (static_cast<double>(logical) - this->logical_min) *
                                     (static_cast<double>(this->physical_max) - this->physical_min) /
                                     (static_cast<double>(this->logical_max) - this->logical_min);
  }
  return static_cast<float>(value * std::pow(10.0, this->unit_exponent - unit_base_exponent(this->unit)));
}

int32_t HidField::to_logical(float physical) const {
  double value = physical / std::pow(10.0, this->unit_exponent - unit_base_exponent(this->unit));
  // Without a physical range the value goes out as is, so -1 (cancel) becomes all ones, as with NUT.
  if (!this->has_physical || this->physical_max <= this->physical_min || this->logical_max <= this->logical_min)
    return static_cast<int32_t>(std::lround(value));
  auto result = std::lround(this->logical_min + (value - this->physical_min) *
                                                    (static_cast<double>(this->logical_max) - this->logical_min) /
                                                    (static_cast<double>(this->physical_max) - this->physical_min));
  if (result < this->logical_min)
    return this->logical_min;
  if (result > this->logical_max)
    return this->logical_max;
  return static_cast<int32_t>(result);
}

HidParseResult parse_report_descriptor(const uint8_t *desc, size_t len, HidFieldMap &out) {
  out = HidFieldMap{};
  Parser parser(out);
  return parser.parse(desc, len);
}

}  // namespace esphome::ups_hid
