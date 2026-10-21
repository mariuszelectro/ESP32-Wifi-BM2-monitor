#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace bm2_ble {

static const char *const TAG = "bm2_ble";

class BM2BLEComponent : public Component, public ble_client::BLEClientNode {
 public:
  BM2BLEComponent() {}

  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::DATA; }

  // Entity registration (called from Python codegen)
  template<typename T>
  void add_entity(const std::string &type, T *entity) {
    if constexpr (std::is_base_of<sensor::Sensor, T>::value) {
      sensors_[type] = entity;
    } else if constexpr (std::is_base_of<binary_sensor::BinarySensor, T>::value) {
      binary_sensors_[type] = entity;
    } else if constexpr (std::is_base_of<text_sensor::TextSensor, T>::value) {
      text_sensors_[type] = entity;
    } else {
      ESP_LOGW(TAG, "Unknown entity type for type %s", type.c_str());
    }
  }

 protected:
  static constexpr uint8_t AES_KEY[16] = {
      0x6C,0x65,0x61,0x67,0x65,0x6E,0x64,0xFF,0xFE,0x31,0x38,0x38,0x32,0x34,0x36,0x36};

  void decrypt_and_handle(const std::vector<uint8_t> &data);
  void parse_voltage_message(const std::string &hex);
  void update_entities(float voltage, int status, int battery);
  void ensure_subscription();

  // override event handler to capture BLE notifications
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  bool subscribed_{false};
  uint16_t notify_handle_{0};
  uint16_t write_handle_{0};

  uint32_t last_update_{0};

  // Entity maps (type-based)
  std::map<std::string, sensor::Sensor *> sensors_;
  std::map<std::string, binary_sensor::BinarySensor *> binary_sensors_;
  std::map<std::string, text_sensor::TextSensor *> text_sensors_;
};

// Platform sensor classes
class BM2BleSensor : public sensor::Sensor, public PollingComponent {
 public:
  void update() override {}
};

class BM2BleTextSensor : public text_sensor::TextSensor, public PollingComponent {
 public:
  void update() override {}
};

class BM2BleBinarySensor : public binary_sensor::BinarySensor, public PollingComponent {
 public:
  void update() override {}
};

}  // namespace bm2_ble
}  // namespace esphome
