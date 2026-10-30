#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace mybm {

inline constexpr float DIVIDER_RATIO = 6.0f;

inline float scale_adc_voltage(float adc_voltage) {
  return adc_voltage * DIVIDER_RATIO;
}

struct Settings {
  float factor{1.033f};
  uint32_t period_ms{1000};
  uint32_t burst_ms{50};
  uint32_t history_interval_s{1};
  uint32_t average_count{1};
  uint32_t portal_minutes{5};
  uint32_t button_interval_s{1};
  uint32_t measurement_interval_s{1};
  float portal_voltage{13.3f};
};

inline bool valid_measurement_interval(uint32_t seconds) {
  return seconds == 1 || seconds == 2 || seconds == 5 || seconds == 10 ||
         seconds == 30 || seconds == 60 || seconds == 120;
}

inline bool valid_settings(const Settings &s) {
  return std::isfinite(s.factor) && s.factor >= 0.5f && s.factor <= 1.5f &&
         s.period_ms >= 1000 && s.period_ms <= 60000 && s.burst_ms >= 50 &&
         s.burst_ms <= 500 && s.history_interval_s >= 1 && s.history_interval_s <= 3600 &&
         s.average_count >= 1 && s.average_count <= 60 &&
         s.portal_minutes >= 5 && s.portal_minutes <= 20 &&
         s.button_interval_s >= 1 && s.button_interval_s <= 3600 &&
         valid_measurement_interval(s.measurement_interval_s) &&
         std::isfinite(s.portal_voltage) && s.portal_voltage >= 0.5f && s.portal_voltage <= 30;
}

struct VoltagePortal {
  bool active{false};
  bool below{false};
  uint64_t below_since_us{0};

  void observe(float voltage, float threshold, uint64_t now_us) {
    if (!std::isfinite(voltage)) return;
    if (voltage > threshold) {
      active = true;
      below = false;
    } else if (active) {
      if (!below) {
        below = true;
        below_since_us = now_us;
      } else if (now_us - below_since_us >= 60000000ULL) {
        active = false;
        below = false;
      }
    }
  }
};

inline uint32_t portal_remaining_ms(uint32_t now, uint32_t started, uint32_t minutes) {
  const uint32_t duration = minutes * 60000U;
  const uint32_t elapsed = now - started;
  return elapsed < duration ? duration - elapsed : 0;
}

inline uint64_t next_sleep_wake_us(uint64_t next_button_us, uint64_t next_measurement_us,
                                   uint64_t next_radio_us) {
  return std::min({next_button_us, next_measurement_us, next_radio_us});
}

inline uint64_t next_button_check_us(uint64_t checked_us, uint32_t interval_s) {
  return checked_us + uint64_t(interval_s) * 1000000ULL;
}

struct Sample {
  float raw_voltage{NAN};
  float temperature{NAN};
  bool valid{false};

  bool update(float raw, float chip_temperature, float factor) {
    const float calibrated = raw * factor;
    if (!std::isfinite(raw) || !std::isfinite(calibrated) || calibrated < 0 ||
        calibrated > 30 || !std::isfinite(chip_temperature))
      return false;
    raw_voltage = raw;
    temperature = chip_temperature;
    valid = true;
    return true;
  }
};

inline bool resume_sleep_cycle(bool deep_sleep_reset, bool timer_wake, bool retained_valid) {
  return deep_sleep_reset && timer_wake && retained_valid;
}

inline uint64_t deep_sleep_duration_us(uint64_t now_us, uint64_t wake_us) {
  return wake_us > now_us ? wake_us - now_us : 1000ULL;
}

struct Point {
  uint32_t seconds;
  float voltage;
  float temperature;
};

struct History {
  std::array<Point, 400> points{};
  uint16_t head{0};
  uint16_t count{0};
  uint64_t total_points{0};
  uint32_t pending_count{0};
  double voltage_sum{0};
  double temperature_sum{0};
  uint64_t last_input_us{0};
  bool has_input{false};

  void reset_average() {
    pending_count = 0;
    voltage_sum = temperature_sum = 0;
    has_input = false;
  }
  bool add(uint64_t now_us, float voltage, float temperature, const Settings &settings) {
    if (!std::isfinite(voltage) || !std::isfinite(temperature)) return false;
    if (has_input && now_us - last_input_us < uint64_t(settings.history_interval_s) * 1000000ULL)
      return false;
    has_input = true;
    last_input_us = now_us;
    voltage_sum += voltage;
    temperature_sum += temperature;
    if (++pending_count < settings.average_count) return false;
    points[head] = {static_cast<uint32_t>(now_us / 1000000ULL),
                    static_cast<float>(voltage_sum / pending_count),
                    static_cast<float>(temperature_sum / pending_count)};
    head = (head + 1) % points.size();
    count = std::min<size_t>(count + 1, points.size());
    ++total_points;
    pending_count = 0;
    voltage_sum = temperature_sum = 0;
    return true;
  }
  const Point &at(size_t index) const { return points[(head + points.size() - count + index) % points.size()]; }
};

inline uint32_t next_wake_counter(uint32_t counter) { return counter + 1U; }

inline bool advertisement(float voltage, uint32_t wake_counter,
                          std::array<uint8_t, 31> &payload, uint8_t &size) {
  if (!std::isfinite(voltage) || voltage < 0 || voltage > 30) return false;
  char name[24];
  const int length = std::snprintf(name, sizeof(name), "MyBM:%.3f %010u",
                                   static_cast<double>(voltage),
                                   static_cast<unsigned>(wake_counter));
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(name) || length > 26) return false;
  payload = {};
  payload[0] = 2;
  payload[1] = 0x01;
  payload[2] = 0x06;
  payload[3] = static_cast<uint8_t>(length + 1);
  payload[4] = 0x09;  // Complete Local Name in the advertisement, not a scan response.
  std::copy_n(name, length, payload.begin() + 5);
  size = static_cast<uint8_t>(length + 5);
  return true;
}

enum class RadioPhase { IDLE, CONFIGURING, STARTING, ADVERTISING, STOPPING, DISABLING };

struct WakeTiming {
  uint64_t boot_us{0};
  uint64_t measurement_us{0};
  uint64_t radio_requested_us{0};
  uint64_t radio_ready_us{0};
  uint64_t advertising_started_us{0};
  uint64_t stop_requested_us{0};
  uint64_t advertising_stopped_us{0};
  uint64_t radio_disabled_us{0};
  uint64_t awake_us{0};
  bool measured{false};
  bool radio_requested{false};
  bool radio_completed{false};

  uint64_t startup_us() const { return radio_ready_us - radio_requested_us; }
  uint64_t configuration_us() const { return advertising_started_us - radio_ready_us; }
  uint64_t advertising_us() const { return stop_requested_us - advertising_started_us; }
  uint64_t stopping_us() const { return advertising_stopped_us - stop_requested_us; }
  uint64_t shutdown_us() const { return radio_disabled_us - advertising_stopped_us; }
};

}  // namespace mybm
