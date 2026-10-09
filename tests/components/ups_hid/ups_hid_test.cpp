#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "esphome/components/ups_hid/hid_report_parser.h"
#include "esphome/components/ups_hid/ups_data.h"

namespace esphome::ups_hid::testing {

// A report descriptor laid out like APC Back-UPS ones (as described by NUT's apc-hid subdriver):
// feature and input reports for UPS.PowerSummary, a PresentStatus bit field, input voltage with the
// one-byte logical maximum quirk, a battery test, and APC vendor usages.
static const std::vector<uint8_t> APC_LIKE_DESCRIPTOR = {
    0x05, 0x84,                                      // Usage Page (Power Device)
    0x09, 0x04,                                      // Usage (UPS)
    0xA1, 0x01,                                      // Collection (Application)
    0x09, 0x24,                                      //   Usage (PowerSummary)
    0xA1, 0x02,                                      //   Collection (Logical)
    0x85, 0x0C,                                      //     Report ID (0x0C)
    0x75, 0x08, 0x95, 0x01,                          //     Report Size 8, Count 1
    0x15, 0x00, 0x26, 0xFF, 0x00,                    //     Logical 0..255
    0x05, 0x85, 0x09, 0x66,                          //     Usage (RemainingCapacity)
    0xB1, 0x02,                                      //     Feature (Data, Var)
    0x09, 0x66, 0x81, 0x02,                          //     Usage (RemainingCapacity), Input (Data, Var)
    0x75, 0x10,                                      //     Report Size 16
    0x27, 0xFF, 0xFF, 0x00, 0x00,                    //     Logical Max 65535
    0x09, 0x68, 0xB1, 0x02,                          //     Usage (RunTimeToEmpty), Feature
    0x09, 0x68, 0x81, 0x02,                          //     Usage (RunTimeToEmpty), Input
    0x85, 0x09,                                      //     Report ID (0x09)
    0x05, 0x84, 0x09, 0x30,                          //     Usage (Voltage)
    0x67, 0x21, 0xD1, 0xF0, 0x00,                    //     Unit (Volt)
    0x55, 0x05,                                      //     Unit Exponent (5) -> centivolts after the volt base of 7
    0xB1, 0x02,                                      //     Feature
    0x65, 0x00, 0x55, 0x00,                          //     Unit (None), Unit Exponent (0)
    0x85, 0x18,                                      //     Report ID (0x18)
    0x75, 0x08, 0x15, 0x01, 0x25, 0x03,              // Size 8, Logical 1..3
    0x09, 0x5A, 0xB1, 0x02,                          // Usage (AudibleAlarmControl), Feature
    0x85, 0x21,                                      // Report ID (0x21)
    0x75, 0x18, 0x15, 0x00,                          // Size 24, Logical Min 0
    0x27, 0xFF, 0xFF, 0xFF, 0x00,                    // Logical Max 0xFFFFFF
    0x0B, 0x16, 0x00, 0x86, 0xFF,                    // Usage (APCBattReplaceDate, full 32-bit)
    0xB1, 0x02,                                      // Feature
    0x09, 0x02,                                      // Usage (PresentStatus)
    0xA1, 0x02,                                      // Collection (Logical)
    0x85, 0x16,                                      //   Report ID (0x16)
    0x75, 0x01, 0x95, 0x06, 0x15, 0x00, 0x25, 0x01,  // Size 1, Count 6, Logical 0..1
    0x05, 0x85, 0x09, 0x44, 0x09, 0x45, 0x09, 0xD0,  // Charging, Discharging, ACPresent
    0x09, 0x42, 0x09, 0x4B, 0x09, 0x43,              // BelowRemainingCapacityLimit, NeedReplacement, TimeLimitExpired
    0x81, 0x02,                                      //   Input
    0x95, 0x02, 0x05, 0x84, 0x09, 0x69, 0x09, 0x65,  //   Count 2: ShutdownImminent, Overload
    0x81, 0x02,                                      //   Input
    0xC0,                                            // End Collection
    0xC0,                                            // End Collection (PowerSummary)
    0x09, 0x1A,                                      // Usage (Input)
    0xA1, 0x02,                                      // Collection (Logical)
    0x85, 0x31,                                      //   Report ID (0x31)
    0x75, 0x10, 0x95, 0x01, 0x15, 0x00, 0x25, 0xFF,  //   Size 16, Count 1, Logical 0..0xFF (reads as -1)
    0x09, 0x30, 0x67, 0x21, 0xD1, 0xF0, 0x00, 0x55, 0x07,  // Voltage, Unit (Volt), Exponent 7
    0xB1, 0x02,                                            //   Feature
    0x85, 0x33, 0x26, 0x2C, 0x01,                          //   Report ID (0x33), Logical Max 300
    0x09, 0x54, 0xB1, 0x02,                                //   HighVoltageTransfer, Feature
    0x65, 0x00, 0x55, 0x00,                                //   Unit (None), Unit Exponent (0)
    0xC0,                                                  // End Collection
    0x09, 0x12,                                            // Usage (Battery)
    0xA1, 0x02,                                            // Collection (Logical)
    0x85, 0x52, 0x75, 0x08, 0x15, 0x00, 0x25, 0x07,        //   Report ID (0x52), Size 8, Logical 0..7
    0x09, 0x58, 0xB1, 0x02,                                //   Usage (Test), Feature
    0xC0,                                                  // End Collection
    0x0B, 0x05, 0x00, 0x86, 0xFF,                          // Usage (APCGeneralCollection)
    0xA1, 0x02,                                            // Collection (Logical)
    0x85, 0x41, 0x75, 0x10, 0x15, 0xFF, 0x26, 0xFF, 0x7F,  //   Report ID (0x41), Size 16, Logical -1..32767
    0x0B, 0x7D, 0x00, 0x86, 0xFF, 0xB1, 0x02,              //   APCDelayBeforeShutdown, Feature
    0xC0,                                                  // End Collection
    0xC0,                                                  // End Collection (UPS)
};

class UpsHidParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(parse_report_descriptor(APC_LIKE_DESCRIPTOR.data(), APC_LIKE_DESCRIPTOR.size(), this->map_),
              HID_PARSE_RESULT_OK);
  }
  HidFieldMap map_;
};

