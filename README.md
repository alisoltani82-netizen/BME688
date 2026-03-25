# BME688 Gas Classification on ESP32-C3

This branch contains an ESP32-C3 + BME688 + GC9A01 round-display project that runs Bosch BSEC2 gas classification and renders the current class, confidence, and environmental values on a 240x240 TFT.

The current firmware uses Bosch's `FieldAir_HandSanitizer` pre-trained classifier and shows these primary outputs:

- `GAS_ESTIMATE_1`: Field Air probability
- `GAS_ESTIMATE_2`: Hand Sanitizer probability

## Hardware

- ESP32-C3 SuperMini
- BME688 gas sensor
- GC9A01A 240x240 round SPI TFT

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
- Bosch BSEC2 library
- Bosch BME68x library
- Adafruit GFX
- Adafruit GC9A01A

The PlatformIO environment is defined in [platformio.ini](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/platformio.ini).

## What This Branch Adds

- BSEC2 scan-mode gas classification on ESP32-C3
- State persistence using `Preferences` so BSEC learning survives reboot
- Automatic model-config copy during build
- Automatic patching of BSEC warning handling during build
- Display UI for class name, confidence, probability bars, and sensor data
- Tight polling loop to avoid BME688 FIFO overrun during scan mode

## Why The Extra Build Scripts Exist

Two pre-build scripts run automatically from [platformio.ini](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/platformio.ini):

- [copy_bsec_config.py](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/copy_bsec_config.py)
  Copies the `FieldAir_HandSanitizer` model blob from the installed BSEC2 library into `src/config/model/bsec_selectivity.txt` so it can be compiled into the firmware.

- [patch_bsec_warning.py](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/patch_bsec_warning.py)
  Patches `bsec2.cpp` in the downloaded BSEC2 library so positive warning codes do not abort `processData()`. Without that patch, scan-mode processing can stop early and classification remains stuck at accuracy `0`.

## Why Classification Was Stuck At Accuracy 0

Two issues had to be fixed for stable classification on the ESP32-C3:

1. BSEC warning handling

The upstream library treated warnings the same as fatal errors inside `processData()`. In scan mode, this caused the readout loop to terminate early and prevented BSEC from receiving a complete heater-profile sequence.

2. FIFO timing

The BME688 scan flow is timing-sensitive. If `bme.run()` is not called often enough, the sensor FIFO can overflow and BSEC misses part of the gas profile. This branch fixes that by:

- polling `bme.run()` as fast as possible in the main loop
- rate-limiting display refreshes
- calling `bme.run()` again immediately after display drawing
- only replacing displayed classification values when BSEC reports `accuracy > 0`

## Project Files

- [src/main.cpp](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/main.cpp)
  Main firmware: display, sensor setup, BSEC subscription, callback handling, and runtime loop.

- [src/model_labels.h](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/model_labels.h)
  Label mapping for gas-estimate channels.

- [src/bsec_model_config.h](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/bsec_model_config.h)
  Includes the compiled BSEC model blob.

- [src/config/model/bsec_selectivity.txt](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/config/model/bsec_selectivity.txt)
  Current classifier blob used by the firmware.

## Build And Upload

From the project root:

```powershell
pio run
pio run -t upload
pio device monitor -b 115200
```

## Serial Output

Typical serial output includes:

- temperature
- humidity
- gas resistance
- BSEC accuracy
- probabilities `P1..P4`

Example:

```text
T:30.4 H:23 G:64951.4k acc:3 P1=100.0 P2=0.0 P3=0.0 P4=0.0
```

`acc:3` means BSEC has produced a calibrated classification result.

## Display Behavior

The GC9A01 screen shows:

- top predicted class
- confidence percentage
- temperature / humidity / gas resistance
- per-class probability bars
- BSEC accuracy state
- scan count and uptime

## Switching Models

To use a different Bosch or AI Studio model:

1. Replace the source model selected in [copy_bsec_config.py](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/copy_bsec_config.py).
2. Update labels in [src/model_labels.h](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/model_labels.h).
3. Rebuild so the new `bsec_selectivity.txt` is copied into the project.

If you use a custom AI Studio export, [src/bsec_model_config.h](/c:/Users/solta/Documents/PlatformIO/Projects/BME688/src/bsec_model_config.h) is already set up to include it from `src/config/model/bsec_selectivity.txt`.

## Notes

- This branch is currently configured for the Bosch `FieldAir_HandSanitizer` classifier.
- The ESP32-C3 uses the RISC-V BSEC binary selected by the library.
- The firmware saves BSEC state periodically to NVS so calibration is preserved between reboots.