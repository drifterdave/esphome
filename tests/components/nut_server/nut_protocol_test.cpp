#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "esphome/components/nut_server/nut_protocol.h"

namespace esphome::nut_server::testing {

using ups_hid::UpsCommand;
using ups_hid::UpsState;

class StringWriter final : public NutWriter {
 public:
  void write(const char *data, size_t len) override { this->text.append(data, len); }
  std::string text;
};

class FakeDevice final : public ups_hid::UpsDevice {
 public:
  const UpsState &get_state() const override { return this->state; }
  bool run_command(UpsCommand command) override {
    this->commands.push_back(command);
    return true;
  }
  UpsState state;
  std::vector<UpsCommand> commands;
};

class FakeHost final : public NutHost {
 public:
  const NutConfig &get_nut_config() const override { return this->config; }
  ups_hid::UpsDevice *get_ups_device() override { return &this->device; }
  size_t get_session_count() const override { return this->sessions.size(); }
  const NutSession *get_session(size_t index) const override { return &this->sessions[index]; }

  NutConfig config;
  FakeDevice device;
  std::array<NutSession, 2> sessions;
};

class NutProtocolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->host_.config.ups_name = "apc";
    this->host_.config.description = "Back \"room\"";
    this->host_.config.username = "admin";
    this->host_.config.password = "s3cret pass";
    this->host_.sessions[0].begin(&this->host_, "192.168.1.20");
    this->host_.sessions[1].begin(&this->host_, "192.168.1.21");
  }

  void connect_ups() {
    // Beeper (feature) and AC present (input) known, as after reading a real descriptor
    ups_hid::HidFieldMap map;
    map.feature[ups_hid::UPS_FIELD_BEEPER].bit_size = 8;
    map.feature[ups_hid::UPS_FIELD_TEST].bit_size = 8;
    UpsState &state = this->host_.device.state;
    state.set_field_map(map);
    state.set_identity(0x051D, 0x0002, "American Power Conversion", "Back-UPS ES 600M1 FW:928.a5 .D USB FW:a5",
                       "4B1234P56789");
    state.set_value(ups_hid::UPS_FIELD_BATTERY_CHARGE, 100);
    state.set_value(ups_hid::UPS_FIELD_AC_PRESENT, 1);
    state.set_connected(true);
  }

  /// Send one line on session 0 (or the given one) and return the reply.
  std::string send(const char *line, size_t session = 0) {
    StringWriter out;
    char buf[NUT_LINE_MAX];
    snprintf(buf, sizeof(buf), "%s", line);
    this->keep_open_ = this->host_.sessions[session].handle_line(buf, out);
    return out.text;
  }

  FakeHost host_;
  bool keep_open_{true};
};

TEST_F(NutProtocolTest, VersionCommands) {
  EXPECT_EQ(this->send("NETVER"), "1.3\n");
  EXPECT_EQ(this->send("VER").rfind("Network UPS Tools upsd ", 0), 0u);
  EXPECT_EQ(this->send("STARTTLS"), "ERR FEATURE-NOT-CONFIGURED\n");
  EXPECT_EQ(this->send("BOGUS"), "ERR UNKNOWN-COMMAND\n");
  EXPECT_EQ(this->send(""), "");
}

TEST_F(NutProtocolTest, ListUpsEscapesDescription) {
  EXPECT_EQ(this->send("LIST UPS"), "BEGIN LIST UPS\nUPS apc \"Back \\\"room\\\"\"\nEND LIST UPS\n");
}

TEST_F(NutProtocolTest, DataStaleBeforeConnect) {
  EXPECT_EQ(this->send("LIST VAR apc"), "ERR DATA-STALE\n");
  EXPECT_EQ(this->send("GET VAR apc battery.charge"), "ERR DATA-STALE\n");
}