TEST_F(UpsHidParserTest, FindsChargeAndRuntimeInBothReportTypes) {
  const HidField &charge = this->map_.feature[UPS_FIELD_BATTERY_CHARGE];
  ASSERT_TRUE(charge.present());
  EXPECT_EQ(charge.report_id, 0x0C);
  EXPECT_EQ(charge.bit_offset, 0);
  EXPECT_EQ(charge.bit_size, 8);
  EXPECT_EQ(charge.report_bytes, 4);  // ID + 8 + 16 bits
  const HidField &runtime = this->map_.feature[UPS_FIELD_BATTERY_RUNTIME];
  ASSERT_TRUE(runtime.present());
  EXPECT_EQ(runtime.bit_offset, 8);
  EXPECT_EQ(runtime.bit_size, 16);
  EXPECT_TRUE(this->map_.input[UPS_FIELD_BATTERY_CHARGE].present());
  EXPECT_TRUE(this->map_.input[UPS_FIELD_BATTERY_RUNTIME].present());
}

TEST_F(UpsHidParserTest, ExtractsValuesFromReport) {
  const uint8_t report[] = {0x0C, 100, 0x58, 0x02};  // 100 %, 600 s
  int32_t value;
  ASSERT_TRUE(this->map_.feature[UPS_FIELD_BATTERY_CHARGE].extract(report, sizeof(report), value));
  EXPECT_EQ(value, 100);
  ASSERT_TRUE(this->map_.feature[UPS_FIELD_BATTERY_RUNTIME].extract(report, sizeof(report), value));
  EXPECT_EQ(value, 600);
  EXPECT_FALSE(this->map_.feature[UPS_FIELD_BATTERY_RUNTIME].extract(report, 2, value));
}

TEST_F(UpsHidParserTest, AppliesVoltUnitExponent) {
  const HidField &voltage = this->map_.feature[UPS_FIELD_BATTERY_VOLTAGE];
  ASSERT_TRUE(voltage.present());
  EXPECT_EQ(voltage.report_id, 0x09);
  EXPECT_NEAR(voltage.to_physical(1350), 13.5f, 0.001f);
}

TEST_F(UpsHidParserTest, StatusBitsFromInputReport) {
  EXPECT_FALSE(this->map_.feature[UPS_FIELD_AC_PRESENT].present());
  const uint8_t report[] = {0x16, 0b00000101};  // Charging, ACPresent
  int32_t value;
  ASSERT_TRUE(this->map_.input[UPS_FIELD_CHARGING].extract(report, sizeof(report), value));
  EXPECT_EQ(value, 1);
  ASSERT_TRUE(this->map_.input[UPS_FIELD_DISCHARGING].extract(report, sizeof(report), value));
  EXPECT_EQ(value, 0);
  ASSERT_TRUE(this->map_.input[UPS_FIELD_AC_PRESENT].extract(report, sizeof(report), value));
  EXPECT_EQ(value, 1);
  EXPECT_EQ(this->map_.input[UPS_FIELD_OVERLOAD].bit_offset, 7);
  EXPECT_EQ(this->map_.max_input_report_bytes, 4);
}

