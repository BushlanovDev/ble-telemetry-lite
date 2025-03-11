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

// Logging
// #define MAIN_DEBUG
#define TAG "MAIN"

// Hardware
#define SERIAL_PORT 1
#define LED_PIN 8

#ifdef BOARD_ESP32C3
#define SERIAL_PIN_RX 3
#define SERIAL_PIN_TX 4
#define BOOT_PIN 9
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
#define FIRMWARE "0.4.0"

// CRSF Protocol
#define CRSF_ADDRESS_RADIO_TRANSMITTER 0xEA
#define CRSF_PING_PACKET_ID 0x28
#define CRSF_RC_SYNC_PACKET_ID 0x3A
#define CRSF_MIN_PACKET_SIZE 5
#define CRSF_MAX_PACKET_SIZE 64
#define CRSF_MAX_PAYLOAD_SIZE 60
const uint8_t EMPTY_LINK_STATS_PACKET[] = {0xea, 0x0c, 0x14, 0x78, 0x78, 0x00, 0xec, 0x00, 0x07, 0x02, 0x78, 0x00, 0xec, 0x90};
#define EMPTY_LINK_STATS_PACKET_SIZE 14

// Default values
#define DEFAULT_SERIAL_BAUDRATE 115200
#define DEFAULT_DOMAIN_NAME "BLE Telemetry Lite"
#define DEFAULT_PASSWORD "12345678"
#define DEFAULT_BLE_LOW_PWR ESP_PWR_LVL_P3
#define DEFAULT_BLE_HIGH_PWR ESP_PWR_LVL_P9
#define DEFAULT_WEB_PORT 80
#define DEFAULT_TIMEOUT_MS 120000  // 120 seconds
#define DEFAULT_BLE_LINKSTATS_PACKET_PERIOD_MS 200

// HTML pages
const char *indexHtml = R"literal(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>BLE Telemetry Lite</title>
<style>
* {margin: 0; padding: 0; box-sizing: border-box; font-family: Arial, sans-serif;}
body {width: 100%; max-width: 480px; margin: auto; background: #121212; color: white; padding: 20px;}
h2 {color: #00ff99; text-align: center; margin: 20px 0;}
table {width: 100%; border-collapse: collapse; background: #1e1e1e;}
th, td {border: 1px solid #333; padding: 10px; text-align: left;}
.setting {display: flex; gap: 5px; margin-bottom: 5px;}
input, button {padding: 8px; border: none; outline: none;}
input {flex: 1; background: #222; color: white;}
button {background: #00ff99; color: black; cursor: pointer;}
button:hover {opacity: 0.8;}
form {text-align: center; margin-top: 10px;}
#prg {width: 0; height: 20px; background: #00ff99; text-align: center; display: none;}
.tabs {display: flex; justify-content: space-around; margin-bottom: 10px;}
.tab {padding: 10px; cursor: pointer; background: #222; color: white; border-radius: 5px;}
.tab.active {background: #00ff99; color: black;}
.tab-content {display: none;}
.tab-content.active {display: block;}
textarea {width: 100%; height: 200px; background: #222; color: white; margin-top: 10px;}
</style>
</head>
<body>
<div class="tabs">
<div class="tab active" onclick="showTab('settings')">Settings</div>
<div class="tab" onclick="showTab('update')">Update</div>
<div class="tab" onclick="showTab('telemetry')">Telemetry</div>
</div>
<div id="settings" class="tab-content active">
<h2>Settings</h2>
<table>
<thead><tr><th>Parameter</th><th>Value</th></tr></thead>
<tbody id='settings-table'></tbody>
</table>
<br>
<div id='settings-inputs'></div>
</div>
<div id="update" class="tab-content">
<h2>Firmware Update</h2>
<form id='upload-form'>
<input type='file' id='file'>
<button type='submit' id='update-btn'>Update</button>
</form>
<div id='prg'></div>
</div>
<div id="telemetry" class="tab-content">
<h2>Telemetry</h2>
<table>
<thead><tr><th>Sensor</th><th>Value</th></tr></thead>
<tbody id="telemetry-table"></tbody>
</table>
<br>
<button onclick="startWebSocket()">Connect</button>
<textarea id="log" readonly></textarea>
</div>
<script>
function showTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.tab').forEach(el => el.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    document.querySelector(`.tab[onclick="showTab('${tabId}')"]`).classList.add('active');
}
document.addEventListener('DOMContentLoaded', async () => {
    let res = await fetch('/settings');
    let data = await res.json();
    let tableBody = document.querySelector('#settings-table');
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
    let updateBtn = document.getElementById('update-btn');
    updateBtn.disabled = true;
    prg.style.width = '0%';
    prg.textContent = '0%';
    prg.style.display = 'block';
    let req = new XMLHttpRequest();
    req.open('POST', '/update?size=' + document.getElementById('file').files[0].size);
    req.upload.onprogress = p => {
        if (p.lengthComputable) {
            let percent = Math.round((p.loaded / p.total) * 100) + '%';
            prg.textContent = percent;
            prg.style.width = percent;
        }
    };
    req.onload = req.onerror = () => updateBtn.disabled = false;
    req.send(new FormData(e.target));
});
function startWebSocket() {
    let socket = new WebSocket("ws://192.168.4.1/ws");
    let log = document.getElementById('log');
    let telemetryTable = document.getElementById('telemetry-table');
    socket.binaryType = "arraybuffer";
    socket.onmessage = function (e) {
        if (e.data instanceof ArrayBuffer) {
            let bytes = new Uint8Array(e.data);
            let hexString = Array.from(bytes, byte => '0x' + byte.toString(16).padStart(2, '0')).join(' ');
            log.value += hexString + "\n";
        } else {
            log.value += e.data.toString() + "\n";
        }
    };
}
</script>
</body>
</html>
)literal";
