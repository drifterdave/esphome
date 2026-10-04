#pragma once

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)
#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/event_pool.h"
#include "esphome/core/lock_free_queue.h"
#include "esphome/components/usb_host/usb_host.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#include "hid_report_parser.h"
#include "ups_data.h"

#include <array>
#include <atomic>

namespace esphome::ups_hid {

/// Largest report descriptor accepted; APC Back-UPS descriptors are about 1 KB.
static constexpr size_t REPORT_DESCRIPTOR_MAX = 2048;
/// Largest single report read or written.
static constexpr size_t REPORT_MAX = 64;
static constexpr uint8_t INPUT_QUEUE_SIZE = 8;
static constexpr uint8_t COMMAND_QUEUE_SIZE = 4;

struct InputReport {
  uint8_t data[REPORT_MAX];
  uint8_t len;
  void release() {}
};

enum UpsHidStage : uint8_t {
  UPS_HID_STAGE_DISCONNECTED,
  UPS_HID_STAGE_CONFIGURING,
  UPS_HID_STAGE_RUNNING,
  UPS_HID_STAGE_FAILED,
};

enum ControlOp : uint8_t {
  CONTROL_OP_NONE,
  CONTROL_OP_SET_IDLE,
  CONTROL_OP_GET_DESCRIPTOR,
  CONTROL_OP_POLL,
  CONTROL_OP_COMMAND_READ,
  CONTROL_OP_COMMAND_WRITE,
};

struct PollEntry {
  uint16_t bytes;
  HidReportType type;
  uint8_t id;
};

/// A USB HID Power Device (UPS) attached to the USB host port.
class UpsHid : public usb_host::USBClient, public UpsDevice {
#ifdef USE_SENSOR
  SUB_SENSOR(battery_level)
  SUB_SENSOR(battery_voltage)
  SUB_SENSOR(runtime)
  SUB_SENSOR(input_voltage)
  SUB_SENSOR(output_voltage)
  SUB_SENSOR(load)
#endif
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(online)
  SUB_BINARY_SENSOR(charging)
  SUB_BINARY_SENSOR(low_battery)
  SUB_BINARY_SENSOR(replace_battery)
  SUB_BINARY_SENSOR(overload)
  SUB_BINARY_SENSOR(connected)
#endif
#ifdef USE_TEXT_SENSOR
  SUB_TEXT_SENSOR(status)
  SUB_TEXT_SENSOR(model)
  SUB_TEXT_SENSOR(serial)
  SUB_TEXT_SENSOR(test_result)
#endif

 public:
  UpsHid(uint16_t vid, uint16_t pid) : USBClient(vid, pid) {}
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_update_interval(uint32_t update_interval) { this->update_interval_ = update_interval; }
  void set_shutdown_delay(uint32_t seconds) { this->ups_.set_shutdown_delay(seconds); }

  const UpsState &get_state() const override { return this->ups_; }
  bool run_command(UpsCommand command) override;

 protected:
  void on_connected() override;
  void on_disconnected() override;

  bool find_hid_interface_();
  void read_identity_();
  bool submit_control_(ControlOp op, uint8_t type, uint8_t request, uint16_t value, uint16_t length);
  bool get_report_(ControlOp op, HidReportType type, uint8_t id, uint16_t length);
  void handle_control_done_();
  void on_descriptor_read_(const uint8_t *data, size_t len);
  void on_command_report_read_(bool success, const uint8_t *data, size_t len);
  bool start_next_control_();
  void start_input_();
  void build_poll_list_();
  void decode_report_(HidReportType type, const uint8_t *data, size_t len);
  void publish_entities_();
  void log_fields_() const;

  static void control_callback(usb_transfer_t *transfer);

  HidFieldMap fields_{};
  UpsState ups_{};

  // Dedicated control transfer, large enough for the report descriptor. Only one is in flight at a time.
  usb_transfer_t *control_{nullptr};
  std::atomic<bool> control_done_{false};
  ControlOp control_op_{CONTROL_OP_NONE};

  LockFreeQueue<InputReport, INPUT_QUEUE_SIZE> input_queue_;
  EventPool<InputReport, INPUT_QUEUE_SIZE - 1> input_pool_;
  std::atomic<bool> input_active_{false};
  usb_host::transfer_cb_t input_callback_;

  std::array<PollEntry, UPS_FIELD_COUNT> poll_list_{};
  std::array<UpsCommand, COMMAND_QUEUE_SIZE> commands_{};
  UpsCommand active_command_{};

  uint32_t update_interval_{10000};
  uint16_t report_descriptor_length_{0};
  uint16_t input_length_{0};
  uint8_t interface_number_{0};
  uint8_t input_endpoint_{0};
  uint8_t poll_count_{0};
  uint8_t poll_index_{0};
  uint8_t command_count_{0};
  UpsHidStage stage_{UPS_HID_STAGE_DISCONNECTED};
  bool poll_pending_{false};
  bool publish_pending_{false};
};

}  // namespace esphome::ups_hid

#endif