TEST_F(UpsHidParserTest, InputVoltageRangeIsWidened) {
  const HidField &voltage = this->map_.feature[UPS_FIELD_INPUT_VOLTAGE];
  ASSERT_TRUE(voltage.present());
  // 0xFF in one byte is -1, read as 255 like NUT, then widened to twice the high transfer limit
  EXPECT_EQ(voltage.logical_min, 0);
  EXPECT_EQ(voltage.logical_max, 600);
  const uint8_t report[] = {0x31, 0x04, 0x01};  // 260 V
  int32_t value;
  ASSERT_TRUE(voltage.extract(report, sizeof(report), value));
  EXPECT_EQ(value, 260);
  EXPECT_NEAR(voltage.to_physical(value), 260.0f, 0.001f);
}

TEST_F(UpsHidParserTest, VendorUsagesAndCommands) {
  EXPECT_TRUE(this->map_.has_feature(UPS_FIELD_BEEPER));
  EXPECT_TRUE(this->map_.has_feature(UPS_FIELD_TEST));
  EXPECT_TRUE(this->map_.has_feature(UPS_FIELD_DELAY_SHUTDOWN));
  EXPECT_TRUE(this->map_.has_feature(UPS_FIELD_BATTERY_DATE_APC));
  EXPECT_FALSE(this->map_.has_any(UPS_FIELD_PANEL_TEST));
  EXPECT_FALSE(this->map_.has_any(UPS_FIELD_DELAY_REBOOT));
}

TEST_F(UpsHidParserTest, InsertsSignedValueAsAllOnes) {
  const HidField &delay = this->map_.feature[UPS_FIELD_DELAY_SHUTDOWN];
  ASSERT_TRUE(delay.present());
  EXPECT_EQ(delay.report_bytes, 3);
  uint8_t report[3] = {0x41, 0x00, 0x00};
  ASSERT_TRUE(delay.insert(report, sizeof(report), delay.to_logical(-1.0f)));
  EXPECT_EQ(report[1], 0xFF);
  EXPECT_EQ(report[2], 0xFF);
  int32_t value;
  ASSERT_TRUE(delay.extract(report, sizeof(report), value));
  EXPECT_EQ(value, -1);
  ASSERT_TRUE(delay.insert(report, sizeof(report), delay.to_logical(20.0f)));
  EXPECT_EQ(report[1], 20);
  EXPECT_EQ(report[2], 0);
}

TEST_F(UpsHidParserTest, InsertKeepsNeighbouringBits) {
  const HidField &beeper = this->map_.feature[UPS_FIELD_BEEPER];
  uint8_t report[2] = {0x18, 0x02};
  ASSERT_TRUE(beeper.insert(report, sizeof(report), 3));
  EXPECT_EQ(report[1], 0x03);
}

TEST(UpsHidParser, RejectsTruncatedDescriptor) {
  HidFieldMap map;
  const uint8_t truncated[] = {0x05, 0x84, 0x27, 0xFF};
  EXPECT_EQ(parse_report_descriptor(truncated, sizeof(truncated), map), HID_PARSE_RESULT_TRUNCATED);
}

TEST(UpsHidParser, ReportsWithoutIdsHaveNoPrefixByte) {
  // Single unnamed report: Usage Page (Battery System), PowerSummary collection, RemainingCapacity
  const uint8_t desc[] = {0x05, 0x84, 0x09, 0x24, 0xA1, 0x02, 0x75, 0x08, 0x95, 0x01, 0x15,
                          0x00, 0x25, 0x64, 0x05, 0x85, 0x09, 0x66, 0x81, 0x02, 0xC0};
  HidFieldMap map;
  ASSERT_EQ(parse_report_descriptor(desc, sizeof(desc), map), HID_PARSE_RESULT_OK);
  const HidField &charge = map.input[UPS_FIELD_BATTERY_CHARGE];
  ASSERT_TRUE(charge.present());
  EXPECT_EQ(charge.report_id, 0);
  EXPECT_EQ(charge.report_bytes, 1);
  const uint8_t report[] = {87};
  int32_t value;
  ASSERT_TRUE(charge.extract(report, sizeof(report), value));
  EXPECT_EQ(value, 87);
}

