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

// HTML pages
const char *indexHtml = R"literal(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>BLE Telemetry Lite</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
* {margin: 0; padding: 0; box-sizing: border-box; font-family: Arial, sans-serif;}
body {width: 100%; max-width: 480px; margin: auto; background: #121212; color: white; padding: 20px;}
@media (max-width: 600px) { body {max-width: 100%; padding: 10px;} }
h2 {color: #00ff99; text-align: center; margin: 20px 0;}
table {width: 100%; border-collapse: collapse; background: #1e1e1e;}
th, td {border: 1px solid #333; padding: 10px; text-align: left;}
.setting {display: flex; gap: 5px; margin-bottom: 10px;}
input, button, select {padding: 8px; border: none; outline: none;}
input, select {flex: 1; background: #222; color: white; width: 100%;}
select {text-overflow: ellipsis; white-space: nowrap; overflow: hidden;}
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
<tbody id="settings-table"></tbody>
</table>
<br>
<div id="settings-inputs">
<label>Name of Bluetooth device and Wi-Fi hotspot</label>
<div class="setting"><input id="domain_name" value=""><button onclick='updateSetting("domain_name")'>Save</button></div>
<label>Module connection speed</label>
<div class="setting"><select id="serial_baudrate">
<option value="57600">57k EdgeTX versions earlier than 2.10, via Serial AUX (except TX16)</option>
<option value="115200">115k EdgeTX versions 2.10 and later, via Serial AUX + all EdgeTX versions with TX16</option>
<option value="400000">400k direct connection to internal ELRS/TBS module</option>
<option value="921600">921k direct connection to internal ELRS/TBS module</option>
<option value="1870000">1.87M direct connection to internal ELRS/TBS module</option>
<option value="3750000">3.75M direct connection to internal module ELRS/TBS</option>
<option value="5250000">5.25M direct connection to the internal ELRS/TBS module</option>
</select>
<button onclick='updateSetting("serial_baudrate")'>Save</button>
</div>
<label>Module operating mode</label>
<div class="setting">
<select id="mode">
<option value="0">BLE</option>
<option value="1">Wi-Fi</option>
</select>
<button onclick='updateSetting("mode")'>Save</button>
</div>
</div>
</div>
<div id="update" class="tab-content">
<h2>Firmware Update</h2>
<form id="upload-form">
<div class="setting">
<input type="file" id="file" name="update">
<button type="submit" id="update-btn">Update</button>
</div>
</form>
<br>
<div id="prg"></div>
</div>
<div id="telemetry" class="tab-content">
<h2>Telemetry</h2>
<table>
<thead><tr><th>Sensor</th><th>Value</th></tr></thead>
<tbody id="telemetry-table"></tbody>
</table>
<br>
<button id="ws-btn" onclick="toggleWebSocket()">Connect</button>
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
        if (data[k]) document.getElementById(k).value = data[k];
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
let socket = null;
function toggleWebSocket() {
    let btn = document.getElementById("ws-btn");
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.close();
    } else {
        socket = new WebSocket("ws://192.168.4.1/ws");
        socket.binaryType = "arraybuffer";
        socket.onopen = () => btn.textContent = "Disconnect";
        socket.onclose = () => btn.textContent = "Connect";
        socket.onmessage = (e) => {
            if (e.data instanceof ArrayBuffer) {
                let bytes = new Uint8Array(e.data);
                processCRSFData(bytes);
            }
        };
    }
}
function getPowerLabel(power) {
    switch (power) {
        case 1: return '10 mW';
        case 2: return '25 mW';
        case 3: return '100 mW';
        case 4: return '500 mW';
        case 5: return '1 W';
        case 6: return '2 W';
        case 7: return '250 mW';
        case 8: return '50 mW';
        default: return power;
    }
}
function getELRSRateLabel(rate) {
    switch (rate) {
        case 0: return '4';
        case 1: return '25';
        case 2: return '50';
        case 3: return '100c';
        case 4: return '100';
        case 5: return '150';
        case 6: return '200';
        case 7: return '250';
        case 8: return '333c';
        case 9: return '500';
        case 10: return 'D250';
        case 11: return 'D500';
        case 12: return 'F500';
        case 13: return 'F1000';
        default: return rate;
    }
}
function processCRSFData(bytes) {
    let packetType = bytes[2];
    let payload = bytes.slice(3, -1);
    let dataView = new DataView(payload.buffer);
    switch (packetType) {
        case 0x08:
            let voltage = (payload[0] << 8 | payload[1]) / 10;
            let current = (payload[2] << 8 | payload[3]) / 10;
            let capacity = (payload[4] << 16 | payload[5] << 8 | payload[6]);
            addRow("Voltage", voltage + " V");
            addRow("Current", current + " A");
            addRow("Used capacity", capacity + " mAh");
            break;
        case 0x02:
            let latitude = dataView.getInt32(0, false) / 1e7;
            let longitude = dataView.getInt32(4, false) / 1e7;
            let speed = ((payload[8] << 8) | payload[9]) / 10;
            let altitudeGPS = ((payload[12] << 8) | payload[13]) - 1000;
            let satellite = payload[14];
            if (latitude != 0.0 && longitude != 0.0) {
                addRow("Latitude", latitude.toFixed(6));
                addRow("Longitude", longitude.toFixed(6));
            }
            addRow("Speed", speed + " km/h");
            addRow("Altitude GPS", altitudeGPS + " m");
            addRow("Satellites", satellite);
            break;
        case 0x14:
            addRow("Uplink RSSI Ant. 1", (payload[0] != 0 ? (payload[0] - 256) : 0) + " dBm");
            addRow("Uplink RSSI Ant. 2", (payload[1] != 0 ? (payload[1] - 256) : 0) + " dBm");
            addRow("Uplink LQ", payload[2] + "%");
            addRow("Rate", getELRSRateLabel(payload[5]));
            addRow("TX Power", getPowerLabel(payload[6]));
            addRow("Downlink RSSI", (payload[7] != 0 ? (payload[7] - 256) : 0) + " dBm");
            addRow("Downlink LQ", payload[8] + "%");
            break;
        case 0x09:
            let altitude = ((payload[0] << 8) | payload[1]) - 1000;
            addRow("Altitude", altitude + " m");
            break;
    }
}
function addRow(sensorName, value) {
    let existingRow = document.querySelector(`#telemetry-table tr[data-sensor="${sensorName}"]`);
    if (existingRow) {
        existingRow.children[1].textContent = value;
    } else {
        let row = document.createElement("tr");
        row.setAttribute("data-sensor", sensorName);
        row.innerHTML = `<td>${sensorName}</td><td>${value}</td>`;
        document.getElementById('telemetry-table').appendChild(row);
    }
}
</script>
</body>
</html>
)literal";
