#pragma once

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <stdint.h>
#include <string>
#include <Update.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "crc.h"

// Logging
// #define MAIN_DEBUG
#define TAG "MAIN"

// Hardware
#define SERIAL_PORT 1

#ifdef BOARD_ESP32C3
#define SERIAL_PIN_RX 3
#define SERIAL_PIN_TX 4
#define BOOT_PIN 9
#define LED_PIN 8
#endif

#ifdef BOARD_ESP32S3
#define SERIAL_PIN_RX 16
#define SERIAL_PIN_TX 17
#endif

// Preferences
#define PREFERENCES_NAME "skydevices"
#define PREFERENCES_REC_DOMAIN_NAME "domain_name"
#define PREFERENCES_REC_SERIAL_BAUDRATE "serial_baudrate"
#define PREFERENCES_REC_MODE "mode"

// Modes
#define MODE_BLE 0
#define MODE_WEB 1

// Const values
#define VENDOR "SkyDevices.ru"
#define MODEL "BLE Telemetry Lite"
#define FIRMWARE "0.4.1"

// CRSF Protocol
#define CRSF_ADDRESS_RADIO 0xEA
#define CRSF_ADDRESS_RX 0xEC
#define CRSF_ADDRESS_TX 0xEE
#define CRSF_SYNC_BYTE 0xC8
#define CRSF_PING_PACKET_ID 0x28
#define CRSF_RC_SYNC_PACKET_ID 0x3A
#define CRSF_MAX_PACKET_SIZE 64
#define CRSF_MIN_PAYLOAD_SIZE 3
#define CRSF_MAX_PAYLOAD_SIZE 62
#define CRSF_CRC_POLY 0xD5
const uint8_t EMPTY_LINK_STATS_PACKET[] = {0xEA, 0x0C, 0x14, 0x78, 0x78, 0x00, 0xEC, 0x00, 0x07, 0x02, 0x78, 0x00, 0xEC, 0x90};
#define EMPTY_LINK_STATS_PACKET_SIZE 14

// Default values
#define DEFAULT_SERIAL_BAUDRATE 115200
#define DEFAULT_DOMAIN_NAME "BLE Telemetry Lite"
#define DEFAULT_PASSWORD "12345678"
#define DEFAULT_BLE_LOW_PWR ESP_PWR_LVL_P3
#define DEFAULT_BLE_HIGH_PWR ESP_PWR_LVL_P9
#define DEFAULT_WEB_PORT 80
#define DEFAULT_TIMEOUT_MS 120000
#define DEFAULT_BLE_LINKSTATS_PACKET_PERIOD_MS 250
#define DEFAULT_WIFI_LOW_PWR WIFI_POWER_2dBm
#define DEFAULT_WIFI_HIGH_PWR WIFI_POWER_8_5dBm
