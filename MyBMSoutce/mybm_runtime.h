#pragma once

#include "mybm_model.h"
#include "mybm_page.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/preferences.h"
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_private/esp_clk.h>
#include <esp_rom_sys.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_pm.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <span>
#include <string>

namespace mybm {

#ifndef MYBM_LIGHT_SLEEP
#define MYBM_LIGHT_SLEEP 0
#endif
static_assert(MYBM_LIGHT_SLEEP == 0 || MYBM_LIGHT_SLEEP == 1);
// Light sleep retains the BLE host/controller between bursts; never call
// esp_light_sleep_start() directly while the BLE controller is enabled.
inline constexpr bool LIGHT_SLEEP_MODE = MYBM_LIGHT_SLEEP == 1;
inline bool light_sleep_allowed{false};

inline void configure_light_sleep(bool allow) {
  esp_pm_config_t config{};
  config.max_freq_mhz = 160;
  config.min_freq_mhz = allow ? 40 : 160;
  config.light_sleep_enable = allow;
  ESP_ERROR_CHECK(esp_pm_configure(&config));
  light_sleep_allowed = allow;
}

struct Retained {
  uint32_t magic{0};
  History history{};
  uint32_t bursts{0};
  bool button_blocked{false};
  uint64_t next_radio_us{0};
  uint64_t next_button_us{0};
  uint64_t next_measurement_us{0};
  Sample sample{};
  uint32_t wake_counter{0};
};
RTC_DATA_ATTR inline Retained retained;
struct TimingHistory {
  uint32_t magic{0};
  std::array<WakeTiming, 3> last{};
  std::array<uint32_t, 3> count{};
};
RTC_DATA_ATTR inline TimingHistory timing_history;
inline WakeTiming wake_timing;
inline bool wake_included_portal{false};
inline uint64_t timing_us() { return static_cast<uint64_t>(esp_timer_get_time()); }
inline Settings settings;
inline esphome::ESPPreferenceObject preference;
inline std::mutex state_mutex;
inline esphome::esp32_ble::ESP32BLE *ble{nullptr};
inline bool portal_active{false};
inline bool timed_portal_active{false};
inline VoltagePortal voltage_portal;
inline bool boot_completed{false};
inline bool initial_measurement_pending{false};
inline bool wifi_enable_pending{false};
inline bool wifi_disable_pending{false};
inline bool button_press_active{false};
inline uint32_t button_pressed_ms{0};
inline RadioPhase phase{RadioPhase::IDLE};
inline uint32_t phase_started_ms{0};
inline uint32_t portal_started_ms{0};
inline bool portal_stop_requested{false};
inline uint32_t portal_stop_requested_ms{0};
inline uint32_t led_off_ms{0};
inline uint32_t led_last_flash_ms{0};
inline uint64_t cycle_started_us{0};
inline bool radio_cycle_due{false};
inline bool button_check_only{false};
inline bool sleep_after_cycle{false};
inline bool sleep_after_portal{false};
inline float voltage{NAN};
inline float uncalibrated_voltage{NAN};
inline float temperature{NAN};
inline std::array<uint8_t, 31> payload{};
inline uint8_t payload_size{0};
inline std::string error;

inline uint64_t clock_us() { return esp_clk_rtc_time(); }

inline void enter_deep_sleep_until(uint64_t wake_us) {
  if (!boot_completed || portal_active) {
    ESP_LOGE("mybm", "Deep sleep blocked: startup or portal is active");
    return;
  }
  if constexpr (LIGHT_SLEEP_MODE) {
    sleep_after_portal = false;
    return;
  }
  const uint64_t now_us = clock_us();
  const uint64_t duration_us = deep_sleep_duration_us(
      now_us, next_sleep_wake_us(retained.next_button_us, retained.next_measurement_us, wake_us));
  const esp_err_t error_code = esp_sleep_enable_timer_wakeup(duration_us);
  if (error_code != ESP_OK) {
    ESP_LOGE("mybm", "Could not configure deep-sleep timer: %d", error_code);
    esp_restart();
    while (true) {
    }
  }
  gpio_set_level(GPIO_NUM_15, 1);
  if (!wake_included_portal && (!wake_timing.radio_requested || wake_timing.radio_completed)) {
    wake_timing.awake_us = timing_us();
    const size_t kind = wake_timing.radio_requested ? 2 : (wake_timing.measured ? 1 : 0);
    timing_history.last[kind] = wake_timing;
    ++timing_history.count[kind];
  }
  ESP_LOGD("mybm", "Entering deep sleep for %llu us", static_cast<unsigned long long>(duration_us));
  esp_deep_sleep_start();
  while (true) {
  }
}

inline void sleep_for_button_check() {
  if constexpr (LIGHT_SLEEP_MODE) return;
  enter_deep_sleep_until(retained.next_radio_us);
}

inline bool radio_shutdown_confirmed() {
  return phase == RadioPhase::IDLE &&
         esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED &&
         esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE;
}

inline void fail(const char *operation, int code) {
  char text[160];
  std::snprintf(text, sizeof(text), "%s: %d", operation, code);
  error = text;
  ESP_LOGE("mybm", "%s", text);
  phase = RadioPhase::DISABLING;
  phase_started_ms = millis();
  ble->disable();
}

inline void set_phase(RadioPhase value) {
  phase = value;
  phase_started_ms = millis();
  if (value == RadioPhase::STOPPING) wake_timing.stop_requested_us = timing_us();
}

inline void flash_led(uint32_t duration_ms) {
  gpio_set_level(GPIO_NUM_15, 0);
  led_off_ms = millis() + duration_ms;
}

inline void boot() {
  gpio_set_direction(GPIO_NUM_15, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_15, 1);
  gpio_set_direction(GPIO_NUM_9, GPIO_MODE_INPUT);
  gpio_set_pull_mode(GPIO_NUM_9, GPIO_PULLUP_ONLY);
  constexpr uint32_t kRetainedMagic = 0x4D594207;
  const bool timer_wake = resume_sleep_cycle(
      esp_reset_reason() == ESP_RST_DEEPSLEEP,
      esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER,
      retained.magic == kRetainedMagic);
  portal_active = !timer_wake;
  initial_measurement_pending = !timer_wake;
  constexpr uint32_t kTimingMagic = 0x4D595401;
  if (!timer_wake || timing_history.magic != kTimingMagic) {
    timing_history = {};
    timing_history.magic = kTimingMagic;
  }
  if (!timer_wake || retained.magic != kRetainedMagic) {
    retained = {};
    retained.magic = kRetainedMagic;
  }
  preference = esphome::global_preferences->make_preference<Settings>(0x4D594204);
  Settings stored;
  bool loaded = preference.load(&stored);
  if (!loaded) {
    struct PreviousTimingSettings {
      float factor;
      uint32_t period_ms, burst_ms, history_interval_s, average_count, portal_minutes;
      uint32_t button_interval_s, measurement_interval_s;
    } previous;
    auto old_timing = esphome::global_preferences->make_preference<PreviousTimingSettings>(0x4D594203);
    if (old_timing.load(&previous)) {
      stored = {previous.factor, previous.period_ms, previous.burst_ms, previous.history_interval_s,
                previous.average_count, previous.portal_minutes, previous.button_interval_s,
                previous.measurement_interval_s};
      loaded = true;
      if (valid_settings(stored) &&
          (!preference.save(&stored) || !esphome::global_preferences->sync()))
        ESP_LOGE("mybm", "Failed to migrate timing settings");
    }
  }
  if (loaded) {
    if (valid_settings(stored)) settings = stored;
    else ESP_LOGE("mybm", "Invalid saved settings; using factory settings");
  } else {
    struct PreviousSettings {
      float factor;
      uint32_t period_ms, burst_ms, history_interval_s, average_count, portal_minutes;
    } previous;
    auto previous_preference = esphome::global_preferences->make_preference<PreviousSettings>(0x4D594202);
    auto old_preference = esphome::global_preferences->make_preference<PreviousSettings>(0x4D594201);
    const bool previous_loaded = previous_preference.load(&previous);
    const bool old_loaded = !previous_loaded && old_preference.load(&previous);
    if (previous_loaded || old_loaded) {
      stored = {previous.factor, previous.period_ms, previous.burst_ms,
                previous.history_interval_s, previous.average_count, previous.portal_minutes};
      if (old_loaded) stored.burst_ms = 50;
      if (valid_settings(stored)) {
        settings = stored;
        if (!preference.save(&stored) || !esphome::global_preferences->sync())
          ESP_LOGE("mybm", "Failed to migrate saved settings");
      } else {
        ESP_LOGE("mybm", "Invalid saved settings; using factory settings");
      }
    } else {
      struct LegacySettings {
        float factor;
        uint32_t period_ms, burst_ms, history_interval_s, average_count;
      } legacy;
      auto legacy_preference = esphome::global_preferences->make_preference<LegacySettings>(0x4D594201);
      if (legacy_preference.load(&legacy)) {
        stored = {legacy.factor, legacy.period_ms, 50,
                  legacy.history_interval_s, legacy.average_count, 5};
        if (valid_settings(stored)) {
          settings = stored;
          if (!preference.save(&stored) || !esphome::global_preferences->sync())
            ESP_LOGE("mybm", "Failed to migrate legacy settings");
        } else {
          ESP_LOGE("mybm", "Invalid legacy settings; using factory settings");
        }
      }
    }
  }
  if (!timer_wake) retained.next_radio_us = 0;
  if (gpio_get_level(GPIO_NUM_9) != 0) retained.button_blocked = false;
  else if (!retained.button_blocked) {
    gpio_set_level(GPIO_NUM_15, 0);
    for (uint32_t elapsed = 0; elapsed < 3000; ++elapsed) {
      esp_rom_delay_us(1000);
      if (gpio_get_level(GPIO_NUM_9) != 0) {
        portal_active = true;
        break;
      }
    }
    retained.button_blocked = !portal_active;
    gpio_set_level(GPIO_NUM_15, 1);
  }
  const uint64_t now_us = clock_us();
  retained.next_button_us = next_button_check_us(now_us, settings.button_interval_s);
  if (retained.sample.valid) {
    uncalibrated_voltage = retained.sample.raw_voltage;
    voltage = uncalibrated_voltage * settings.factor;
    temperature = retained.sample.temperature;
  }
  radio_cycle_due = !portal_active &&
                    (retained.next_radio_us == 0 || now_us >= retained.next_radio_us);
  button_check_only = timer_wake && !portal_active && !radio_cycle_due &&
                      now_us < retained.next_measurement_us;
  portal_started_ms = millis();
  timed_portal_active = portal_active;
  wake_included_portal = portal_active;
  cycle_started_us = clock_us() - uint64_t(millis()) * 1000ULL;
  boot_completed = true;
  wake_timing.boot_us = timing_us();
  if (!timer_wake)
    ESP_LOGW("mybm", "Boot: reset=%d, portal=%d, portal duration=%lu min",
             static_cast<int>(esp_reset_reason()), portal_active,
             static_cast<unsigned long>(settings.portal_minutes));
}

inline std::string number_or_null(float value, unsigned precision) {
  if (!std::isfinite(value)) return "null";
  char text[32];
  std::snprintf(text, sizeof(text), "%.*f", static_cast<int>(precision), static_cast<double>(value));
  return text;
}

inline std::string timing_json() {
  std::string out = "[";
  for (size_t i = 0; i < timing_history.last.size(); ++i) {
    if (i) out += ',';
    const auto &t = timing_history.last[i];
    out += "{\"count\":" + std::to_string(timing_history.count[i]);
    if (timing_history.count[i]) {
      out += ",\"awake_us\":" + std::to_string(t.awake_us) +
             ",\"boot_us\":" + std::to_string(t.boot_us) +
             ",\"measurement_us\":" + std::to_string(t.measurement_us);
      if (t.radio_completed) {
        out += ",\"before_ble_us\":" + std::to_string(t.radio_requested_us) +
               ",\"ble_startup_us\":" + std::to_string(t.startup_us()) +
               ",\"configuration_us\":" + std::to_string(t.configuration_us()) +
               ",\"advertising_us\":" + std::to_string(t.advertising_us()) +
               ",\"stopping_us\":" + std::to_string(t.stopping_us()) +
               ",\"shutdown_us\":" + std::to_string(t.shutdown_us());
      }
    }
    out += '}';
  }
  return out + "]";
}

inline std::string state_json() {
  std::lock_guard<std::mutex> lock(state_mutex);
  const uint32_t remaining = portal_remaining_ms(millis(), portal_started_ms, settings.portal_minutes);
  std::string out = "{\"voltage\":" + number_or_null(voltage, 3) +
    ",\"uncalibrated_voltage\":" + number_or_null(uncalibrated_voltage, 6) +
    ",\"temperature\":" + number_or_null(temperature, 2) +
    ",\"portal_remaining_s\":" + std::to_string(timed_portal_active ? remaining / 1000 : 0) +
    ",\"voltage_portal_active\":" + (voltage_portal.active ? "true" : "false") +
    ",\"bursts\":" + std::to_string(retained.bursts) +
    ",\"timings\":" + timing_json() +
    ",\"total_points\":" + std::to_string(retained.history.total_points) +
    ",\"capacity\":" + std::to_string(retained.history.points.size()) + ",\"error\":\"";
  for (char c : error) {
    if (c == '"' || c == '\\') out += '\\';
    if (static_cast<unsigned char>(c) >= 0x20) out += c;
  }
  out += "\",\"settings\":{\"factor\":" + number_or_null(settings.factor, 4) +
    ",\"period_ms\":" + std::to_string(settings.period_ms) +
    ",\"burst_ms\":" + std::to_string(settings.burst_ms) +
    ",\"history_interval_s\":" + std::to_string(settings.history_interval_s) +
    ",\"average_count\":" + std::to_string(settings.average_count) +
    ",\"portal_minutes\":" + std::to_string(settings.portal_minutes) +
    ",\"button_interval_s\":" + std::to_string(settings.button_interval_s) +
    ",\"measurement_interval_s\":" + std::to_string(settings.measurement_interval_s) +
    ",\"portal_voltage\":" + number_or_null(settings.portal_voltage, 3) + "},\"points\":[";
  for (size_t i = 0; i < retained.history.count; ++i) {
    if (i) out += ',';
    const Point &p = retained.history.at(i);
    out += "{\"seconds\":" + std::to_string(p.seconds) + ",\"voltage\":" +
      number_or_null(p.voltage, 3) + ",\"temperature\":" + number_or_null(p.temperature, 2) + "}";
  }
  return out + "]}";
}

inline bool parse_float(const std::string &text, float &value) {
  char *end = nullptr;
  errno = 0;
  value = std::strtof(text.c_str(), &end);
  return !text.empty() && errno == 0 && end == text.c_str() + text.size() && std::isfinite(value);
}

inline bool parse_uint(const std::string &text, uint32_t &value) {
  if (text.empty() || text.size() > 10) return false;
  for (char c : text) if (c < '0' || c > '9') return false;
  char *end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
  if (errno || end != text.c_str() + text.size() || parsed > UINT32_MAX) return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

class PortalHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    return portal_active && (request->method() == HTTP_GET || request->method() == HTTP_POST);
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
    const auto path = request->url_to(std::span<char, AsyncWebServerRequest::URL_BUF_SIZE>(buffer));
    if (request->method() == HTTP_GET && path == "/api/state") {
      const auto json = state_json();
      request->send(200, "application/json", json.c_str());
    } else if (request->method() == HTTP_POST && path == "/api/settings") {
      Settings proposed;
      if (!parse_float(request->arg("factor"), proposed.factor) ||
          !parse_uint(request->arg("period_ms"), proposed.period_ms) ||
          !parse_uint(request->arg("burst_ms"), proposed.burst_ms) ||
          !parse_uint(request->arg("history_interval_s"), proposed.history_interval_s) ||
          !parse_uint(request->arg("average_count"), proposed.average_count) ||
          !parse_uint(request->arg("portal_minutes"), proposed.portal_minutes) ||
          !parse_uint(request->arg("button_interval_s"), proposed.button_interval_s) ||
          !parse_uint(request->arg("measurement_interval_s"), proposed.measurement_interval_s) ||
          !parse_float(request->arg("portal_voltage"), proposed.portal_voltage) ||
          !valid_settings(proposed)) {
        ESP_LOGW("mybm", "Rejected invalid portal settings");
        request->send(400, "text/plain", "Nieprawidlowe ustawienia");
        return;
      }
      std::lock_guard<std::mutex> lock(state_mutex);
      if (!preference.save(&proposed) || !esphome::global_preferences->sync()) {
        ESP_LOGE("mybm", "Failed to persist portal settings");
        request->send(500, "text/plain", "Blad zapisu pamieci flash");
        return;
      }
      if (proposed.factor != settings.factor || proposed.history_interval_s != settings.history_interval_s ||
          proposed.average_count != settings.average_count) retained.history.reset_average();
      if (proposed.portal_minutes != settings.portal_minutes) {
        portal_started_ms = millis();
        timed_portal_active = true;
      }
      const uint64_t now_us = clock_us();
      if (proposed.button_interval_s != settings.button_interval_s)
        retained.next_button_us = next_button_check_us(now_us, proposed.button_interval_s);
      if (proposed.measurement_interval_s != settings.measurement_interval_s ||
          proposed.factor != settings.factor || proposed.portal_voltage != settings.portal_voltage)
        retained.next_measurement_us = 0;
      settings = proposed;
      if (retained.sample.valid) voltage = retained.sample.raw_voltage * settings.factor;
      request->send(200, "text/plain", "OK");
    } else if (request->method() == HTTP_POST &&
               (path == "/api/portal/extend" || path == "/api/portal/stop")) {
      std::lock_guard<std::mutex> lock(state_mutex);
      if (path == "/api/portal/extend") {
        portal_started_ms = millis();
        timed_portal_active = true;
        portal_stop_requested = false;
      } else {
        if (voltage_portal.active) {
          request->send(409, "text/plain", "Portal podtrzymywany napieciem lub opoznieniem powrotu");
          return;
        }
        portal_stop_requested = true;
        portal_stop_requested_ms = millis();
      }
      request->send(200, "text/plain", "OK");
    } else if (request->method() == HTTP_GET && (path == "/" || path == "/generate_204" ||
               path == "/hotspot-detect.html")) {
      request->send(200, "text/html; charset=utf-8", PAGE);
    } else {
      request->send(404, "text/plain", "Not found");
    }
  }
};

