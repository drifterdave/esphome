#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)
#include "ups_hid.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>

namespace esphome::ups_hid {

static const char *const TAG = "ups_hid";

using usb_host::SETUP_PACKET_SIZE;

static constexpr uint8_t DESCRIPTOR_TYPE_HID = 0x21;
static constexpr uint8_t DESCRIPTOR_TYPE_REPORT = 0x22;
static constexpr uint8_t HID_REQUEST_GET_REPORT = 0x01;
static constexpr uint8_t HID_REQUEST_SET_REPORT = 0x09;
static constexpr uint8_t HID_REQUEST_SET_IDLE = 0x0A;
static constexpr uint8_t REQUEST_GET_DESCRIPTOR = 0x06;
static constexpr uint8_t CLASS_IN = usb_host::USB_DIR_IN | usb_host::USB_TYPE_CLASS | usb_host::USB_RECIP_INTERFACE;
static constexpr uint8_t CLASS_OUT = usb_host::USB_DIR_OUT | usb_host::USB_TYPE_CLASS | usb_host::USB_RECIP_INTERFACE;
static constexpr uint8_t STANDARD_IN =
    usb_host::USB_DIR_IN | usb_host::USB_TYPE_STANDARD | usb_host::USB_RECIP_INTERFACE;

static constexpr size_t IDENTITY_STRING_SIZE = 64;

/// Convert a USB string descriptor (UTF-16LE) to ASCII, replacing other characters with '?'.
static void descriptor_to_ascii(const usb_str_desc_t *desc, char *buf, size_t len) {
  size_t pos = 0;
  if (desc != nullptr && desc->bLength >= 2) {
    size_t chars = (desc->bLength - 2) / 2;
    for (size_t i = 0; i != chars && pos + 1 < len; i++) {
      uint16_t c = desc->wData[i];
      buf[pos++] = c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
    }
  }
  buf[pos] = '\0';
}

void UpsHid::setup() {
  USBClient::setup();
  if (this->is_failed())
    return;
  if (usb_host_transfer_alloc(SETUP_PACKET_SIZE + REPORT_DESCRIPTOR_MAX, 0, &this->control_) != ESP_OK) {
    ESP_LOGE(TAG, "Control transfer allocation failed");
    this->mark_failed();
    return;
  }
  // CALLBACK CONTEXT: USB task. Queues the report for the main loop and re-arms the transfer.
  this->input_callback_ = [this](const usb_host::TransferStatus &status) {
    if (!this->input_active_.load())
      return;
    if (!status.success) {
      this->input_active_.store(false);
      return;
    }
    if (status.data_len != 0) {
      InputReport *report = this->input_pool_.allocate();
      if (report != nullptr) {
        report->len = static_cast<uint8_t>(std::min<size_t>(status.data_len, REPORT_MAX));
        memcpy(report->data, status.data, report->len);
        this->input_queue_.push(report);
        this->enable_loop_soon_any_context();
        App.wake_loop_threadsafe();
      }
    }
    if (!this->transfer_in(this->input_endpoint_, this->input_callback_, this->input_length_))
      this->input_active_.store(false);
  };
  if (!this->input_pool_.warm()) {
    ESP_LOGE(TAG, "Input report pool allocation failed");
    this->mark_failed();
    return;
  }
  this->set_interval(this->update_interval_, [this]() {
    if (this->stage_ != UPS_HID_STAGE_RUNNING)
      return;
    this->poll_index_ = 0;
    this->poll_pending_ = true;
    this->enable_loop();
  });
}

void UpsHid::loop() {
  bool had_work = this->process_usb_events_();
  if (this->control_done_.exchange(false, std::memory_order_acquire)) {
    this->handle_control_done_();
    had_work = true;
  }
  InputReport *report;
  while ((report = this->input_queue_.pop()) != nullptr) {
    this->decode_report_(HID_REPORT_TYPE_INPUT, report->data, report->len);
    this->input_pool_.release(report);
    this->publish_pending_ = true;
    had_work = true;
  }
  if (this->stage_ == UPS_HID_STAGE_RUNNING && this->control_op_ == CONTROL_OP_NONE)
    had_work |= this->start_next_control_();
  if (this->publish_pending_ && this->control_op_ == CONTROL_OP_NONE) {
    this->publish_pending_ = false;
    this->publish_entities_();
  }
  if (!had_work)
    this->disable_loop();
}

void UpsHid::dump_config() {
  ESP_LOGCONFIG(TAG,
                "UPS HID:\n"
                "  Vendor ID: %04X\n"
                "  Product ID: %04X\n"
                "  Update interval: %" PRIu32 " ms\n"
                "  Shutdown delay: %" PRIu32 " s",
                this->vid_, this->pid_, this->update_interval_, this->ups_.get_shutdown_delay());
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Battery level", this->battery_level_sensor_);
  LOG_SENSOR("  ", "Battery voltage", this->battery_voltage_sensor_);
  LOG_SENSOR("  ", "Runtime", this->runtime_sensor_);
  LOG_SENSOR("  ", "Input voltage", this->input_voltage_sensor_);
  LOG_SENSOR("  ", "Output voltage", this->output_voltage_sensor_);
  LOG_SENSOR("  ", "Load", this->load_sensor_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Online", this->online_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Charging", this->charging_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Low battery", this->low_battery_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Replace battery", this->replace_battery_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Overload", this->overload_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_binary_sensor_);
#endif
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Status", this->status_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Model", this->model_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Serial", this->serial_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Test result", this->test_result_text_sensor_);
#endif
}

bool UpsHid::run_command(UpsCommand command) {
  if (command >= UPS_COMMAND_COUNT)
    return false;
  const char *name = get_command_info(command).name;
  if (this->stage_ != UPS_HID_STAGE_RUNNING) {
    ESP_LOGW(TAG, "Cannot run %s: UPS not connected", name);
    return false;
  }
  if (!this->ups_.command_supported(command)) {
    ESP_LOGW(TAG, "Command %s is not supported by this UPS", name);
    return false;
  }
  if (this->command_count_ == COMMAND_QUEUE_SIZE) {
    ESP_LOGW(TAG, "Command queue full, dropping %s", name);
    return false;
  }
  ESP_LOGI(TAG, "Queueing command %s", name);
  this->commands_[this->command_count_++] = command;
  this->enable_loop();
  return true;
}

void UpsHid::on_connected() {
  if (!this->find_hid_interface_()) {
    this->status_set_error(LOG_STR("No HID interface found"));
    this->disconnect();
    return;
  }
  esp_err_t err = usb_host_interface_claim(this->handle_, this->device_handle_, this->interface_number_, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Claiming interface %u failed: %s", this->interface_number_, esp_err_to_name(err));
    this->status_set_error(LOG_STR("Interface claim failed"));
    this->disconnect();
    return;
  }
  this->read_identity_();
  this->stage_ = UPS_HID_STAGE_CONFIGURING;
  // SET_IDLE(0): only report on change. Many devices stall this request, which is harmless.
  if (!this->submit_control_(CONTROL_OP_SET_IDLE, CLASS_OUT, HID_REQUEST_SET_IDLE, 0, 0))
    this->stage_ = UPS_HID_STAGE_FAILED;
}

void UpsHid::on_disconnected() {
  if (this->stage_ != UPS_HID_STAGE_DISCONNECTED) {
    ESP_LOGI(TAG, "UPS disconnected");
  }
  this->input_active_.store(false);
  if (this->input_endpoint_ != 0) {
    usb_host_endpoint_halt(this->device_handle_, this->input_endpoint_);
    usb_host_endpoint_flush(this->device_handle_, this->input_endpoint_);
  }
  if (this->stage_ != UPS_HID_STAGE_DISCONNECTED)
    usb_host_interface_release(this->handle_, this->device_handle_, this->interface_number_);
  InputReport *report;
  while ((report = this->input_queue_.pop()) != nullptr)
    this->input_pool_.release(report);
  // A control transfer still in flight completes later and is ignored by handle_control_done_()
  this->stage_ = UPS_HID_STAGE_DISCONNECTED;
  this->input_endpoint_ = 0;
  this->poll_count_ = 0;
  this->command_count_ = 0;
  this->poll_pending_ = false;
  this->ups_.reset();
  this->publish_entities_();
  USBClient::on_disconnected();
}

bool UpsHid::find_hid_interface_() {
  const usb_config_desc_t *config;
  if (usb_host_get_active_config_descriptor(this->device_handle_, &config) != ESP_OK)
    return false;
  int offset = 0;
  bool in_hid_interface = false;
  bool found = false;
  this->report_descriptor_length_ = 0;
  this->input_endpoint_ = 0;
  const auto *desc = reinterpret_cast<const usb_standard_desc_t *>(config);
  while ((desc = usb_parse_next_descriptor(desc, config->wTotalLength, &offset)) != nullptr) {
    const auto *raw = reinterpret_cast<const uint8_t *>(desc);
    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      if (found)
        break;  // only the first HID interface is used
      const auto *intf = reinterpret_cast<const usb_intf_desc_t *>(desc);
      in_hid_interface = intf->bInterfaceClass == USB_CLASS_HID && intf->bAlternateSetting == 0;
      if (in_hid_interface) {
        this->interface_number_ = intf->bInterfaceNumber;
        found = true;
      }
    } else if (in_hid_interface && desc->bDescriptorType == DESCRIPTOR_TYPE_HID && desc->bLength >= 9 &&
               raw[6] == DESCRIPTOR_TYPE_REPORT) {
      // HID descriptor: bcdHID(2) bCountryCode bNumDescriptors, then type and length of the report descriptor
      this->report_descriptor_length_ = raw[7] | (raw[8] << 8);
    } else if (in_hid_interface && desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const auto *ep = reinterpret_cast<const usb_ep_desc_t *>(desc);
      if ((ep->bEndpointAddress & usb_host::USB_DIR_IN) &&
          (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT &&
          this->input_endpoint_ == 0) {
        this->input_endpoint_ = ep->bEndpointAddress;
        this->input_length_ = ep->wMaxPacketSize;
      }
    }
  }
  if (!found || this->report_descriptor_length_ == 0)
    return false;
  ESP_LOGD(TAG, "HID interface %u, report descriptor %u bytes, interrupt endpoint %02X (%u bytes)",
           this->interface_number_, this->report_descriptor_length_, this->input_endpoint_, this->input_length_);
  return true;
}

void UpsHid::read_identity_() {
  const usb_device_desc_t *device;
  uint16_t vid = 0;
  uint16_t pid = 0;
  if (usb_host_get_device_descriptor(this->device_handle_, &device) == ESP_OK) {
    vid = device->idVendor;
    pid = device->idProduct;
  }
  usb_device_info_t info;
  char manufacturer[IDENTITY_STRING_SIZE] = "";
  char product[IDENTITY_STRING_SIZE] = "";
  char serial[IDENTITY_STRING_SIZE] = "";
  if (usb_host_device_info(this->device_handle_, &info) == ESP_OK) {
    descriptor_to_ascii(info.str_desc_manufacturer, manufacturer, sizeof(manufacturer));
    descriptor_to_ascii(info.str_desc_product, product, sizeof(product));
    descriptor_to_ascii(info.str_desc_serial_num, serial, sizeof(serial));
  }
  this->ups_.set_identity(vid, pid, manufacturer, product, serial);
  ESP_LOGI(TAG, "UPS connected: %s %s (serial %s, firmware %s)", this->ups_.get_manufacturer(), this->ups_.get_model(),
           this->ups_.get_serial(), this->ups_.get_firmware());
}

bool UpsHid::submit_control_(ControlOp op, uint8_t type, uint8_t request, uint16_t value, uint16_t length) {
  if (this->control_ == nullptr || this->control_op_ != CONTROL_OP_NONE)
    return false;
  if (SETUP_PACKET_SIZE + length > this->control_->data_buffer_size) {
    ESP_LOGE(TAG, "Control transfer of %u bytes is too large", length);
    return false;
  }
  uint8_t *setup = this->control_->data_buffer;
  setup[0] = type;
  setup[1] = request;
  setup[2] = value & 0xFF;
  setup[3] = value >> 8;
  setup[4] = this->interface_number_;
  setup[5] = 0;
  setup[6] = length & 0xFF;
  setup[7] = length >> 8;
  this->control_->device_handle = this->device_handle_;
  this->control_->bEndpointAddress = type & usb_host::USB_DIR_MASK;
  this->control_->num_bytes = static_cast<int>(SETUP_PACKET_SIZE + length);
  this->control_->callback = control_callback;
  this->control_->context = this;
  this->control_op_ = op;
  esp_err_t err = usb_host_transfer_submit_control(this->handle_, this->control_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Control transfer submit failed: %s", esp_err_to_name(err));
    this->control_op_ = CONTROL_OP_NONE;
    return false;
  }
  return true;
}

bool UpsHid::get_report_(ControlOp op, HidReportType type, uint8_t id, uint16_t length) {
  return this->submit_control_(op, CLASS_IN, HID_REQUEST_GET_REPORT, (type << 8) | id, length);
}

// CALLBACK CONTEXT: USB task
void UpsHid::control_callback(usb_transfer_t *transfer) {
  auto *self = static_cast<UpsHid *>(transfer->context);
  self->control_done_.store(true, std::memory_order_release);
  self->enable_loop_soon_any_context();
  App.wake_loop_threadsafe();
}

void UpsHid::handle_control_done_() {
  ControlOp op = this->control_op_;
  this->control_op_ = CONTROL_OP_NONE;
  if (this->stage_ == UPS_HID_STAGE_DISCONNECTED || op == CONTROL_OP_NONE)
    return;  // completed after the device went away
  bool success = this->control_->status == USB_TRANSFER_STATUS_COMPLETED;
  // actual_num_bytes counts the setup packet
  size_t actual = this->control_->actual_num_bytes;
  size_t len = actual > SETUP_PACKET_SIZE ? actual - SETUP_PACKET_SIZE : 0;
  const uint8_t *data = this->control_->data_buffer + SETUP_PACKET_SIZE;

  switch (op) {
    case CONTROL_OP_SET_IDLE:
      if (!this->submit_control_(CONTROL_OP_GET_DESCRIPTOR, STANDARD_IN, REQUEST_GET_DESCRIPTOR,
                                 DESCRIPTOR_TYPE_REPORT << 8,
                                 std::min<uint16_t>(this->report_descriptor_length_, REPORT_DESCRIPTOR_MAX))) {
        this->stage_ = UPS_HID_STAGE_FAILED;
        this->status_set_error(LOG_STR("Report descriptor request failed"));
      }
      break;
    case CONTROL_OP_GET_DESCRIPTOR:
      if (!success) {
        ESP_LOGE(TAG, "Reading the report descriptor failed (status %d)", this->control_->status);
        this->stage_ = UPS_HID_STAGE_FAILED;
        this->status_set_error(LOG_STR("Report descriptor read failed"));
        break;
      }
      this->on_descriptor_read_(data, len);
      break;
    case CONTROL_OP_POLL:
      if (success) {
        this->decode_report_(this->poll_list_[this->poll_index_ - 1].type, data, len);
      } else {
        ESP_LOGV(TAG, "Report %02X read failed (status %d)", this->poll_list_[this->poll_index_ - 1].id,
                 this->control_->status);
      }
      break;
    case CONTROL_OP_COMMAND_READ:
      this->on_command_report_read_(success, data, len);
      break;
    case CONTROL_OP_COMMAND_WRITE: {
      const char *name = get_command_info(this->active_command_).name;
      if (success) {
        ESP_LOGI(TAG, "Command %s sent", name);
      } else {
        ESP_LOGW(TAG, "Command %s failed (status %d)", name, this->control_->status);
      }
      // Read back the new state
      this->poll_index_ = 0;
      this->poll_pending_ = true;
      break;
    }
    default:
      break;
  }
}

void UpsHid::on_descriptor_read_(const uint8_t *data, size_t len) {
  if (len < this->report_descriptor_length_) {
    ESP_LOGW(TAG, "Report descriptor truncated: %zu of %u bytes", len, this->report_descriptor_length_);
  }
  HidParseResult result = parse_report_descriptor(data, len, this->fields_);
  if (result != HID_PARSE_RESULT_OK) {
    ESP_LOGE(TAG, "Report descriptor parse failed (%u)", result);
    this->stage_ = UPS_HID_STAGE_FAILED;
    this->status_set_error(LOG_STR("Report descriptor parse failed"));
    return;
  }
  this->log_fields_();
  this->ups_.set_field_map(this->fields_);
  this->build_poll_list_();
  if (this->poll_count_ == 0) {
    ESP_LOGE(TAG, "No UPS values found in the report descriptor");
    this->stage_ = UPS_HID_STAGE_FAILED;
    this->status_set_error(LOG_STR("Not a UPS"));
    return;
  }
  this->status_clear_error();
  this->ups_.set_connected(true);
  this->stage_ = UPS_HID_STAGE_RUNNING;
  this->start_input_();
  this->poll_index_ = 0;
  this->poll_pending_ = true;
}

void UpsHid::on_command_report_read_(bool success, const uint8_t *data, size_t len) {
  const UpsCommandInfo &info = get_command_info(this->active_command_);
  const HidField &field = this->fields_.feature[info.field];
  // Write the report back as read, with only this field changed. Read and write share the buffer.
  uint8_t *report = this->control_->data_buffer + SETUP_PACKET_SIZE;
  size_t bytes = field.report_bytes;
  if (!success || len < bytes || (field.report_id != 0 && data[0] != field.report_id)) {
    memset(report, 0, bytes);
    report[0] = field.report_id;
  }
  float value = info.value == COMMAND_VALUE_SHUTDOWN_DELAY ? static_cast<float>(this->ups_.get_shutdown_delay())
                                                           : static_cast<float>(info.value);
  field.insert(report, bytes, field.to_logical(value));
  if (!this->submit_control_(CONTROL_OP_COMMAND_WRITE, CLASS_OUT, HID_REQUEST_SET_REPORT,
                             (HID_REPORT_TYPE_FEATURE << 8) | field.report_id, bytes)) {
    ESP_LOGW(TAG, "Command %s could not be sent", info.name);
  }
}

bool UpsHid::start_next_control_() {
  if (this->command_count_ != 0) {
    this->active_command_ = this->commands_[0];
    this->command_count_--;
    for (uint8_t i = 0; i != this->command_count_; i++)
      this->commands_[i] = this->commands_[i + 1];
    const HidField &field = this->fields_.feature[get_command_info(this->active_command_).field];
    if (field.report_bytes > REPORT_MAX ||
        !this->get_report_(CONTROL_OP_COMMAND_READ, HID_REPORT_TYPE_FEATURE, field.report_id, field.report_bytes)) {
      ESP_LOGW(TAG, "Command %s could not be sent", get_command_info(this->active_command_).name);
    }
    return true;
  }
  if (!this->poll_pending_)
    return false;
  if (this->poll_index_ >= this->poll_count_) {
    this->poll_pending_ = false;
    this->publish_pending_ = true;
    return true;
  }
  const PollEntry &entry = this->poll_list_[this->poll_index_++];
  this->get_report_(CONTROL_OP_POLL, entry.type, entry.id, entry.bytes);
  return true;
}

void UpsHid::start_input_() {
  if (this->input_endpoint_ == 0 || this->input_length_ == 0)
    return;  // no interrupt endpoint: polling alone keeps the data current
  // Interrupt IN transfers must be a whole number of packets
  uint16_t packet = this->input_length_;
  uint16_t length = (std::max<uint16_t>(this->fields_.max_input_report_bytes, 1) + packet - 1) / packet * packet;
  if (length > std::min<size_t>(REPORT_MAX, usb_host::USB_MAX_PACKET_SIZE))
    length = packet;
  this->input_length_ = length;
  this->input_active_.store(true);
  if (!this->transfer_in(this->input_endpoint_, this->input_callback_, this->input_length_))
    this->input_active_.store(false);
}

void UpsHid::build_poll_list_() {
  this->poll_count_ = 0;
  auto add = [this](HidReportType type, const HidField &field) {
    for (uint8_t i = 0; i != this->poll_count_; i++) {
      if (this->poll_list_[i].type == type && this->poll_list_[i].id == field.report_id)
        return;
    }
    if (this->poll_count_ < this->poll_list_.size() && field.report_bytes <= REPORT_MAX)
      this->poll_list_[this->poll_count_++] = {field.report_bytes, type, field.report_id};
  };
  for (uint8_t f = 0; f != UPS_FIELD_COUNT; f++) {
    if (this->fields_.feature[f].present()) {
      add(HID_REPORT_TYPE_FEATURE, this->fields_.feature[f]);
    } else if (this->fields_.input[f].present()) {
      add(HID_REPORT_TYPE_INPUT, this->fields_.input[f]);
    }
  }
}

void UpsHid::decode_report_(HidReportType type, const uint8_t *data, size_t len) {
  if (len == 0)
    return;
  for (uint8_t f = 0; f != UPS_FIELD_COUNT; f++) {
    const HidField &field = this->fields_.get(type, static_cast<UpsField>(f));
    if (!field.present() || (field.report_id != 0 && data[0] != field.report_id))
      continue;
    int32_t logical;
    if (field.extract(data, len, logical))
      this->ups_.set_value(static_cast<UpsField>(f), field.to_physical(logical));
  }
}

void UpsHid::publish_entities_() {
  const UpsState &ups = this->ups_;
  bool has_data = ups.has_data();
#ifdef USE_SENSOR
  auto publish = [&](sensor::Sensor *sensor, UpsField field) {
    if (sensor != nullptr)
      sensor->publish_state(has_data && ups.has(field) ? ups.get(field) : NAN);
  };
  publish(this->battery_level_sensor_, UPS_FIELD_BATTERY_CHARGE);
  publish(this->battery_voltage_sensor_, UPS_FIELD_BATTERY_VOLTAGE);
  publish(this->runtime_sensor_, UPS_FIELD_BATTERY_RUNTIME);
  publish(this->input_voltage_sensor_, UPS_FIELD_INPUT_VOLTAGE);
  publish(this->output_voltage_sensor_, UPS_FIELD_OUTPUT_VOLTAGE);
  publish(this->load_sensor_, UPS_FIELD_LOAD);
#endif
#ifdef USE_BINARY_SENSOR
  auto publish_flag = [&](binary_sensor::BinarySensor *sensor, UpsField field) {
    if (sensor != nullptr && has_data && ups.has(field))
      sensor->publish_state(ups.flag(field));
  };
  publish_flag(this->online_binary_sensor_, UPS_FIELD_AC_PRESENT);
  publish_flag(this->charging_binary_sensor_, UPS_FIELD_CHARGING);
  publish_flag(this->overload_binary_sensor_, UPS_FIELD_OVERLOAD);
  publish_flag(this->replace_battery_binary_sensor_, UPS_FIELD_NEED_REPLACEMENT);
  if (this->low_battery_binary_sensor_ != nullptr && has_data &&
      (ups.has(UPS_FIELD_BELOW_CAPACITY_LIMIT) || ups.has(UPS_FIELD_SHUTDOWN_IMMINENT))) {
    this->low_battery_binary_sensor_->publish_state(ups.flag(UPS_FIELD_BELOW_CAPACITY_LIMIT) ||
                                                    ups.flag(UPS_FIELD_TIME_LIMIT_EXPIRED) ||
                                                    ups.flag(UPS_FIELD_SHUTDOWN_IMMINENT));
  }
  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_state(has_data);
#endif
#ifdef USE_TEXT_SENSOR
  auto publish_text = [](text_sensor::TextSensor *sensor, const char *value) {
    if (sensor != nullptr && (!sensor->has_state() || sensor->get_state() != value))
      sensor->publish_state(value);
  };
  char status[UPS_VALUE_BUFFER_SIZE];
  if (has_data) {
    ups.format_status(status, sizeof(status));
  } else {
    snprintf(status, sizeof(status), "%s", this->stage_ == UPS_HID_STAGE_DISCONNECTED ? "Disconnected" : "Waiting");
  }
  publish_text(this->status_text_sensor_, status);
  if (has_data) {
    publish_text(this->model_text_sensor_, ups.get_model());
    publish_text(this->serial_text_sensor_, ups.get_serial());
    const char *test_result = ups.test_result();
    if (test_result != nullptr)
      publish_text(this->test_result_text_sensor_, test_result);
  }
#endif
}

void UpsHid::log_fields_() const {
  static const char *const TYPE_NAMES[] = {"", "input", "output", "feature"};
  uint8_t found = 0;
  for (uint8_t f = 0; f != UPS_FIELD_COUNT; f++) {
    for (HidReportType type : {HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_FEATURE}) {
      const HidField &field = this->fields_.get(type, static_cast<UpsField>(f));
      if (!field.present())
        continue;
      found++;
      ESP_LOGV(TAG, "Field %u: %s report %02X, bit %u, %u bits, range %" PRId32 "..%" PRId32 ", exponent %d", f,
               TYPE_NAMES[type], field.report_id, field.bit_offset, field.bit_size, field.logical_min,
               field.logical_max, field.unit_exponent);
    }
  }
  ESP_LOGD(TAG, "Report descriptor mapped %u fields", found);
}

}  // namespace esphome::ups_hid

#endif