// Report IDs and ranges as NUT's cps_fix_report_desc expects them: output voltage with the high transfer
// point's range, input voltage below it, and maximums too small for nominal power and battery voltage.
static const std::vector<uint8_t> CPS_LIKE_DESCRIPTOR = {
    0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,                    // Usage Page (Power Device), UPS, Collection
    0x09, 0x1A, 0xA1, 0x02,                                //   Input, Collection
    0x85, 0x0F, 0x75, 0x10, 0x95, 0x01, 0x15, 0x00,        //     Report ID (0x0F), Size 16, Count 1, Min 0
    0x26, 0xFA, 0x00, 0x09, 0x30, 0xB1, 0x02,              //     Max 250, Voltage, Feature
    0x85, 0x10, 0x26, 0x2C, 0x01, 0x09, 0x54, 0xB1, 0x02,  //     Report ID (0x10), Max 300, HighVoltageTransfer
    0x85, 0x0E, 0x75, 0x08, 0x26, 0xFA, 0x00,              //     Report ID (0x0E), Size 8, Max 250
    0x09, 0x40, 0xB1, 0x02,                                //     ConfigVoltage, Feature
    0xC0,                                                  //   End Collection
    0x09, 0x1C, 0xA1, 0x02,                                //   Output, Collection
    0x85, 0x12, 0x75, 0x10, 0x26, 0x2C, 0x01,              //     Report ID (0x12), Size 16, Max 300
    0x09, 0x30, 0xB1, 0x02,                                //     Voltage, Feature
    0x85, 0x18, 0x26, 0xE8, 0x03, 0x09, 0x44, 0xB1, 0x02,  //     Report ID (0x18), Max 1000, ConfigActivePower
    0x85, 0x14, 0x75, 0x08, 0x25, 0x07,                    //     Report ID (0x14), Size 8, Max 7
    0x09, 0x58, 0xB1, 0x02,                                //     Test, Feature
    0x85, 0x19, 0x75, 0x01, 0x25, 0x01,                    //     Report ID (0x19), Size 1, Max 1
    0x09, 0x65, 0x81, 0x02, 0x75, 0x07, 0x81, 0x03,        //     Overload, Input; 7 bits padding
    0xC0,                                                  //   End Collection
    0x09, 0x24, 0xA1, 0x02,                                //   PowerSummary, Collection
    0x85, 0x0A, 0x75, 0x10, 0x26, 0xFF, 0x00,              //     Report ID (0x0A), Size 16, Max 255
    0x09, 0x30, 0xB1, 0x02,                                //     Voltage, Feature
    0x85, 0x09, 0x75, 0x08, 0x09, 0x40, 0xB1, 0x02,        //     Report ID (0x09), Size 8, ConfigVoltage
    0xC0,                                                  //   End Collection
    0xC0,                                                  // End Collection (UPS)
};

TEST(UpsHidParser, CyberPowerUsages) {
  HidFieldMap map;
  ASSERT_EQ(parse_report_descriptor(CPS_LIKE_DESCRIPTOR.data(), CPS_LIKE_DESCRIPTOR.size(), map, 0x0764, 0x0601),
            HID_PARSE_RESULT_OK);
  EXPECT_EQ(map.feature[UPS_FIELD_TEST].report_id, 0x14);
  EXPECT_EQ(map.input[UPS_FIELD_OVERLOAD].report_id, 0x19);
}

TEST(UpsHidParser, CyberPowerRangeFixes) {
  HidFieldMap map;
  ASSERT_EQ(parse_report_descriptor(CPS_LIKE_DESCRIPTOR.data(), CPS_LIKE_DESCRIPTOR.size(), map, 0x0764, 0x0601),
            HID_PARSE_RESULT_OK);
  EXPECT_EQ(map.feature[UPS_FIELD_OUTPUT_VOLTAGE].logical_max, 511);
  EXPECT_EQ(map.feature[UPS_FIELD_INPUT_VOLTAGE].logical_max, 511);
  EXPECT_EQ(map.feature[UPS_FIELD_INPUT_VOLTAGE_NOMINAL].logical_max, 255);
  EXPECT_EQ(map.feature[UPS_FIELD_REALPOWER_NOMINAL].logical_max, 2048);
  EXPECT_EQ(map.feature[UPS_FIELD_BATTERY_VOLTAGE].logical_max, 4096);
  EXPECT_EQ(map.feature[UPS_FIELD_BATTERY_VOLTAGE_NOMINAL].logical_max, 4096);
  // 480 in 0.1 V units would lose its top bit with the original maximum of 255
  const uint8_t report[] = {0x0A, 0xE0, 0x01};
  int32_t value;
  ASSERT_TRUE(map.feature[UPS_FIELD_BATTERY_VOLTAGE].extract(report, sizeof(report), value));
  EXPECT_EQ(value, 480);
}

TEST(UpsHidParser, CyberPowerFixesNeedCyberPowerIds) {
  HidFieldMap map;
  ASSERT_EQ(parse_report_descriptor(CPS_LIKE_DESCRIPTOR.data(), CPS_LIKE_DESCRIPTOR.size(), map), HID_PARSE_RESULT_OK);
  EXPECT_EQ(map.feature[UPS_FIELD_OUTPUT_VOLTAGE].logical_max, 300);
  EXPECT_EQ(map.feature[UPS_FIELD_REALPOWER_NOMINAL].logical_max, 1000);
  EXPECT_EQ(map.feature[UPS_FIELD_INPUT_VOLTAGE].logical_max, 600);  // the generic APC fix instead
}