inline void gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  std::lock_guard<std::mutex> lock(state_mutex);
  if (event == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT && phase == RadioPhase::CONFIGURING) {
    if (portal_active) {
      sleep_after_cycle = false;
      set_phase(RadioPhase::DISABLING);
      ble->disable();
      return;
    }
    if (param->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
      fail("BLE payload configuration failed", param->adv_data_raw_cmpl.status);
      return;
    }
    esp_ble_adv_params_t params{};
    params.adv_int_min = 0xA0;
    params.adv_int_max = 0xA0;
    params.adv_type = ADV_TYPE_NONCONN_IND;
    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.channel_map = ADV_CHNL_ALL;
    params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    set_phase(RadioPhase::STARTING);
    const auto result = esp_ble_gap_start_advertising(&params);
    if (result != ESP_OK) fail("BLE advertising start rejected", result);
  } else if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT && phase == RadioPhase::STARTING) {
    wake_timing.advertising_started_us = timing_us();
    if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
      fail("BLE advertising start failed", param->adv_start_cmpl.status);
    else if (portal_active) {
      set_phase(RadioPhase::STOPPING);
      const auto result = esp_ble_gap_stop_advertising();
      if (result != ESP_OK) fail("BLE stop for portal rejected", result);
    } else set_phase(RadioPhase::ADVERTISING);
  } else if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT && phase == RadioPhase::STOPPING) {
    if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
      fail("BLE advertising stop failed", param->adv_stop_cmpl.status);
      return;
    }
    ++retained.bursts;
    retained.wake_counter = next_wake_counter(retained.wake_counter);
    wake_timing.advertising_stopped_us = timing_us();
    payload_size = 0;
    if (!portal_active && retained.bursts % 10 == 0) flash_led(3);
    if (portal_active) {
      sleep_after_cycle = false;
    } else {
      retained.next_radio_us = cycle_started_us + uint64_t(settings.period_ms) * 1000ULL;
      sleep_after_cycle = true;
    }
    if constexpr (LIGHT_SLEEP_MODE) {
      if (!portal_active) {
        sleep_after_cycle = false;
        radio_cycle_due = false;
        set_phase(RadioPhase::IDLE);
        return;
      }
    }
    set_phase(RadioPhase::DISABLING);
    ble->disable();
  }
}

