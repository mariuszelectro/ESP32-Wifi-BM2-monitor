# ESP32 WiFi BM2 Monitor

ESP32 WiFi BM2 Monitor is a low-power autonomous battery monitoring system based on the Seeed Studio XIAO ESP32-C6, a BM2 Bluetooth Battery Monitor, and a Li-Ion 18650 backup battery.

The project was designed to monitor the long-term health of a vehicle battery while minimizing power consumption. It periodically reads the battery voltage from a BM2 monitor via Bluetooth Low Energy (BLE), monitors its own power source, records measurements into RTC memory, and presents all information through a built-in web interface.

The primary goal is to detect slow battery discharge when a vehicle remains parked for extended periods.

---
# Why This Project Exists

This project was created to solve a very practical problem.

When a vehicle remains parked in a garage for weeks or months, the battery can slowly discharge due to standby loads from the vehicle electronics. If the owner is away from home, it is often impossible to know whether the battery is still healthy or whether charging has become necessary.

The ESP32 WiFi BM2 Monitor allows remote verification of battery condition whenever the vehicle is connected to local WiFi.

Typical usage scenario:

```text
Vehicle parked in garage
↓
Battery monitored automatically
↓
Historical voltage trend recorded
↓
Owner accesses local web interface remotely
↓
Battery status evaluated
↓
Battery charger activated if required
```

The system makes it possible to:

- Monitor long-term battery discharge trends
- Detect excessive standby battery drain
- Verify battery condition before returning home
- Decide when charging is required
- Avoid deep battery discharge and battery damage
- Maintain vehicles that are used only occasionally

The focus of the project is not real-time monitoring but long-term observation of battery health with extremely low power consumption.


# Features

- BM2 Bluetooth Battery Monitor integration
- Vehicle battery voltage monitoring
- Internal 18650 battery voltage monitoring
- ESP32-C6 temperature monitoring
- Built-in BLE scanner
- Built-in web server
- RTC memory data logging
- History graphs and statistics
- Ultra low power Deep Sleep operation
- Automatic VBUS detection
- On-device diagnostics and event logging
- Fully autonomous operation
- No Home Assistant required
- No MQTT required
- No Internet connection required

---

# Hardware

- Seeed Studio XIAO ESP32-C6
- BM2 Bluetooth Battery Monitor
- Li-Ion 18650 battery
- Voltage divider for 18650 monitoring
- Voltage divider for VBUS detection
- WiFi 2.4 GHz

---

# Configuration

Before compiling and flashing the firmware, the following values must be configured manually:

## WiFi Settings

Configure your WiFi credentials in Yaml file: section wifi

```cpp
ssid: "YOUR_WIFI_SSID";
password: "YOUR_WIFI_PASSWORD";
```

## BM2 Bluetooth MAC Address

Configure the MAC address of your BM2 monitor: yaml file section ble_client:

```cpp
mac_addres: „xx:xx:xx:xx”
```

Replace it with the address of your own BM2 device.

The built-in BLE scanner can be used to discover nearby Bluetooth devices and determine the correct MAC address.

---

# Built-In BLE Scanner

The device includes a Bluetooth Low Energy scanner capable of:

- Detecting nearby BLE devices
- Discovering BM2 battery monitors
- Displaying RSSI signal strength
- Assisting BLE diagnostics and troubleshooting

This feature is useful during setup and testing.

---

# Temperature Monitoring

The internal ESP32-C6 temperature sensor is monitored continuously.

Temperature information is:

- displayed on the web interface
- logged for diagnostics
- used to monitor operating conditions

---

# Power Management

## VBUS Detection

External power presence is detected through:

```text
GPIO18 (VBUS Detect)
```

The current VBUS status is displayed on the web interface.

---

## External Power Mode

When VBUS is present:

- WiFi is enabled
- Web server is active
- Diagnostics are available
- Measurements are updated continuously
- Live monitoring is available

---

## Battery Mode

