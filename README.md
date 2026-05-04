![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.1-blue)
![Platform](https://img.shields.io/badge/platform-ESP32-orange)
[![ESP-IDF Build](https://github.com/sxpl-DavidSchmidt/plantbased/actions/workflows/esp-idf-build.yml/badge.svg)](https://github.com/sxpl-DavidSchmidt/plantbased/actions/workflows/esp-idf-build.yml)
[![C/C++ Linter](https://github.com/sxpl-DavidSchmidt/plantbased/actions/workflows/cpp-linter.yml/badge.svg)](https://github.com/sxpl-DavidSchmidt/plantbased/actions/workflows/cpp-linter.yml)

# 🌱 Plantbased

**Plantbased** is an ESP32-based project designed to monitor and report key environmental metrics relevant to plant health. It acts as a configurable slave device that collects sensor data and communicates it to a master device for further processing or visualization.

> ⚠️ This project is still under development.

---

## ✨ Features

- 📡 ESP32-based implementation
- 🌡️ Measurement of plant-relevant metrics such as:
  - Temperature
  - Humidity
  - Soil moisture
  - (planned) Light intensity
- 🔌 Configurable as a slave device
- 🔄 Communication with a master device (e.g., via I2C, UART, or WiFi – depending on configuration)
- ⚙️ Modular and extensible design
