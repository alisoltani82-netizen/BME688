# BME688 Environmental Monitor for ESP32-C3

This repository contains a PlatformIO project for an ESP32-C3 SuperMini connected to a BME688 gas sensor and a 240x240 GC9A01 round SPI display.

The `main` branch is the baseline environmental monitor implementation. It uses the Adafruit BME680 library stack, estimates an air-quality score from gas resistance and humidity, and renders a circular gauge UI on the display.

## Features

- Temperature, humidity, pressure, altitude, and gas-resistance readings
- Heuristic IAQ-style score in the range `0..500`
- Estimated CO2 value derived from gas resistance
- 5-minute gas-baseline calibration on startup
- Trend arrows for temperature, humidity, and IAQ
- Circular GC9A01 gauge UI optimized for the round display
- Serial monitor output for diagnostics and logging
- Retry and sensor reinitialization logic if readings fail repeatedly

## Hardware

- ESP32-C3 SuperMini
- BME688 sensor
- GC9A01A 240x240 round TFT display

## Wiring

### BME688 (software SPI)

- `VCC` -> `3.3V`
- `GND` -> `GND`
- `SCK` -> `GPIO4`
- `SDI` -> `GPIO6` (`MOSI`)
- `SDO` -> `GPIO5` (`MISO`)
- `CS` -> `GPIO7`

### GC9A01A display (SPI)

- `VCC` -> `3.3V`
- `GND` -> `GND`
- `SCL` -> `GPIO2`
- `SDA` -> `GPIO3`
- `DC` -> `GPIO8`
- `CS` -> `GPIO9`
- `RST` -> `GPIO10`

## Software Stack

- PlatformIO
- Arduino framework
- `espressif32`
- Adafruit BME680 Library
- Adafruit Unified Sensor
- Adafruit GFX Library
- Adafruit GC9A01A

The active PlatformIO environment is defined in [platformio.ini](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/platformio.ini).

## How It Works

The firmware reads the BME688 using the Adafruit library and computes a simple air-quality score from:

- gas resistance
- humidity
- a rolling startup baseline

This is not Bosch BSEC. It is a heuristic approach that is simpler to build and easier to understand, but it does not provide the trained gas-classification outputs available in the BSEC-based branch.

### Baseline calibration

For the first 5 minutes after startup, the firmware averages gas-resistance readings to establish a baseline.

During this period:

- the display shows calibration status
- serial output reports that calibration is still in progress

After calibration, the baseline is treated as stable and used for the IAQ-style score and estimated CO2 calculations.

## Sensor Configuration

The current firmware configures the BME688 with:

- temperature oversampling: `8x`
- humidity oversampling: `2x`
- pressure oversampling: `4x`
- IIR filter size: `3`
- gas heater: `320 C` for `150 ms`

These settings are applied in [src/main.cpp](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/main.cpp).

## Display Layout

The round display shows:

- a large IAQ score in the center
- a colored circular gauge for IAQ severity
- textual air-quality rating such as `Excellent`, `Good`, or `Moderate`
- temperature and humidity with trend arrows
- estimated CO2, gas resistance, altitude, and pressure
- calibration status at the bottom

## Serial Output

Each successful reading prints a block similar to:

```text
--- Sensor Reading ---
Temperature: 28.6 C
Humidity: 41.2 %
Pressure: 1012.8 hPa
Altitude: 4.3 m
Gas Resistance: 52.7 kOhm
IAQ Score: 63 (Good)
Est. CO2: 812 ppm
Baseline Calibrated: Yes
```

## Build And Upload

From the project root:

```powershell
pio run
pio run -t upload
pio device monitor -b 115200
```

## Project Structure

- [src/main.cpp](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/main.cpp)
  Main firmware for the GC9A01 dashboard and heuristic air-quality calculations.

- [platformio.ini](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/platformio.ini)
  Active PlatformIO configuration for the current branch.

- [platformio_basic.ini.example](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/platformio_basic.ini.example)
  Alternative example configuration kept in the repository.

- [src/main_basic.cpp.example](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/main_basic.cpp.example)
  Simpler example source variant.

- [src/main_adafruit.cpp.bak](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/main_adafruit.cpp.bak)
- [src/main_normal.cpp.bak](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/main_normal.cpp.bak)
- [src/main_ssd1306.cpp.bak](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/main_ssd1306.cpp.bak)
  Archived source variants retained in the repo.

## Notes

- This branch uses the Adafruit driver path and does not depend on Bosch BSEC.
- The IAQ and CO2 values are derived estimates, not certified measurements.
- Gas readings may be zero during the first few measurement cycles while the gas heater warms up.
- If the sensor fails repeatedly, the firmware attempts to reinitialize it automatically.

## Related Branch

The `feature/bsec2-gas-classification` branch contains a separate implementation that uses Bosch BSEC2 for trained gas classification on the same hardware platform.