#pragma once

#include <span>
#include <cstdint>
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <esp_rom_sys.h>

#include "esphome/components/web_server_base/web_server_base.h"
#include "offline_rtc.h"

struct WallboxBtDevice {
  std::string address;
  std::string name;
  uint32_t packet_count{0};
  uint32_t last_seen_ms{0};
};

inline std::vector<WallboxBtDevice> wallbox_bt_devices;
inline SemaphoreHandle_t wallbox_bt_devices_mutex = nullptr;

inline void wallbox_bt_devices_initialize() {
  if (wallbox_bt_devices_mutex == nullptr) wallbox_bt_devices_mutex = xSemaphoreCreateMutex();
}

inline size_t wallbox_bt_devices_clear() {
  if (wallbox_bt_devices_mutex != nullptr) xSemaphoreTake(wallbox_bt_devices_mutex, portMAX_DELAY);
  wallbox_bt_devices.clear();
  const size_t count = wallbox_bt_devices.size();
  if (wallbox_bt_devices_mutex != nullptr) xSemaphoreGive(wallbox_bt_devices_mutex);
  return count;
}

inline size_t wallbox_bt_device_observe(const std::string &address, const std::string &name, uint32_t now_ms) {
  if (wallbox_bt_devices_mutex != nullptr) xSemaphoreTake(wallbox_bt_devices_mutex, portMAX_DELAY);
  auto device = wallbox_bt_devices.begin();
  while (device != wallbox_bt_devices.end() && device->address != address) ++device;
  if (device == wallbox_bt_devices.end()) {
    wallbox_bt_devices.push_back({address, name, 1, now_ms});
  } else {
    device->packet_count++;
    device->last_seen_ms = now_ms;
    if (!name.empty()) device->name = name;
  }
  const size_t count = wallbox_bt_devices.size();
  if (wallbox_bt_devices_mutex != nullptr) xSemaphoreGive(wallbox_bt_devices_mutex);
  return count;
}

inline void wallbox_json_append_escaped(std::string &output, const std::string &value) {
  for (const unsigned char character : value) {
    switch (character) {
      case '"': output.append("\\\""); break;
      case '\\': output.append("\\\\"); break;
      case '\b': output.append("\\b"); break;
      case '\f': output.append("\\f"); break;
      case '\n': output.append("\\n"); break;
      case '\r': output.append("\\r"); break;
      case '\t': output.append("\\t"); break;
      default:
        if (character < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          output.append("\\u00");
          output.push_back(hex[character >> 4]);
          output.push_back(hex[character & 0x0F]);
        } else {
          output.push_back(static_cast<char>(character));
        }
    }
  }
}

inline std::string wallbox_bt_devices_json() {
  if (wallbox_bt_devices_mutex != nullptr) xSemaphoreTake(wallbox_bt_devices_mutex, portMAX_DELAY);
  std::string response;
  response.reserve(24 + wallbox_bt_devices.size() * 100);
  response.append("{\"count\":");
  response.append(std::to_string(wallbox_bt_devices.size()));
  response.append(",\"devices\":[");
  for (size_t index = 0; index < wallbox_bt_devices.size(); index++) {
    if (index != 0) response.push_back(',');
    const auto &device = wallbox_bt_devices[index];
    response.append("{\"address\":\"");
    wallbox_json_append_escaped(response, device.address);
    response.append("\",\"name\":\"");
    wallbox_json_append_escaped(response, device.name);
    response.append("\",\"packets\":");
    response.append(std::to_string(device.packet_count));
    response.append(",\"last_seen_s\":");
    response.append(std::to_string((millis() - device.last_seen_ms) / 1000U));
    response.push_back('}');
  }
  response.append("]}");
  if (wallbox_bt_devices_mutex != nullptr) xSemaphoreGive(wallbox_bt_devices_mutex);
  return response;
}

class WallboxVoltageApiHandler final : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    if (request->method() != HTTP_GET) return false;
    char path[AsyncWebServerRequest::URL_BUF_SIZE];
    const std::span<char, AsyncWebServerRequest::URL_BUF_SIZE> path_span(path);
    const auto url = request->url_to(path_span);
    return url == "/api/voltage" || url == "/api/devices";
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char path[AsyncWebServerRequest::URL_BUF_SIZE];
    const std::span<char, AsyncWebServerRequest::URL_BUF_SIZE> path_span(path);
    if (request->url_to(path_span) == "/api/devices") {
      const std::string response = wallbox_bt_devices_json();
      request->send(200, "application/json", response.c_str());
      return;
    }

    const std::string source = request->arg("source");
    const int source_index = source == "esp" ? 0 : source == "bm2" ? 1 : source == "temp" ? 2 : -1;
    if (source_index < 0) {
      request->send(400, "application/json", "{\"error\":\"source must be esp, bm2 or temp\"}");
      return;
    }

    const std::string summary = wallbox_rtc_summary(source_index);
    const std::string plot = wallbox_rtc_plot(source_index);
    std::string response;
    response.reserve(summary.size() + plot.size() + 32);
    response.append("{\"summary\":");
    response.append(summary);
    response.append(",\"plot\":");
    response.append(plot);
    response.push_back('}');
    request->send(200, "application/json", response.c_str());
  }
};

inline void wallbox_register_voltage_api() {
  static bool registered = false;
  static WallboxVoltageApiHandler handler;
  if (registered || esphome::web_server_base::global_web_server_base == nullptr) return;
  esphome::web_server_base::global_web_server_base->add_handler(&handler);
  registered = true;
}