inline void initialize(esphome::esp32_ble::ESP32BLE *radio) {
  ble = radio;
  ble->add_gap_event_callback(gap_event);
  static PortalHandler handler;
  esphome::web_server_base::global_web_server_base->add_handler(&handler);
  phase_started_ms = millis();
  if constexpr (LIGHT_SLEEP_MODE) {
    configure_light_sleep(!portal_active);
    ESP_LOGW("mybm", "Experimental automatic light sleep; BLE retained between bursts");
  }
}

inline bool measurement_due() {
  std::lock_guard<std::mutex> lock(state_mutex);
  return !sleep_after_portal && clock_us() >= retained.next_measurement_us;
}

inline void measure(float adc_voltage, float chip_temperature) {
  std::lock_guard<std::mutex> lock(state_mutex);
  initial_measurement_pending = false;
  const uint64_t now_us = clock_us();
  retained.next_measurement_us = now_us + uint64_t(settings.measurement_interval_s) * 1000000ULL;
  if (!retained.sample.update(scale_adc_voltage(adc_voltage), chip_temperature, settings.factor)) {
    error = "Invalid voltage/temperature sample; retaining last valid measurement";
    ESP_LOGE("mybm", "%s", error.c_str());
    return;
  }
  uncalibrated_voltage = retained.sample.raw_voltage;
  voltage = uncalibrated_voltage * settings.factor;
  temperature = retained.sample.temperature;
  retained.history.add(now_us, voltage, temperature, settings);
  voltage_portal.observe(voltage, settings.portal_voltage, now_us);
  if (voltage_portal.active && !portal_active) {
    wake_included_portal = true;
    portal_active = true;
    wifi_enable_pending = true;
    portal_stop_requested = false;
    radio_cycle_due = false;
    sleep_after_cycle = false;
    sleep_after_portal = false;
  }
  error.clear();
}