TEST(UpsHidParser, CyberPowerAssumedVoltageMaximums) {
  // Input voltage 16 bits and output voltage 8 bits, both with a one-byte Logical Max of 0xFF (reads as -1)
  const uint8_t desc[] = {0x05, 0x84, 0x09, 0x04, 0xA1, 0x01, 0x09, 0x1A, 0xA1, 0x02, 0x85, 0x0F, 0x75,
                          0x10, 0x95, 0x01, 0x15, 0x00, 0x25, 0xFF, 0x09, 0x30, 0xB1, 0x02, 0xC0, 0x09,
                          0x1C, 0xA1, 0x02, 0x85, 0x12, 0x75, 0x08, 0x09, 0x30, 0xB1, 0x02, 0xC0, 0xC0};
  HidFieldMap map;
  ASSERT_EQ(parse_report_descriptor(desc, sizeof(desc), map, 0x0764, 0x0501), HID_PARSE_RESULT_OK);
  EXPECT_TRUE(map.feature[UPS_FIELD_INPUT_VOLTAGE].logical_max_assumed);
  EXPECT_EQ(map.feature[UPS_FIELD_INPUT_VOLTAGE].logical_max, 511);
  EXPECT_EQ(map.feature[UPS_FIELD_OUTPUT_VOLTAGE].logical_max, 255);  // capped to 8 bits
}

