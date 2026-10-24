#pragma once

#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <esp_attr.h>

// Parametry wykresu; nadpisywane z YAML (substitutions -> build_flags).
#ifndef WALLBOX_RING_SAMPLES
#define WALLBOX_RING_SAMPLES 200
#endif
#ifndef WALLBOX_CHART_AVERAGE
#define WALLBOX_CHART_AVERAGE 5
#endif
#ifndef WALLBOX_CHART_MAX_POINTS
#define WALLBOX_CHART_MAX_POINTS (((WALLBOX_RING_SAMPLES - 2) + WALLBOX_CHART_AVERAGE - 1) / WALLBOX_CHART_AVERAGE + 2)
#endif

static constexpr uint32_t WALLBOX_RTC_MAGIC = 0x57424F5D;
static constexpr uint32_t WALLBOX_BM2_AGE_MAGIC = 0x424D3241;
static_assert(WALLBOX_RING_SAMPLES >= 3 && WALLBOX_RING_SAMPLES <= 65535, "ring size out of range");

// Wartosci calkowite: napiecia w mV (div 1000), temperatura w setnych stopnia (div 100).
struct WallboxRing {
  int16_t samples[WALLBOX_RING_SAMPLES];
  uint16_t next;
  uint16_t count;
  int16_t min_value;
  int16_t max_value;
  int16_t last_value;
  uint32_t age_seconds;
  uint32_t sequence;
};

struct WallboxRtcState {
  uint32_t magic;
  uint32_t offline_elapsed_seconds;
  uint32_t wake_after_s;
  uint32_t hop_s;
  uint32_t sample_period_s;
  uint8_t external_power_mode;
  uint8_t sample_pending;
  uint64_t mark_us;
  uint64_t uptime_base_us;
  WallboxRing esp;
  WallboxRing bm2;
  WallboxRing temp;
};

RTC_NOINIT_ATTR WallboxRtcState wallbox_rtc_state;

struct WallboxRtcBm2Age {
  uint32_t magic;
  uint32_t seconds;
};

RTC_NOINIT_ATTR WallboxRtcBm2Age wallbox_rtc_bm2_age;

struct WallboxRtcBm2Stats {
  uint32_t magic;
  uint32_t attempts;
  uint32_t successes;
};

static constexpr uint32_t WALLBOX_BM2_STATS_MAGIC = 0x424D3253;
RTC_NOINIT_ATTR WallboxRtcBm2Stats wallbox_rtc_bm2_stats;

inline void wallbox_rtc_bm2_age_initialize(bool keep_state) {
  if (!keep_state || wallbox_rtc_bm2_age.magic != WALLBOX_BM2_AGE_MAGIC) {
    wallbox_rtc_bm2_age.magic = WALLBOX_BM2_AGE_MAGIC;
    wallbox_rtc_bm2_age.seconds = 0;
  }
}

inline void wallbox_ring_reset(WallboxRing &ring) {
  memset(&ring, 0, sizeof(ring));
  ring.min_value = INT16_MAX;
  ring.max_value = INT16_MIN;
}

inline void wallbox_rtc_initialize(bool keep_state) {
  if (!keep_state || wallbox_rtc_bm2_stats.magic != WALLBOX_BM2_STATS_MAGIC) {
    wallbox_rtc_bm2_stats = {WALLBOX_BM2_STATS_MAGIC, 0, 0};
  }
  if (!keep_state || wallbox_rtc_state.magic != WALLBOX_RTC_MAGIC) {
    memset(&wallbox_rtc_state, 0, sizeof(wallbox_rtc_state));
    wallbox_rtc_state.magic = WALLBOX_RTC_MAGIC;
    wallbox_ring_reset(wallbox_rtc_state.esp);
    wallbox_ring_reset(wallbox_rtc_state.bm2);
    wallbox_ring_reset(wallbox_rtc_state.temp);
  }
}

inline int16_t wallbox_to_int16(float scaled) {
  const long rounded = lroundf(scaled);
  return static_cast<int16_t>(std::max<long>(INT16_MIN, std::min<long>(INT16_MAX, rounded)));
}

inline void wallbox_ring_push(WallboxRing &ring, int16_t value, bool positive_min_only = false) {
  ring.samples[ring.next] = value;
  ring.next = (ring.next + 1) % WALLBOX_RING_SAMPLES;
  if (ring.count < WALLBOX_RING_SAMPLES) ring.count++;
  if ((!positive_min_only || value > 0) && value < ring.min_value) ring.min_value = value;
  if (value > ring.max_value) ring.max_value = value;
  ring.last_value = value;
  ring.age_seconds = 0;
  ring.sequence++;
}

inline void wallbox_rtc_push_esp(float volts) { wallbox_ring_push(wallbox_rtc_state.esp, wallbox_to_int16(volts * 1000.0f), true); }
inline void wallbox_rtc_push_bm2(float volts) {
  wallbox_ring_push(wallbox_rtc_state.bm2, wallbox_to_int16(volts * 1000.0f), true);
  wallbox_rtc_bm2_stats.attempts++;
  if (volts > 0.0f) wallbox_rtc_bm2_stats.successes++;
}
inline void wallbox_rtc_push_temp(float celsius) { wallbox_ring_push(wallbox_rtc_state.temp, wallbox_to_int16(celsius * 100.0f)); }

