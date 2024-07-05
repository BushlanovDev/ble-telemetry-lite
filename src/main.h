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

// Preferences
#define PREFERENCES_NAME "dronecontrol"
#define PREFERENCES_REC_DOMAIN_NAME "domain_name"
#define PREFERENCES_REC_SERIAL_BAUDRATE "serial_baudrate"


// Default values
#define DEFAULT_SERIAL_BAUDRATE 115200
#define DEFAULT_DOMAIN_NAME "BLE Telemetry Light"