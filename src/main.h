#include <Arduino.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <stdint.h>
#include <string>
#include <Update.h>
#include <WiFi.h>
#include <WebServer.h>

// Logging
// #define MAIN_DEBUG
#define TAG "MAIN"

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
#define PREFERENCES_REC_PIN_TX "pin_tx"
#define PREFERENCES_REC_PIN_RX "pin_rx"
#define PREFERENCES_REC_PIN_PPM "pin_ppm"
#define PREFERENCES_REC_PIN_KEY "pin_boot"
#define PREFERENCES_REC_PIN_LED "pin_led"
#define PREFERENCES_REC_LED_MODE "led_mode"

// Modes
#define MODE_BLE 0
#define MODE_WEB 1

// Const values
#define IP_ADDRESS 10,0,0,0
#define VENDOR "SkyDevices.ru"
#define MODEL "BLE Telemetry Lite"
#define FIRMWARE "0.2.1"

// Protocol
#define PING_PACKET_ID 0x28
#define MIN_PAYLOAD_SIZE 4

// Default values
#define DEFAULT_SERIAL_BAUDRATE 115200
#define DEFAULT_DOMAIN_NAME "BLE Telemetry Lite"
#define DEFAULT_PASSWORD "12345678"
#define DEFAULT_PROTOCOL "UDP"
#define DEFAULT_PORT 14550
#define DEFAULT_BLE_LOW_PWR ESP_PWR_LVL_P3
#define DEFAULT_BLE_HIGH_PWR ESP_PWR_LVL_P9

// HTML pages
const char *indexHtml = R"literal(
<!DOCTYPE html>
<body style='width:480px'>
  <h2>Firmware Update</h2>
  <form method='POST' enctype='multipart/form-data' id='upload-form'>
    <input type='file' id='file' name='update'>
    <input type='submit' value='Update'>
  </form>
  <br>
  <div id='prg' style='width:0;color:white;text-align:center'>0%</div>
</body>
<script>
var prg = document.getElementById('prg');
var form = document.getElementById('upload-form');
form.addEventListener('submit', el=>{
  prg.style.backgroundColor = 'blue';
  el.preventDefault();
  var data = new FormData(form);
  var req = new XMLHttpRequest();
  var fsize = document.getElementById('file').files[0].size;
  req.open('POST', '/update?size=' + fsize);
  req.upload.addEventListener('progress', p=>{
    let w = Math.round(p.loaded/p.total*100) + '%';
      if(p.lengthComputable){
         prg.innerHTML = w;
         prg.style.width = w;
      }
      if(w == '100%') prg.style.backgroundColor = 'black';
  });
  req.send(data);
 });
</script>
)literal";