// Report descriptor read from a CyberPower CST135UC2 (0764:0601). Most of its values are flagged Constant.
static const std::vector<uint8_t> CST135UC2_DESCRIPTOR = {
    0x05, 0x84, 0x09, 0x04, 0xa1, 0x01, 0x09, 0x24, 0xa1, 0x00, 0x85, 0x01, 0x09, 0xfe, 0x75, 0x08, 0x95, 0x01, 0x15,
    0x00, 0x26, 0xff, 0x00, 0xb1, 0x23, 0x85, 0x02, 0x09, 0xff, 0xb1, 0x23, 0x85, 0x1b, 0x06, 0x01, 0xff, 0x09, 0xd0,
    0xb1, 0x22, 0x85, 0x03, 0x05, 0x85, 0x09, 0x89, 0xb1, 0x23, 0x85, 0x04, 0x09, 0x8f, 0xb1, 0x23, 0x85, 0x05, 0x09,
    0x8b, 0xb1, 0x23, 0x85, 0x06, 0x09, 0x2c, 0xb1, 0x23, 0x85, 0x07, 0x75, 0x08, 0x95, 0x06, 0x25, 0x64, 0x09, 0x83,
    0x09, 0x8d, 0x09, 0x8e, 0x09, 0x8c, 0x09, 0x29, 0x09, 0x67, 0xb1, 0x23, 0x85, 0x08, 0x75, 0x08, 0x95, 0x01, 0x65,
    0x00, 0x09, 0x66, 0x81, 0x23, 0x09, 0x66, 0xb1, 0xa3, 0x09, 0x68, 0x75, 0x10, 0x27, 0xff, 0xff, 0x00, 0x00, 0x66,
    0x01, 0x10, 0x81, 0x23, 0x09, 0x68, 0xb1, 0xa3, 0x09, 0x2a, 0x26, 0xe0, 0x01, 0x81, 0x23, 0x09, 0x2a, 0xb1, 0xa2,
    0x85, 0x09, 0x75, 0x10, 0x26, 0xe0, 0x01, 0x05, 0x84, 0x09, 0x40, 0x67, 0x21, 0xd1, 0xf0, 0x00, 0x55, 0x06, 0xb1,
    0x23, 0x85, 0x0a, 0x09, 0x30, 0xb1, 0xa3, 0x09, 0x02, 0xa1, 0x02, 0x65, 0x00, 0x55, 0x00, 0x85, 0x0b, 0x75, 0x01,
    0x95, 0x06, 0x25, 0x01, 0x05, 0x85, 0x09, 0xd0, 0x09, 0x44, 0x09, 0x45, 0x09, 0x42, 0x09, 0x46, 0x09, 0x43, 0x81,
    0x23, 0x09, 0xd0, 0x09, 0x44, 0x09, 0x45, 0x09, 0x42, 0x09, 0x46, 0x09, 0x43, 0xb1, 0xa3, 0x75, 0x02, 0x95, 0x01,
    0x81, 0x01, 0xb1, 0x01, 0xc0, 0x85, 0x0c, 0x05, 0x84, 0x09, 0x5a, 0x75, 0x08, 0x15, 0x01, 0x25, 0x03, 0xb1, 0xa2,
    0x09, 0x5a, 0x81, 0x22, 0x85, 0x0d, 0x09, 0xfd, 0x15, 0x00, 0x26, 0xff, 0x00, 0xb1, 0x23, 0xc0, 0x05, 0x84, 0x09,
    0x1a, 0xa1, 0x00, 0x85, 0x0e, 0x05, 0x84, 0x09, 0x40, 0x75, 0x08, 0x67, 0x21, 0xd1, 0xf0, 0x00, 0x55, 0x07, 0xb1,
    0x23, 0x85, 0x0f, 0x75, 0x10, 0x09, 0x30, 0xb1, 0xa3, 0xc0, 0x09, 0x1c, 0xa1, 0x00, 0x85, 0x10, 0x09, 0x53, 0x15,
    0x61, 0x25, 0x67, 0xb1, 0xa2, 0x09, 0x53, 0x81, 0x23, 0x09, 0x54, 0x16, 0x87, 0x00, 0x26, 0x90, 0x00, 0xb1, 0xa2,
    0x09, 0x54, 0x81, 0x23, 0x15, 0x00, 0x26, 0xff, 0x01, 0x85, 0x12, 0x09, 0x30, 0xb1, 0xa3, 0x75, 0x08, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x65, 0x00, 0x55, 0x00, 0x85, 0x13, 0x09, 0x35, 0xb1, 0xa3, 0x85, 0x14, 0x09, 0x58, 0x25, 0x06,
    0xb1, 0xa2, 0x09, 0x58, 0x81, 0x22, 0x85, 0x15, 0x09, 0x57, 0x75, 0x10, 0x15, 0xff, 0x26, 0xff, 0x7f, 0x35, 0xc4,
    0x47, 0xc4, 0xff, 0x1d, 0x00, 0x66, 0x01, 0x10, 0xb1, 0xa2, 0x85, 0x16, 0x09, 0x56, 0xb1, 0xa2, 0x85, 0x17, 0x09,
    0x6e, 0x75, 0x01, 0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0xb1, 0xa3, 0x09, 0x65, 0xb1, 0xa3,
    0x75, 0x06, 0xb1, 0x01, 0x85, 0x18, 0x95, 0x02, 0x75, 0x10, 0x09, 0x44, 0x26, 0xd0, 0x07, 0x66, 0x21, 0xd1, 0x55,
    0x07, 0x09, 0x43, 0xb1, 0xa3, 0x85, 0x19, 0x95, 0x01, 0x09, 0x34, 0x26, 0xd0, 0x07, 0xb1, 0xa3, 0x09, 0x34, 0x81,
    0x23, 0x85, 0x1d, 0x09, 0x33, 0x26, 0xd0, 0x07, 0xb1, 0xa3, 0x09, 0x33, 0x81, 0x23, 0x85, 0x1a, 0x06, 0x01, 0xff,
    0x65, 0x00, 0x55, 0x00, 0x75, 0x08, 0x15, 0x00, 0x25, 0x02, 0x09, 0x43, 0xb1, 0xa2, 0x09, 0x43, 0x81, 0x23, 0xc0,
    0x85, 0x1c, 0x09, 0xba, 0xa1, 0x02, 0x75, 0x10, 0x27, 0xff, 0xff, 0x00, 0x00, 0x09, 0xbb, 0xb1, 0x22, 0x75, 0x08,
    0x26, 0xff, 0x00, 0x09, 0xbc, 0xb1, 0x22, 0x75, 0x10, 0x27, 0xff, 0xff, 0x00, 0x00, 0x09, 0xbd, 0xb2, 0xa2, 0x01,
    0xc0, 0x06, 0x01, 0xff, 0x09, 0x1d, 0xa1, 0x00, 0x09, 0x19, 0xa1, 0x02, 0x85, 0x25, 0x09, 0x20, 0x65, 0x00, 0x55,
    0x00, 0x15, 0x00, 0x25, 0x01, 0x95, 0x01, 0x75, 0x01, 0xb1, 0x82, 0x75, 0x07, 0xb1, 0x03, 0x85, 0x2c, 0x09, 0x21,
    0x75, 0x01, 0xb1, 0x83, 0x09, 0x21, 0x81, 0x03, 0x75, 0x07, 0xb1, 0x83, 0x81, 0x03, 0xc0, 0x09, 0x1a, 0xa1, 0x02,
    0x85, 0x26, 0x09, 0x01, 0x75, 0x01, 0xb1, 0x82, 0x75, 0x07, 0xb1, 0x83, 0x85, 0x27, 0x09, 0x02, 0x25, 0x03, 0x75,
    0x08, 0xb1, 0x03, 0xc0, 0x09, 0x1b, 0xa1, 0x02, 0x85, 0x28, 0x09, 0x16, 0x65, 0x00, 0x55, 0x00, 0x15, 0x00, 0x25,
    0x3e, 0x75, 0x08, 0x95, 0x01, 0xb1, 0x83, 0x09, 0x16, 0x81, 0x03, 0x09, 0x18, 0x26, 0xff, 0x00, 0x95, 0x3e, 0xb2,
    0x83, 0x01, 0x09, 0x18, 0x82, 0x03, 0x01, 0x85, 0x29, 0x09, 0x15, 0x15, 0x01, 0x25, 0x3e, 0x95, 0x01, 0xb1, 0x02,
    0x09, 0x15, 0x91, 0x02, 0x09, 0x17, 0x15, 0x00, 0x26, 0xff, 0x00, 0x95, 0x3e, 0xb2, 0x02, 0x01, 0x09, 0x17, 0x92,
    0x02, 0x01, 0x85, 0x2d, 0x75, 0x01, 0x95, 0x03, 0x25, 0x01, 0x09, 0x10, 0x09, 0x1e, 0x09, 0x1f, 0xb1, 0x83, 0x09,
    0x10, 0x09, 0x1e, 0x09, 0x1f, 0x81, 0x03, 0x75, 0x05, 0x95, 0x01, 0xb1, 0x83, 0x81, 0x03, 0x85, 0x2a, 0x09, 0x13,
    0x75, 0x01, 0xb1, 0x82, 0x75, 0x07, 0xb1, 0x83, 0x85, 0x2b, 0x09, 0x14, 0x75, 0x01, 0xb1, 0x82, 0x75, 0x07, 0xb1,
    0x83, 0xc0, 0xc0, 0xc0,
};

