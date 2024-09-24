#include "Arduino.h"

#include <Preferences.h>
#include <stdint.h>
#include <string>

// Hardware
#define SERIAL_PORT 1
#define SERIAL_MODE SERIAL_8N1
#define SERIAL_PIN_RX 3
#define SERIAL_PIN_TX 4
#define SERIAL_BUFFER_LENGTH 64
#define LED_PIN 8
#define BOOT_PIN 9

// Preferences
#define PREFERENCES_NAME "skydevices"
#define PREFERENCES_REC_DOMAIN_NAME "domain_name"
#define PREFERENCES_REC_SERIAL_BAUDRATE "serial_baudrate"
#define PREFERENCES_REC_PASSWORD "password"
#define PREFERENCES_REC_PROTOCOL "protocol"
#define PREFERENCES_REC_PORT "port"
#define PREFERENCES_REC_MODE "mode"

// Const values
#define IP_ADDRESS 10,0,0,0
#define VENDOR "SkyDevices.ru"
#define MODEL "BLE Telemetry Lite"
#define FIRMWARE "0.2.0"

// Default values
#define DEFAULT_SERIAL_BAUDRATE 115200
#define DEFAULT_DOMAIN_NAME "BLE Telemetry Lite"
#define DEFAULT_PASSWORD "12345678"
#define DEFAULT_PROTOCOL "UDP"
#define DEFAULT_PORT 14550

#include "esp_attr.h"
