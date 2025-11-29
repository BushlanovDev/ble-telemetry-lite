# BLE Telemetry Lite

[English](./README.md) | [Russian](./README_RU.md)

Firmware for **ESP32-C3**, **ESP32-S3**, and similar microcontrollers, enabling telemetry broadcast from Crossfire and ELRS radio systems over Bluetooth or Wi-Fi.

## Features

* Transmits Crossfire/ELRS telemetry from RC transmitters to phones, tablets, or laptops via Bluetooth or Wi-Fi
* Compatible with transmitters using Aux-serial output as well as direct connection to internal modules (e.g. Jumper RC up to T15)
* Telemetry display supported via [TelemetryViewer](https://github.com/RomanLut/android-taranis-smartport-telemetry/releases), [WebTelemetry](http://telemetry.skydevices.ru), or the built-in web interface

## Hardware

Development boards **ESP32-C3-SuperMini** (recommended) or **ESP32-S3-SuperMini**.
Other ESP32-based boards with BLE and Wi-Fi support may also work, but compatibility is untested and not guaranteed.

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/esp32-c3_esp32-s3.jpg?raw=true" width="60%" alt="ESP32-C3-SuperMini & ESP32-S3-SuperMini"/>

These boards are compact, affordable, and free from unnecessary components.
If using a different **ESP32-C3** or **ESP32-S3** board, note that pin numbering may differ from the microcontroller's internal pin numbers.
Refer to your board’s datasheet to identify correct pin mappings.

## Wiring

The module connects to the transmitter’s SerialAUX interface using 4 wires:

* 5V
* GND
* TX → to pin 3 (module RX)
* RX → to pin 4 (module TX) (optional)

Wiring to a RadioMaster Pocket transmitter example

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/esp32-c3-supermini_to_rm_pocket.jpg?raw=true" width="50%" alt="Connection to RadioMaster Pocket" title="Connection to RadioMaster Pocket" />

If the transmitter lacks SerialAUX, connect the ELRS/Crossfire internal module’s TX pin directly to the module’s RX (pin 3).

Connection to internal TX on a Jumper T20 transmitter example

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/esp32-c3-supermini_to_jumper-t20.jpg?raw=true" width="50%" alt="Connection to Jumper T20 internal TX" title="Connection to Jumper T20 internal TX" />

## Flashing and Configuration

### Firmware via web interface

Switch between BLE and Wi-Fi modes by pressing the **Boot** button on the module.

In Wi-Fi mode, connect to the **BLE Telemetry Lite** access point using the password: **12345678**.
The web interface is accessible at [http://192.168.4.1](http://192.168.4.1).

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/web-interface.png?raw=true" alt="Web Interface" title="Web Interface" />

### Firmware via Flash Download Tool

Download [Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32c3/production_stage/tools/flash_download_tool.html), set the settings as shown in the screenshot and don't forget to specify your COM port.

<img src="https://github.com/BushlanovDev/ble-telemetry-lite/blob/main/images/flasher-tool.png?raw=true" alt="Flash Download Tool" />

## License

This repository's source code is available under the [MIT License](LICENSE).
