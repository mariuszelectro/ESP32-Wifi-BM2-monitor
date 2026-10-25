#pragma once

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include "esphome/components/http_request/http_request.h"

inline bool wallbox_ntfy_low_voltage(float voltage) {
  return std::isfinite(voltage) && voltage > 5.0f && voltage < 11.9f;
}

inline uint32_t wallbox_ntfy_window_remaining(uint32_t now, uint32_t started) {
  const uint32_t elapsed = now - started;
  return elapsed < 30000U ? 30000U - elapsed : 0U;
}

inline bool wallbox_ntfy_server_valid(const std::string &server) {
  if (server.size() > 160 || server.compare(0, 8, "https://") != 0) return false;
  std::string host = server.substr(8);
  if (!host.empty() && host.back() == '/') host.pop_back();
  const size_t colon = host.find(':');
  if (colon != std::string::npos) {
    const std::string port = host.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned number = 0;
    for (const unsigned char c : port) {
      if (c < '0' || c > '9') return false;
      number = number * 10 + c - '0';
    }
    if (number == 0 || number > 65535) return false;
    host.resize(colon);
  }
  if (host.empty() || host.front() == '.' || host.back() == '.' || host.find("..") != std::string::npos)
    return false;
  for (const unsigned char c : host) {
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '.' || c == '-')) return false;
  }
  return true;
}

inline bool wallbox_ntfy_identifier(const std::string &value, size_t maximum, bool allow_empty = false) {
  if ((!allow_empty && value.empty()) || value.size() > maximum) return false;
  for (const unsigned char c : value) {
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '_' || c == '-')) return false;
  }
  return true;
}

inline std::string wallbox_ntfy_send(esphome::http_request::HttpRequestComponent *http,
                                     const std::string &topic, const std::string &token,
                                     const std::string &message, float voltage, bool test_message,
                                     const std::string &server = "https://ntfy.sh") {
  if (!wallbox_ntfy_server_valid(server) ||
      !wallbox_ntfy_identifier(topic, 64) || !wallbox_ntfy_identifier(token, 128, true) ||
      message.empty() || message.size() > 240) {
    ESP_LOGE("ntfy", "Invalid server, topic, token or message; notification not sent");
    return "Blad: niepoprawny serwer, topic, token lub tresc.";
  }
  if (!test_message && !wallbox_ntfy_low_voltage(voltage)) {
    ESP_LOGE("ntfy", "BM2 voltage outside alarm range; notification not sent");
    return "Blad: napiecie BM2 poza zakresem alarmu.";
  }
  std::string body = test_message ? "Test ntfy. " + message : message;
  if (std::isfinite(voltage) && voltage > 5.0f && voltage < 30.0f) {
    char measured[64];
    snprintf(measured, sizeof(measured), " Napiecie BM2: %.2f V.", voltage);
    body += measured;
  } else if (test_message) {
    body += " Brak aktualnego pomiaru BM2.";
  }
  std::vector<esphome::http_request::Header> headers{
      {"Title", "Wallbox BM2"}, {"Priority", test_message ? "default" : "high"},
      {"Tags", test_message ? "test_tube,battery" : "warning,battery"},
      {"Content-Type", "text/plain; charset=utf-8"}};
  if (!token.empty()) headers.push_back({"Authorization", "Bearer " + token});
  const std::string base = server.back() == '/' ? server.substr(0, server.size() - 1) : server;
  auto response = http->post(base + "/" + topic, body, headers);
  if (response == nullptr) {
    ESP_LOGE("ntfy", "Notification failed: HTTP/TLS transport error");
    return "Blad HTTP/TLS: wiadomosc nie zostala wyslana.";
  }
  const int status = response->status_code;
  response->end();
  if (!esphome::http_request::is_success(status)) {
    ESP_LOGE("ntfy", "Notification rejected: HTTP %d", status);
    return "Blad ntfy: HTTP " + std::to_string(status);
  }
  ESP_LOGI("ntfy", "Notification sent: HTTP %d", status);
  return test_message ? "Wiadomosc testowa wyslana." : "Alarm niskiego napiecia wyslany.";
}