inline void record_measurement_time(uint64_t started_us) {
  wake_timing.measured = true;
  wake_timing.measurement_us += timing_us() - started_us;
}

inline void tick() {
  std::lock_guard<std::mutex> lock(state_mutex);
  const uint32_t now = millis();
  if constexpr (LIGHT_SLEEP_MODE) {
    if (!portal_active && !sleep_after_portal && clock_us() >= retained.next_radio_us)
      radio_cycle_due = true;
  }
  const bool button_down = gpio_get_level(GPIO_NUM_9) == 0;
  retained.next_button_us = next_button_check_us(clock_us(), settings.button_interval_s);
  if (!button_down) {
    if (button_press_active) {
      if (!retained.button_blocked &&
          !portal_active && static_cast<uint32_t>(now - button_pressed_ms) < 3000U) {
        portal_active = true;
        wake_included_portal = true;
        portal_started_ms = now;
        timed_portal_active = true;
        portal_stop_requested = false;
        wifi_enable_pending = true;
        radio_cycle_due = false;
        sleep_after_cycle = false;
        sleep_after_portal = false;
      }
      button_press_active = false;
      gpio_set_level(GPIO_NUM_15, 1);
      led_off_ms = 0;
    }
    retained.button_blocked = false;
  } else if (!retained.button_blocked) {
    if (!button_press_active) {
      button_press_active = true;
      button_pressed_ms = now;
      flash_led(3000);
    } else if (static_cast<uint32_t>(now - button_pressed_ms) >= 3000U) {
      retained.button_blocked = true;
      gpio_set_level(GPIO_NUM_15, 1);
      led_off_ms = 0;
    }
  }
  if (led_off_ms && static_cast<int32_t>(now - led_off_ms) >= 0) {
    gpio_set_level(GPIO_NUM_15, 1);
    led_off_ms = 0;
  }
  if (portal_active && static_cast<uint32_t>(now - led_last_flash_ms) >= 2000U) {
    flash_led(10);
    led_last_flash_ms = now;
  }
  if (timed_portal_active &&
      (portal_remaining_ms(now, portal_started_ms, settings.portal_minutes) == 0 ||
       (portal_stop_requested && static_cast<uint32_t>(now - portal_stop_requested_ms) >= 1000U)))
    timed_portal_active = false;
  if (portal_active && !timed_portal_active && !voltage_portal.active) {
    portal_active = false;
    wifi_disable_pending = true;
    sleep_after_portal = true;
    radio_cycle_due = false;
    retained.next_radio_us = clock_us() + uint64_t(settings.period_ms) * 1000ULL;
    retained.next_button_us = next_button_check_us(clock_us(), settings.button_interval_s);
  }
  if (portal_active) {
    if (phase == RadioPhase::ADVERTISING) {
      set_phase(RadioPhase::STOPPING);
      const auto result = esp_ble_gap_stop_advertising();
      if (result != ESP_OK) fail("BLE stop for portal rejected", result);
    } else if (phase == RadioPhase::IDLE && ble->is_active()) {
      sleep_after_cycle = false;
      set_phase(RadioPhase::DISABLING);
      ble->disable();
    }
    if (phase != RadioPhase::DISABLING) return;
  }
  if (!portal_active && !sleep_after_portal && phase == RadioPhase::IDLE && radio_cycle_due &&
      !payload_size) {
    cycle_started_us = clock_us();
    if (!retained.sample.valid ||
        !advertisement(voltage, next_wake_counter(retained.wake_counter), payload, payload_size)) {
      error = "No valid ADC measurement; advertisement skipped";
      ESP_LOGE("mybm", "%s", error.c_str());
      retained.next_radio_us = cycle_started_us + uint64_t(settings.period_ms) * 1000ULL;
      radio_cycle_due = false;
    } else {
      wake_timing.radio_requested = true;
      wake_timing.radio_requested_us = timing_us();
      phase_started_ms = millis();
      ble->enable();
    }
  }
  if (!portal_active && !radio_cycle_due && !sleep_after_portal &&
      !button_press_active && radio_shutdown_confirmed())
    enter_deep_sleep_until(retained.next_radio_us);
  if (phase == RadioPhase::IDLE && payload_size && ble->is_active()) {
    wake_timing.radio_ready_us = timing_us();
    set_phase(RadioPhase::CONFIGURING);
    const auto result = esp_ble_gap_config_adv_data_raw(payload.data(), payload_size);
    if (result != ESP_OK) fail("BLE payload rejected", result);
  } else if (phase == RadioPhase::ADVERTISING &&
             static_cast<uint32_t>(now - phase_started_ms) >= settings.burst_ms) {
    set_phase(RadioPhase::STOPPING);
    const auto result = esp_ble_gap_stop_advertising();
    if (result != ESP_OK) fail("BLE stop rejected", result);
  } else if (phase == RadioPhase::DISABLING &&
             esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED &&
             esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    payload_size = 0;
    if (sleep_after_cycle) {
      wake_timing.radio_disabled_us = timing_us();
      wake_timing.radio_completed = true;
    }
    set_phase(RadioPhase::IDLE);
    if (sleep_after_cycle && !portal_active) {
      sleep_after_cycle = false;
      radio_cycle_due = false;
      if (!button_press_active) enter_deep_sleep_until(retained.next_radio_us);
      return;
    }
    if (portal_active || sleep_after_portal) return;
    retained.next_radio_us = clock_us() + uint64_t(settings.period_ms) * 1000ULL;
    radio_cycle_due = false;
    if (!button_press_active) enter_deep_sleep_until(retained.next_radio_us);
  } else if (phase == RadioPhase::IDLE && payload_size && !ble->is_active() &&
             static_cast<uint32_t>(now - phase_started_ms) >= 5000U) {
    fail("BLE startup timed out", -1);
  } else if (phase != RadioPhase::IDLE && phase != RadioPhase::ADVERTISING &&
             static_cast<uint32_t>(now - phase_started_ms) >= 5000U) {
    if (phase != RadioPhase::DISABLING) fail("BLE asynchronous operation timed out", static_cast<int>(phase));
    else {
      ESP_LOGE("mybm", "BLE shutdown timed out; restarting device");
      esp_restart();
    }
  }
}

