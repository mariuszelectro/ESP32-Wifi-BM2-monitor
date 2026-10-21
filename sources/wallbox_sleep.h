#pragma once

#include <algorithm>
#include <cstdint>

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_private/esp_clk.h>

#include "esphome/components/safe_mode/safe_mode.h"
#include "esphome/core/preferences.h"
#include "offline_rtc.h"

static constexpr uint32_t WALLBOX_SLEEP_HOP_S = 10;

// Splits a sleep of total_s seconds into hops; returns the first hop and stores the rest in RTC.
inline uint32_t wallbox_plan_sleep(uint32_t total_s) {
  const uint32_t total = std::max<uint32_t>(total_s, 1);
  const uint32_t hop = std::min(total, WALLBOX_SLEEP_HOP_S);
  wallbox_rtc_state.hop_s = hop;
  wallbox_rtc_state.wake_after_s = total - hop;
  return hop;
}

// Next hop of an already planned sleep; wake_after_s must be > 0.
inline uint32_t wallbox_next_hop() {
  return wallbox_plan_sleep(wallbox_rtc_state.wake_after_s);
}

// LED is active low; drive it off before sleep (C6 has no digital pad hold in deep sleep).
inline void wallbox_hold_led_off(int led_gpio) {
  gpio_set_level(static_cast<gpio_num_t>(led_gpio), 1);
}

inline void wallbox_release_led_hold(int) {}

// Sleeps without bringing up Wi-Fi; safe-mode counter is cleared as in a regular shutdown.
[[noreturn]] inline void wallbox_sleep_now(esphome::safe_mode::SafeModeComponent *safe_mode, uint32_t seconds,
                                           int led_gpio) {
  safe_mode->on_safe_shutdown();
  esphome::global_preferences->sync();
  wallbox_hold_led_off(led_gpio);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  esp_deep_sleep_start();
  while (true) {
  }
}
