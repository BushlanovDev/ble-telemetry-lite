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
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Firmware Configurator</title>
    <style>
        table {width: 100%; border-collapse: collapse;}
        .setting {display: flex; margin-bottom: 5px;}
        .setting input {flex: 1;}
    </style>
</head>
<body style='width:480px'>
<h2>Settings</h2>
<table border='1' id='settings-table'>
    <thead><tr><th>Parameter</th><th>Value</th></tr></thead>
    <tbody></tbody>
</table>
<br>
<div id='settings-inputs'></div>
<br>
<h2>Firmware Update</h2>
<form id='upload-form'>
    <input type='file' id='file' name='update'>
    <input type='submit' value='Update'>
</form>
<br>
<div id='prg' style='width:0;color:white;text-align:center'>0%</div>
<script>
document.addEventListener('DOMContentLoaded', async () => {
    let res = await fetch('/settings');
    let data = await res.json();
    let tableBody = document.querySelector('#settings-table tbody');
    ['vendor', 'model', 'firmware'].forEach(k => data[k] && (tableBody.innerHTML += `<tr><td>${k}</td><td>${data[k]}</td></tr>`));
    let settingsDiv = document.getElementById('settings-inputs');
    ['domain_name', 'serial_baudrate', 'mode'].forEach(k => {
        if (data[k]) settingsDiv.innerHTML += `<div class='setting'><input id='${k}' value='${data[k]}'><button onclick='updateSetting("${k}")'>Save</button></div>`;
    });
});
function updateSetting(k) {
    fetch(`/settings?${k}=` + encodeURIComponent(document.getElementById(k).value), {method: 'POST'});
}
document.getElementById('upload-form').addEventListener('submit', e => {
    e.preventDefault();
    let prg = document.getElementById('prg');
    prg.style.backgroundColor = '';
    prg.style.width = '0%';
    prg.textContent = '0%';
    let req = new XMLHttpRequest();
    req.open('POST', '/update?size=' + document.getElementById('file').files[0].size);
    req.upload.onprogress = p => p.lengthComputable && (prg.textContent = prg.style.width = Math.round((p.loaded / p.total) * 100) + '%', p.loaded === p.total && (prg.style.backgroundColor = 'black'));
    req.send(new FormData(e.target));
});
</script>
</body>
</html>
)literal";
