#pragma once

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || \
    defined(USE_ESP32_VARIANT_ESP32S31) || defined(USE_ESP32_VARIANT_ESP32H4)
#include "esphome/core/automation.h"
#include "ups_hid.h"

namespace esphome::ups_hid {

template<typename... Ts> class UpsCommandAction : public Action<Ts...> {
 public:
  explicit UpsCommandAction(UpsHid *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(UpsCommand, command)

  void play(const Ts &...x) override { this->parent_->run_command(this->command_.value(x...)); }

 protected:
  UpsHid *parent_;
};

}  // namespace esphome::ups_hid

#endif