TEST_F(NutProtocolTest, GetVar) {
  this->connect_ups();
  EXPECT_EQ(this->send("GET VAR apc battery.charge"), "VAR apc battery.charge \"100\"\n");
  EXPECT_EQ(this->send("get var apc ups.status"), "VAR apc ups.status \"OL\"\n");
  EXPECT_EQ(this->send("GET VAR apc ups.model"), "VAR apc ups.model \"Back-UPS ES 600M1\"\n");
  EXPECT_EQ(this->send("GET VAR apc input.voltage"), "ERR VAR-NOT-SUPPORTED\n");
  EXPECT_EQ(this->send("GET VAR other battery.charge"), "ERR UNKNOWN-UPS\n");
  EXPECT_EQ(this->send("GET VAR apc"), "ERR INVALID-ARGUMENT\n");
}

TEST_F(NutProtocolTest, ListVar) {
  this->connect_ups();
  std::string reply = this->send("LIST VAR apc");
  EXPECT_EQ(reply.rfind("BEGIN LIST VAR apc\n", 0), 0u);
  EXPECT_NE(reply.find("VAR apc battery.charge \"100\"\n"), std::string::npos);
  EXPECT_NE(reply.find("VAR apc device.mfr \"American Power Conversion\"\n"), std::string::npos);
  EXPECT_NE(reply.find("VAR apc ups.firmware \"928.a5 .D\"\n"), std::string::npos);
  EXPECT_NE(reply.find("VAR apc ups.status \"OL\"\n"), std::string::npos);
  EXPECT_EQ(reply.find("input.voltage"), std::string::npos);
  EXPECT_EQ(reply.substr(reply.size() - 17), "END LIST VAR apc\n");
}

TEST_F(NutProtocolTest, ListCmdOnlySupported) {
  this->connect_ups();
  std::string reply = this->send("LIST CMD apc");
  EXPECT_NE(reply.find("CMD apc beeper.mute\n"), std::string::npos);
  EXPECT_NE(reply.find("CMD apc test.battery.start.quick\n"), std::string::npos);
  EXPECT_EQ(reply.find("load.off.delay"), std::string::npos);
  EXPECT_EQ(this->send("GET CMDDESC apc beeper.mute"), "CMDDESC apc beeper.mute \"Temporarily mute the UPS beeper\"\n");
  EXPECT_EQ(this->send("GET CMDDESC apc nope"), "ERR CMD-NOT-SUPPORTED\n");
}

TEST_F(NutProtocolTest, MetadataQueries) {
  this->connect_ups();
  EXPECT_EQ(this->send("GET TYPE apc battery.charge"), "TYPE apc battery.charge NUMBER\n");
  EXPECT_EQ(this->send("GET TYPE apc ups.status"), "TYPE apc ups.status STRING:64\n");
  EXPECT_EQ(this->send("GET DESC apc ups.load"), "DESC apc ups.load \"Description unavailable\"\n");
  EXPECT_EQ(this->send("GET UPSDESC apc"), "UPSDESC apc \"Back \\\"room\\\"\"\n");
  EXPECT_EQ(this->send("LIST RW apc"), "BEGIN LIST RW apc\nEND LIST RW apc\n");
  EXPECT_EQ(this->send("LIST ENUM apc ups.status"), "BEGIN LIST ENUM apc ups.status\nEND LIST ENUM apc ups.status\n");
  EXPECT_EQ(this->send("SET VAR apc ups.status OB"), "ERR READONLY\n");
}

TEST_F(NutProtocolTest, InstcmdNeedsCredentials) {
  this->connect_ups();
  EXPECT_EQ(this->send("INSTCMD apc beeper.mute"), "ERR USERNAME-REQUIRED\n");
  EXPECT_EQ(this->send("USERNAME admin"), "OK\n");
  EXPECT_EQ(this->send("USERNAME admin"), "ERR ALREADY-SET-USERNAME\n");
  EXPECT_EQ(this->send("INSTCMD apc beeper.mute"), "ERR PASSWORD-REQUIRED\n");
  EXPECT_EQ(this->send("PASSWORD wrong"), "OK\n");
  EXPECT_EQ(this->send("INSTCMD apc beeper.mute"), "ERR ACCESS-DENIED\n");
  EXPECT_TRUE(this->host_.device.commands.empty());
}