TEST(UpsHidParser, CyberPowerCst135uc2) {
  HidFieldMap map;
  ASSERT_EQ(CST135UC2_DESCRIPTOR.size(), 726u);
  ASSERT_EQ(parse_report_descriptor(CST135UC2_DESCRIPTOR.data(), CST135UC2_DESCRIPTOR.size(), map, 0x0764, 0x0601),
            HID_PARSE_RESULT_OK);
  EXPECT_EQ(map.input[UPS_FIELD_BATTERY_CHARGE].report_id, 0x08);
  EXPECT_EQ(map.input[UPS_FIELD_BATTERY_RUNTIME].bit_offset, 8);
  EXPECT_EQ(map.input[UPS_FIELD_AC_PRESENT].report_id, 0x0B);
  EXPECT_EQ(map.input[UPS_FIELD_DISCHARGING].bit_offset, 2);
  EXPECT_EQ(map.feature[UPS_FIELD_INPUT_VOLTAGE].report_id, 0x0F);
  EXPECT_EQ(map.feature[UPS_FIELD_OUTPUT_VOLTAGE].report_id, 0x12);
  EXPECT_EQ(map.feature[UPS_FIELD_LOAD].report_id, 0x13);
  EXPECT_EQ(map.feature[UPS_FIELD_OVERLOAD].report_id, 0x17);
  EXPECT_EQ(map.feature[UPS_FIELD_TEST].report_id, 0x14);
  const HidField &battery = map.feature[UPS_FIELD_BATTERY_VOLTAGE];
  EXPECT_EQ(battery.report_id, 0x0A);
  EXPECT_NEAR(battery.to_physical(137), 13.7f, 0.001f);  // 0.1 V units
  // The shutdown timer counts minutes; a cancel must stay -1 rather than becoming 0 (shut down now)
  const HidField &delay = map.feature[UPS_FIELD_DELAY_SHUTDOWN];
  EXPECT_EQ(delay.report_id, 0x15);
  EXPECT_EQ(delay.to_logical(-1), -1);
  EXPECT_EQ(delay.to_logical(60), 1);
  EXPECT_EQ(delay.to_logical(20), 0);
  EXPECT_NEAR(delay.to_physical(2), 120.0f, 0.001f);
}
TEST(UpsState, ClampsBatteryCharge) {
  UpsState state;
  state.set_value(UPS_FIELD_BATTERY_CHARGE, 104);
  EXPECT_EQ(state.get(UPS_FIELD_BATTERY_CHARGE), 100.0f);
}