When no external power is detected, the system automatically switches to battery-powered operation using the internal 18650 cell.

To minimize power consumption, the ESP32 operates in Deep Sleep mode.

Operation cycle:

```text
Deep Sleep
↓
Wake Up
↓
Read BM2 Voltage
↓
Read 18650 Voltage
↓
Read ESP32 Temperature
↓
Store Measurements
↓
Return To Deep Sleep
```

Measurement intervals are user configurable.

Supported interval range:

```text
1 minute to 24 hours
```

Data history capacity:

```text
200 measurements
```

---

# BM2 Operation

The BM2 monitor is used as a remote voltage sensor.

Measurement sequence:

```text
Connect BLE
↓
Read Voltage
↓
Store Sample
↓
Disconnect BLE
```

The Bluetooth connection remains active only for a few seconds, minimizing power consumption for both the ESP32 and the BM2 monitor.

---

# Data Logging

The device stores:

```text
200 historical measurements
```

Measurements are preserved in RTC memory and survive Deep Sleep wake-up cycles.

Stored information includes:

- Vehicle battery voltage
- Internal battery voltage
- ESP32 temperature
- Timestamp information
- Device operating statistics

---

# Web Interface

The integrated web interface provides:

- Vehicle battery voltage
- Internal 18650 battery voltage
- ESP32 temperature
- BM2 connection state
- VBUS status
- Historical graphs
- Min / Max values
- Latest measurement
- Event logs
- Device diagnostics

All data is available locally through a standard web browser.

---

# Power Consumption

Measured current consumption:

## Deep Sleep

```text
24-25 µA
```

## Active Operation

```text
~70 mA
```

---

# LED Status Indicator

### Deep Sleep Mode

Heartbeat LED:

```text
5 ms pulse every 10 seconds
```

### Active Mode

Heartbeat LED:

```text
30 ms pulse every 2 seconds
```

This allows quick visual identification of device status.

---

# Intended Applications

- Long-term vehicle battery monitoring
- Seasonal vehicles
- Motorhomes and campers
- Trailers
- Fleet monitoring
- Standby battery observation
- Battery self-discharge studies

---

# Project Goal

The purpose of the project is to answer a simple question:

> Is my vehicle battery slowly discharging while the vehicle is parked?

while consuming as little power as possible.

---

# Acknowledgements

This project uses and benefits from the following open-source projects:

- ESPHome
- ESP-IDF
- Arduino ESP32
- BM2 BLE component by Andrew Backway

Special thanks to Andrew Backway for reverse engineering the BM2 Bluetooth protocol and making the ESPHome BM2 integration available to the community.

BM2 BLE component:

https://github.com/andrewbackway/esphome-bm2\_ble

---

# Author

mariuszelectro@gmail.com
# Hardware Connections

## VBUS Detection

External power is detected using GPIO18.

Required voltage divider:

```text
VBUS
 |
100K
 |
 +---- GPIO18
 |
100K
 |
GND
```

Filtering capacitor:

```text
GPIO18
 |
100nF
 |
GND
```

This circuit allows reliable detection of external USB/VBUS power.

---

## Internal Battery Voltage Measurement

The internal 18650 battery voltage is monitored using GPIO0.

Required voltage divider:

```text
Battery+
 |
100K
 |
 +---- GPIO0
 |
100K
 |
GND
```

Filtering capacitor:

```text
GPIO0
 |
100nF
 |
GND
```

ADC measurement accuracy can be calibrated by adjusting the voltage multiplier in the source code.

Default calibration is located near:

```cpp
yaml file (filtres)
```

around line:

```text
260
```

Adjust the multiplier according to measurements performed with a calibrated multimeter.

---

## 18650 Battery Connection

The XIAO ESP32-C6 battery version exposes battery solder pads on the bottom side of the PCB.

The 18650 cell should be connected directly to these battery pads:

```text
BAT+
BAT-
```

Using the onboard charging circuitry allows automatic charging whenever VBUS power is present.