TEST_F(NutProtocolTest, InstcmdRuns) {
  this->connect_ups();
  EXPECT_EQ(this->send("USERNAME admin"), "OK\n");
  EXPECT_EQ(this->send("PASSWORD \"s3cret pass\""), "OK\n");
  EXPECT_EQ(this->send("INSTCMD apc test.battery.start.quick"), "OK\n");
  EXPECT_EQ(this->send("INSTCMD apc load.off.delay"), "ERR CMD-NOT-SUPPORTED\n");
  EXPECT_EQ(this->send("INSTCMD apc made.up"), "ERR CMD-NOT-SUPPORTED\n");
  EXPECT_EQ(this->send("INSTCMD other beeper.mute"), "ERR UNKNOWN-UPS\n");
  ASSERT_EQ(this->host_.device.commands.size(), 1u);
  EXPECT_EQ(this->host_.device.commands[0], ups_hid::UPS_COMMAND_TEST_BATTERY_START_QUICK);
}

TEST_F(NutProtocolTest, InstcmdDisabledWithoutConfiguredUser) {
  this->connect_ups();
  this->host_.config.username = nullptr;
  this->host_.config.password = nullptr;
  this->send("USERNAME admin");
  this->send("PASSWORD x");
  EXPECT_EQ(this->send("INSTCMD apc beeper.mute"), "ERR ACCESS-DENIED\n");
}

TEST_F(NutProtocolTest, LoginAndClients) {
  this->connect_ups();
  EXPECT_EQ(this->send("GET NUMLOGINS apc"), "NUMLOGINS apc 0\n");
  EXPECT_EQ(this->send("LOGIN apc"), "ERR USERNAME-REQUIRED\n");
  this->send("USERNAME admin");
  this->send("PASSWORD \"s3cret pass\"");
  EXPECT_EQ(this->send("LOGIN apc"), "OK\n");
  EXPECT_EQ(this->send("LOGIN apc"), "ERR ALREADY-LOGGED-IN\n");
  EXPECT_EQ(this->send("GET NUMLOGINS apc", 1), "NUMLOGINS apc 1\n");
  EXPECT_EQ(this->send("LIST CLIENT apc", 1), "BEGIN LIST CLIENT apc\nCLIENT apc 192.168.1.20\nEND LIST CLIENT apc\n");
  EXPECT_EQ(this->send("FSD apc"), "ERR ACCESS-DENIED\n");
}

TEST_F(NutProtocolTest, LogoutClosesSession) {
  EXPECT_EQ(this->send("LOGOUT"), "OK Goodbye\n");
  EXPECT_FALSE(this->keep_open_);
}

TEST(NutSplitArgs, QuotesAndEscapes) {
  char line[] = "PASSWORD \"a \\\"b\\\\ c\"  x\\ y";
  char *argv[4];
  ASSERT_EQ(split_args(line, argv, 4), 3);
  EXPECT_STREQ(argv[0], "PASSWORD");
  EXPECT_STREQ(argv[1], "a \"b\\ c");
  EXPECT_STREQ(argv[2], "x y");
}

TEST(NutSplitArgs, Errors) {
  char unterminated[] = "SET VAR \"abc";
  char *argv[4];
  EXPECT_EQ(split_args(unterminated, argv, 4), -1);
  char too_many[] = "a b c d e";
  EXPECT_EQ(split_args(too_many, argv, 4), -1);
  char blank[] = "   ";
  EXPECT_EQ(split_args(blank, argv, 4), 0);
}

}  // namespace esphome::nut_server::testing