TEST(UpsState, ParsesApcProductString) {
  UpsState state;
  state.set_identity(0x051D, 0x0002, "American Power Conversion", "Back-UPS ES 600M1 FW:928.a5 .D USB FW:a5",
                     "4B1234P56789  ");
  EXPECT_STREQ(state.get_model(), "Back-UPS ES 600M1");
  EXPECT_STREQ(state.get_firmware(), "928.a5 .D");
  EXPECT_STREQ(state.get_serial(), "4B1234P56789");
  char buf[UPS_VALUE_BUFFER_SIZE];
  ASSERT_TRUE(state.format_var("ups.firmware.aux", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "a5");
  ASSERT_TRUE(state.format_var("ups.vendorid", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "051d");
}

TEST(UpsState, ProductWithoutFirmware) {
  UpsState state;
  state.set_identity(0x0764, 0x0501, "CPS", "CP1500PFCLCD", "");
  EXPECT_STREQ(state.get_model(), "CP1500PFCLCD");
  EXPECT_STREQ(state.get_firmware(), "");
  char buf[UPS_VALUE_BUFFER_SIZE];
  EXPECT_FALSE(state.format_var("ups.serial", buf, sizeof(buf)));
}

TEST(UpsState, StatusFlags) {
  UpsState state;
  char buf[UPS_VALUE_BUFFER_SIZE];
  state.format_status(buf, sizeof(buf));
  EXPECT_STREQ(buf, "");
  state.set_value(UPS_FIELD_AC_PRESENT, 1);
  state.set_value(UPS_FIELD_CHARGING, 1);
  state.format_status(buf, sizeof(buf));
  EXPECT_STREQ(buf, "OL CHRG");
  state.set_value(UPS_FIELD_AC_PRESENT, 0);
  state.set_value(UPS_FIELD_CHARGING, 0);
  state.set_value(UPS_FIELD_DISCHARGING, 1);
  state.set_value(UPS_FIELD_SHUTDOWN_IMMINENT, 1);
  state.set_value(UPS_FIELD_NEED_REPLACEMENT, 1);
  state.format_status(buf, sizeof(buf));
  EXPECT_STREQ(buf, "OB DISCHRG LB RB");
}

TEST(UpsState, FormatsValues) {
  UpsState state;
  state.set_value(UPS_FIELD_BATTERY_CHARGE, 99.6f);
  state.set_value(UPS_FIELD_BATTERY_VOLTAGE, 13.54f);
  state.set_value(UPS_FIELD_BEEPER, 2);
  state.set_value(UPS_FIELD_TEST, 1);
  state.set_value(UPS_FIELD_BATTERY_DATE_APC, 0x102202);
  char buf[UPS_VALUE_BUFFER_SIZE];
  ASSERT_TRUE(state.format_var("battery.charge", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "100");
  ASSERT_TRUE(state.format_var("battery.voltage", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "13.5");
  ASSERT_TRUE(state.format_var("ups.beeper.status", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "enabled");
  ASSERT_TRUE(state.format_var("ups.test.result", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Done and passed");
  ASSERT_TRUE(state.format_var("battery.mfr.date", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "2002/10/22");
  EXPECT_FALSE(state.format_var("input.voltage", buf, sizeof(buf)));
  EXPECT_FALSE(state.format_var("no.such.var", buf, sizeof(buf)));
}

TEST(UpsState, HidStandardDate) {
  UpsState state;
  // 2023-07-14: (43 << 9) | (7 << 5) | 14
  state.set_value(UPS_FIELD_BATTERY_DATE, static_cast<float>((43 << 9) | (7 << 5) | 14));
  char buf[UPS_VALUE_BUFFER_SIZE];
  ASSERT_TRUE(state.format_var("battery.mfr.date", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "2023/07/14");
}

TEST(UpsState, CommandsFollowFeatureFields) {
  HidFieldMap map;
  ASSERT_EQ(parse_report_descriptor(APC_LIKE_DESCRIPTOR.data(), APC_LIKE_DESCRIPTOR.size(), map), HID_PARSE_RESULT_OK);
  UpsState state;
  state.set_field_map(map);
  EXPECT_TRUE(state.command_supported(UPS_COMMAND_BEEPER_MUTE));
  EXPECT_TRUE(state.command_supported(UPS_COMMAND_TEST_BATTERY_START_QUICK));
  EXPECT_TRUE(state.command_supported(UPS_COMMAND_LOAD_OFF_DELAY));
  EXPECT_TRUE(state.command_supported(UPS_COMMAND_SHUTDOWN_STOP));
  EXPECT_FALSE(state.command_supported(UPS_COMMAND_SHUTDOWN_REBOOT));
  EXPECT_FALSE(state.command_supported(UPS_COMMAND_TEST_PANEL_START));
  char buf[UPS_VALUE_BUFFER_SIZE];
  ASSERT_TRUE(state.format_var("ups.delay.shutdown", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "20");
  state.reset();
  EXPECT_FALSE(state.command_supported(UPS_COMMAND_BEEPER_MUTE));
}

TEST(UpsState, FindCommand) {
  UpsCommand command;
  ASSERT_TRUE(find_command("test.battery.start.deep", command));
  EXPECT_EQ(command, UPS_COMMAND_TEST_BATTERY_START_DEEP);
  EXPECT_FALSE(find_command("test.battery.start", command));
}

}  // namespace esphome::ups_hid::testing