inline void wait_between_cycles() {
  if constexpr (!LIGHT_SLEEP_MODE) return;
  uint64_t deadline_us;
  {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (light_sleep_allowed == portal_active) configure_light_sleep(!portal_active);
    if (portal_active || sleep_after_portal || wifi_enable_pending || wifi_disable_pending ||
        button_press_active || radio_cycle_due || payload_size || phase != RadioPhase::IDLE)
      return;
    deadline_us = next_sleep_wake_us(retained.next_button_us, retained.next_measurement_us,
                                     retained.next_radio_us);
    if (led_off_ms) {
      const int32_t remaining_ms = static_cast<int32_t>(led_off_ms - millis());
      if (remaining_ms <= 0) return;
      deadline_us = std::min(deadline_us, clock_us() + uint64_t(remaining_ms) * 1000ULL);
    }
  }
  // Blocking only the main task lets ESP-IDF idle the CPU and BLE modem.
  // Bound each wait to feed the watchdog without changing the USER deadline.
  while (true) {
    const uint64_t now_us = clock_us();
    if (now_us >= deadline_us) break;
    const uint64_t remaining_ms = (deadline_us - now_us) / 1000ULL;
    if (remaining_ms == 0) break;
    esphome::App.feed_wdt();
    delay(static_cast<uint32_t>(std::min<uint64_t>(remaining_ms, 1000ULL)));
  }
}

}  // namespace mybm