inline void wallbox_rtc_add_age(uint32_t seconds) {
  wallbox_rtc_state.esp.age_seconds += seconds;
  wallbox_rtc_state.bm2.age_seconds += seconds;
  wallbox_rtc_state.temp.age_seconds += seconds;
  wallbox_rtc_bm2_age.seconds += seconds;
}

inline uint64_t wallbox_uptime_seconds(uint32_t millis_now) {
  return wallbox_rtc_state.uptime_base_us / 1000000ULL + millis_now / 1000ULL;
}

inline const WallboxRing *wallbox_ring_for(int source, int32_t *divisor) {
  switch (source) {
    case 0: *divisor = 1000; return &wallbox_rtc_state.esp;
    case 1: *divisor = 1000; return &wallbox_rtc_state.bm2;
    case 2: *divisor = 100; return &wallbox_rtc_state.temp;
    default: return nullptr;
  }
}

inline void wallbox_append_scaled(std::string &out, int32_t value, int32_t divisor) {
  if (value < 0) {
    out.push_back('-');
    value = -value;
  }
  char fraction[8];
  snprintf(fraction, sizeof(fraction), divisor == 1000 ? "%03d" : "%02d", static_cast<int>(value % divisor));
  out.append(std::to_string(value / divisor));
  out.push_back('.');
  out.append(fraction);
}

inline std::string wallbox_rtc_summary(int source) {
  int32_t divisor = 1;
  const WallboxRing *ring = wallbox_ring_for(source, &divisor);
  if (ring == nullptr) return "{}";
  std::string out;
  out.reserve(96);
  if (ring->sequence == 0) {
    out.append("{\"min\":null,\"max\":null,\"last\":null");
  } else {
    out.append("{\"min\":");
    if (ring->min_value == INT16_MAX) out.append("null");
    else wallbox_append_scaled(out, ring->min_value, divisor);
    out.append(",\"max\":");
    wallbox_append_scaled(out, ring->max_value, divisor);
    out.append(",\"last\":");
    wallbox_append_scaled(out, ring->last_value, divisor);
  }
  out.append(",\"age_s\":");
  out.append(std::to_string(ring->age_seconds));
  out.append(",\"seq\":");
  out.append(std::to_string(ring->sequence));
  out.append(",\"count\":");
  out.append(std::to_string(ring->count));
  out.push_back('}');
  return out;
}

// Pierwsza i ostatnia probka sa dokladne; wnetrze usredniane w grupach po WALLBOX_CHART_AVERAGE
// (grupy rosna, gdy trzeba zmiescic sie w WALLBOX_CHART_MAX_POINTS).
inline std::string wallbox_rtc_plot(int source) {
  int32_t divisor = 1;
  const WallboxRing *ring = wallbox_ring_for(source, &divisor);
  if (ring == nullptr) return "{}";
  const uint32_t count = ring->count;
  const uint32_t oldest = (ring->next + WALLBOX_RING_SAMPLES - count) % WALLBOX_RING_SAMPLES;
  const auto sample_at = [&](uint32_t index) -> int32_t { return ring->samples[(oldest + index) % WALLBOX_RING_SAMPLES]; };

  std::string out;
  out.reserve(32 + WALLBOX_CHART_MAX_POINTS * 8);
  out.append("{\"seq\":");
  out.append(std::to_string(ring->sequence));
  out.append(",\"count\":");
  out.append(std::to_string(count));
  out.append(",\"div\":");
  out.append(std::to_string(divisor));
  out.append(",\"v\":[");

  bool has_value = false;
  const auto append_value = [&](int32_t value) {
    if (has_value) out.push_back(',');
    out.append(std::to_string(value));
    has_value = true;
  };

  if (count >= 1) append_value(sample_at(0));
  if (count > 2) {
    const uint32_t interior = count - 2;
    const uint32_t max_groups = std::max<uint32_t>(1, WALLBOX_CHART_MAX_POINTS > 2 ? WALLBOX_CHART_MAX_POINTS - 2 : 1);
    const uint32_t group = std::max<uint32_t>(std::max<uint32_t>(1, WALLBOX_CHART_AVERAGE), (interior + max_groups - 1) / max_groups);
    for (uint32_t start = 1; start < count - 1; start += group) {
      const uint32_t end = std::min(start + group, count - 1);
      int32_t sum = 0;
      for (uint32_t index = start; index < end; index++) sum += sample_at(index);
      const int32_t n = static_cast<int32_t>(end - start);
      append_value((sum + (sum >= 0 ? n / 2 : -(n / 2))) / n);
    }
  }
  if (count >= 2) append_value(sample_at(count - 1));
  out.append("]}");
  return out;
}
