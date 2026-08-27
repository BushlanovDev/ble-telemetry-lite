#include "main.h"
#include "index_html.h"
#include <esp_chip_info.h>

static Preferences preferences;

static HardwareSerial SerialPort(SERIAL_PORT);

static uint8_t crsfBuffer[CRSF_MAX_PACKET_SIZE];
static size_t crsfIndex = 0;
static GENERIC_CRC8 crsfCrc(CRSF_CRC_POLY);

static uint32_t serialBaudrate = DEFAULT_SERIAL_BAUDRATE;
static std::string domainName = DEFAULT_DOMAIN_NAME;
static std::string password = DEFAULT_PASSWORD;

static uint8_t mode = MODE_BLE;

static bool bleDeviceConnected = false;
static uint16_t bleActiveConnHandle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t bleCurrentMtu = 23;
static bool bleMtuWarned = false;
static bool bleTxSubscribed = false;
static bool deviceShouldShutdown = true;

static unsigned long startTime = 0;
static unsigned long nextTimeLinkStats = 0;
static unsigned long packetCount = 0;

static AsyncWebServer webServer(DEFAULT_WEB_PORT);
static AsyncWebSocket ws("/ws");
static volatile uint32_t wsClientId = 0;

static NimBLEAdvertising *pAdvertising;
static NimBLEServer *pServer;

static NimBLECharacteristic *pCharacteristicVendor;
static NimBLECharacteristic *pCharacteristicModel;
static NimBLECharacteristic *pCharacteristicFirmware;

static NimBLECharacteristic *pCharacteristicTX;
static NimBLECharacteristic *pCharacteristicRX;

static NimBLECharacteristic *pCharacteristicBaudrate;
static NimBLECharacteristic *pCharacteristicDomain;
static NimBLECharacteristic *pCharacteristicMode;
static NimBLECharacteristic *pCharacteristicUartStatus;

static inline bool isValidMode(const uint8_t m) { return m == MODE_BLE || m == MODE_WEB; }
static inline bool isValidBaudrate(const uint32_t b) { return b >= SERIAL_BAUDRATE_MIN && b <= SERIAL_BAUDRATE_MAX; }

static bool isValidDomainName(const uint8_t *data, const size_t len) {
    if (len == 0 || len > DOMAIN_NAME_MAX_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x20 || data[i] > 0x7E) {
            return false;
        }
    }
    return true;
}

static volatile bool pendingRestart = false;
static volatile unsigned long restartRequestTime = 0;
static volatile bool pendingFlush = false;

static inline void requestRestart() {
    restartRequestTime = millis();
    pendingRestart = true;
}

// UART diagnostics: fed by the CRSF parser, evaluated once per window in loop()
static volatile uint8_t uartDiagStatus = UART_DIAG_OK;
static volatile bool pendingDiagReset = false;
static uint32_t diagBytes = 0;
static uint32_t diagFrames = 0;
static uint32_t diagBadWindows = 0;
static unsigned long diagWindowStart = 0;

static void sendUartDiagStatus() {
    if (pCharacteristicUartStatus == nullptr) {
        return;
    }
    const uint8_t status = uartDiagStatus;
    pCharacteristicUartStatus->setValue(&status, 1);
    pCharacteristicUartStatus->notify();
}

static void uartDiagReset() {
    diagBytes = 0;
    diagFrames = 0;
    diagBadWindows = 0;
    diagWindowStart = millis();
    uartDiagStatus = UART_DIAG_OK;
    sendUartDiagStatus();
}

static void uartDiagEvaluate() {
    if (millis() - diagWindowStart < UART_DIAG_WINDOW_MS) {
        return;
    }
    diagWindowStart = millis();

    const uint8_t previous = uartDiagStatus;
    if (diagFrames > 0) {
        uartDiagStatus = UART_DIAG_OK;
        diagBadWindows = 0;
    } else if (diagBytes == 0) {
        uartDiagStatus = UART_DIAG_NO_SIGNAL;
        diagBadWindows = 0;
    } else if (++diagBadWindows >= UART_DIAG_BAD_WINDOWS_TO_ENTER) {
        uartDiagStatus = UART_DIAG_BAD_DATA;
    }
    diagBytes = 0;
    diagFrames = 0;

    if (uartDiagStatus != previous) {
        ESP_LOGI(TAG, "UART diagnostics status changed: %u -> %u", (unsigned)previous, (unsigned)uartDiagStatus);
        sendUartDiagStatus();
    }
}

namespace {
    class ServerCallbacks final : public NimBLEServerCallbacks {
    public:
        void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
            bleActiveConnHandle = connInfo.getConnHandle();
            bleCurrentMtu = 23;
            bleMtuWarned = false;
            bleTxSubscribed = false;
            bleDeviceConnected = true;
            deviceShouldShutdown = false;
            NimBLEDevice::setPower(DEFAULT_BLE_DBM_HIGH_PWR);
            pendingFlush = true;
            ESP_LOGI(TAG, "BLEServer onConnect power up and disable shutdown timer");
        }

        void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
            if (connInfo.getConnHandle() == bleActiveConnHandle) {
                bleActiveConnHandle = BLE_HS_CONN_HANDLE_NONE;
                bleTxSubscribed = false;
            }
            bleDeviceConnected = (pServer->getConnectedCount() > 0);
            if (!bleDeviceConnected) {
                NimBLEDevice::setPower(DEFAULT_BLE_DBM_LOW_PWR);
                NimBLEDevice::startAdvertising();
            }
            ESP_LOGI(TAG, "BLEServer onDisconnect power down");
        }

        void onMTUChange(const uint16_t MTU, NimBLEConnInfo &connInfo) override {
            bleCurrentMtu = MTU;
            if (MTU < CRSF_MAX_PACKET_SIZE + 3) {
                ESP_LOGW(TAG, "Negotiated MTU %u < %u (conn %u); telemetry may be truncated", MTU, (unsigned)(CRSF_MAX_PACKET_SIZE + 3), connInfo.getConnHandle());
            } else {
                ESP_LOGI(TAG, "MTU updated: %u for connection ID: %u", MTU, connInfo.getConnHandle());
            }
        }
    };

    class TXCharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
    public:
        void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override {
            bleTxSubscribed = (subValue != 0);
            if (subValue != 0) {
                // Discovery is done by now; only then request the fast connection interval needed for telemetry.
                NimBLEDevice::getServer()->updateConnParams(connInfo.getConnHandle(), 6, 6, 0, 500);
            }
            ESP_LOGI(TAG, "FFF6 notifications %s by connection %u", subValue ? "enabled" : "disabled", connInfo.getConnHandle());
        }
    };

    static TXCharacteristicCallbacks txCallbacks;

    class CharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
    public:
        void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
            if (pCharacteristic->getUUID() == pCharacteristicBaudrate->getUUID()) {
                const auto v = pCharacteristic->getValue();
                if (v.size() < sizeof(uint32_t)) {
                    return;
                }

                uint32_t newBaudrate;
                memcpy(&newBaudrate, v.data(), sizeof(uint32_t));
                if (!isValidBaudrate(newBaudrate)) {
                    ESP_LOGW(TAG, "Rejected invalid baudrate: %u", newBaudrate);
                    return;
                }

                serialBaudrate = newBaudrate;
                preferences.putUInt(PREFERENCES_REC_SERIAL_BAUDRATE, serialBaudrate);
                SerialPort.updateBaudRate(serialBaudrate);
                pendingDiagReset = true;
                pendingFlush = true;
                ESP_LOGI(TAG, "SerialPort baudrate updated: %d", serialBaudrate);
            } else if (pCharacteristic->getUUID() == pCharacteristicDomain->getUUID()) {
                const auto v = pCharacteristic->getValue();
                if (!isValidDomainName(v.data(), v.size())) {
                    ESP_LOGW(TAG, "Rejected invalid domain name (length %u)", (unsigned)v.size());
                    return;
                }

                domainName.assign(reinterpret_cast<const char *>(v.data()), v.size());
                preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, domainName.data(), domainName.size());
                NimBLEDevice::setDeviceName(domainName);
                pAdvertising = NimBLEDevice::getAdvertising();
                pAdvertising->setName(domainName);
                ESP_LOGI(TAG, "Domain name updated: %s", domainName.c_str());
            } else if (pCharacteristic->getUUID() == pCharacteristicMode->getUUID()) {
                const auto v = pCharacteristic->getValue();
                if (v.size() < 1) {
                    return;
                }
                const uint8_t newMode = static_cast<uint8_t>(v.data()[0]);
                if (!isValidMode(newMode)) {
                    ESP_LOGW(TAG, "Rejected invalid mode: %u", newMode);
                    return;
                }
                mode = newMode;
                preferences.putUInt(PREFERENCES_REC_MODE, mode);
                ESP_LOGI(TAG, "Mode updated: %d", mode);
                requestRestart();
            }
        }
    };

    static CharacteristicCallbacks chrCallbacks;
}

static constexpr size_t IMAGE_HEADER_LEN = 14;
static constexpr uint8_t IMAGE_MAGIC = 0xE9;

static uint8_t otaHeader[IMAGE_HEADER_LEN];
static size_t otaHeaderLen = 0;
static bool otaHeaderChecked = false;
static String otaRejectReason;

static uint16_t imageChipId(const uint8_t *header) {
    return (uint16_t)((unsigned)header[12] | ((unsigned)header[13] << 8));
}

static uint16_t deviceChipId() {
    esp_chip_info_t info;
    esp_chip_info(&info);
    return (uint16_t)info.model;
}

static String chipName(uint16_t chipId) {
    switch (chipId) {
        case 0:
            return "ESP32";
        case 2:
            return "ESP32-S2";
        case 5:
            return "ESP32-C3";
        case 9:
            return "ESP32-S3";
        case 13:
            return "ESP32-C6";
        default:
            return "unknown chip (id " + String(chipId) + ")";
    }
}

static void handleUpdateEnd(AsyncWebServerRequest *request) {
    if (!otaRejectReason.isEmpty()) {
        request->send(400, "text/plain", otaRejectReason);
        otaRejectReason = "";
    } else if (Update.hasError()) {
        request->send(502, "text/plain", Update.errorString());
    } else {
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Refresh", "10");
        response->addHeader("Location", "/");
        request->send(response);
        requestRestart();
    }
}

static void handleUpdate(AsyncWebServerRequest *request, const String &filename, const size_t index, uint8_t *data, size_t len, const bool final) {
    size_t fsize = UPDATE_SIZE_UNKNOWN;
    if (request->hasArg("size")) {
        const long s = request->arg("size").toInt();
        if (s > 0) {
            fsize = (size_t)s;
        }
    }

    if (!index) {
        otaHeaderLen = 0;
        otaHeaderChecked = false;
        otaRejectReason = "";
        request->onDisconnect([]() {
            if (Update.isRunning()) {
                Update.abort();
            }
        });
        ESP_LOGI(TAG, "Receiving Update: %s, Size: %s", filename.c_str(),
                 fsize == UPDATE_SIZE_UNKNOWN ? "unknown" : String(fsize).c_str());
    }

    if (!otaHeaderChecked && otaRejectReason.isEmpty()) {
        const size_t need = IMAGE_HEADER_LEN - otaHeaderLen;
        const size_t take = need < len ? need : len;
        memcpy(otaHeader + otaHeaderLen, data, take);
        otaHeaderLen += take;
        data += take;
        len -= take;

        if (otaHeaderLen == IMAGE_HEADER_LEN) {
            if (otaHeader[0] != IMAGE_MAGIC) {
                otaRejectReason = "Not a firmware image (bad magic byte). Upload firmware.bin from the release ZIP.";
                ESP_LOGW(TAG, "OTA rejected: bad magic byte");
            } else if (imageChipId(otaHeader) != deviceChipId()) {
                otaRejectReason = "Image is for " + chipName(imageChipId(otaHeader)) + ", this device is " + chipName(deviceChipId());
                ESP_LOGW(TAG, "OTA rejected: %s", otaRejectReason.c_str());
            } else {
                otaHeaderChecked = true;
                if (!Update.begin(fsize)) {
                    ESP_LOGI(TAG, "Error: %s", Update.errorString());
                    Update.printError(Serial);
                } else if (Update.write(otaHeader, IMAGE_HEADER_LEN) != IMAGE_HEADER_LEN) {
                    ESP_LOGI(TAG, "Error: %s", Update.errorString());
                    Update.printError(Serial);
                }
            }
        }
    }

    if (otaHeaderChecked && Update.isRunning() && len > 0 && Update.write(data, len) != len) {
        ESP_LOGI(TAG, "Error: %s", Update.errorString());
        Update.printError(Serial);
    }

    if (final) {
        if (!otaRejectReason.isEmpty()) {
            return; // handleUpdateEnd sends the 400 response
        }
        if (!otaHeaderChecked) {
            otaRejectReason = "Uploaded file is too small to be a firmware image.";
            return;
        }
        if (Update.isRunning()) {
            const bool lenientEnd = (fsize == UPDATE_SIZE_UNKNOWN) && (Update.progress() > 0);
            if (!Update.end(lenientEnd)) {
                ESP_LOGI(TAG, "Error: %s", Update.errorString());
                Update.printError(Serial);
            }
        }
    }
}

static void handleSetSettings(AsyncWebServerRequest *request) {
    if (request->hasArg(PREFERENCES_REC_SERIAL_BAUDRATE)) {
        const uint32_t newBaudrate = (uint32_t)request->arg(PREFERENCES_REC_SERIAL_BAUDRATE).toInt();
        if (!isValidBaudrate(newBaudrate)) {
            request->send(400, "text/plain", "Invalid baudrate");
            return;
        }
        serialBaudrate = newBaudrate;
        preferences.putUInt(PREFERENCES_REC_SERIAL_BAUDRATE, serialBaudrate);
        SerialPort.updateBaudRate(serialBaudrate);
        pendingDiagReset = true;
        pendingFlush = true;
        ESP_LOGI(TAG, "SerialPort baudrate updated: %d", serialBaudrate);
        request->send(200);
    }

    if (request->hasArg(PREFERENCES_REC_DOMAIN_NAME)) {
        const String dn = request->arg(PREFERENCES_REC_DOMAIN_NAME);
        if (!isValidDomainName(reinterpret_cast<const uint8_t *>(dn.c_str()), dn.length())) {
            request->send(400, "text/plain", "Invalid domain name");
            return;
        }
        domainName.assign(dn.c_str(), dn.length());
        preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, domainName.data(), domainName.size());
        ESP_LOGI(TAG, "Domain name updated: %s", domainName.c_str());
        request->send(200);
        requestRestart();
    }

    if (request->hasArg(PREFERENCES_REC_MODE)) {
        const uint8_t newMode = (uint8_t)request->arg(PREFERENCES_REC_MODE).toInt();
        if (!isValidMode(newMode)) {
            request->send(400, "text/plain", "Invalid mode");
            return;
        }
        mode = newMode;
        preferences.putUInt(PREFERENCES_REC_MODE, mode);
        ESP_LOGI(TAG, "Mode updated: %d", mode);
        request->send(200);
        requestRestart();
    }
}

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        ESP_LOGI(TAG, "WebSocket client connected from %s", client->remoteIP().toString().c_str());
        const uint32_t prevId = wsClientId;
        if (prevId != 0) {
            ESP_LOGI(TAG, "Kicking previous WebSocket client id %u", (unsigned)prevId);
            server->close(prevId);
        }
        wsClientId = client->id();
        pendingFlush = true;
    } else if (type == WS_EVT_DISCONNECT) {
        ESP_LOGI(TAG, "WebSocket client disconnected from %s", client->remoteIP().toString().c_str());
        if (client->id() == wsClientId) {
            wsClientId = 0;
        }
    }
}

static void onWiFiAPStarted(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (!WiFi.setTxPower(DEFAULT_WIFI_LOW_PWR)) {
        ESP_LOGE(TAG, "Failed to set WiFi AP TX power to low");
        return;
    }
    ESP_LOGI(TAG, "WiFi AP started, TX power set to low");
}

static void onWiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    ESP_LOGI(TAG, "WiFi client connected power up and disabling shutdown timer");
    WiFi.setTxPower(DEFAULT_WIFI_HIGH_PWR);
    deviceShouldShutdown = false;
}

static void onWiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    ESP_LOGI(TAG, "WiFi client disconnected power down");
    WiFi.setTxPower(DEFAULT_WIFI_LOW_PWR);
}

static void initSerial() {
    SerialPort.setRxBufferSize(512);
    SerialPort.begin(serialBaudrate, SERIAL_8N1, SERIAL_PIN_RX);
    ESP_LOGI(TAG, "Serial initialized");
}

static void initBLE() {
    NimBLEDevice::init(domainName);
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService *pServiceInformation = pServer->createService("180A");
    pCharacteristicVendor = pServiceInformation->createCharacteristic("2A29", NIMBLE_PROPERTY::READ);
    pCharacteristicVendor->setValue(VENDOR);
    pCharacteristicModel = pServiceInformation->createCharacteristic("2A24", NIMBLE_PROPERTY::READ);
    pCharacteristicModel->setValue(MODEL);
    pCharacteristicFirmware = pServiceInformation->createCharacteristic("2A26", NIMBLE_PROPERTY::READ);
    pCharacteristicFirmware->setValue(FIRMWARE);

    NimBLEService *pServiceExchange = pServer->createService("FFF0");
    pCharacteristicTX = pServiceExchange->createCharacteristic("FFF6", NIMBLE_PROPERTY::NOTIFY);
    pCharacteristicTX->setCallbacks(&txCallbacks);
    pCharacteristicRX = pServiceExchange->createCharacteristic("FFF7", NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicRX->setCallbacks(&chrCallbacks);

    NimBLEService *pServiceConfig = pServer->createService("FFF1");

    pCharacteristicBaudrate = pServiceConfig->createCharacteristic("FFF1", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicBaudrate->setCallbacks(&chrCallbacks);
    pCharacteristicBaudrate->setValue(serialBaudrate);

    pCharacteristicDomain = pServiceConfig->createCharacteristic("FFF2", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicDomain->setCallbacks(&chrCallbacks);
    pCharacteristicDomain->setValue(domainName);

    pCharacteristicMode = pServiceConfig->createCharacteristic("FFF3", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicMode->setCallbacks(&chrCallbacks);
    pCharacteristicMode->setValue(mode);

    pCharacteristicUartStatus = pServiceConfig->createCharacteristic("FFF4", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    sendUartDiagStatus();

    pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(pServiceExchange->getUUID());
    pAdvertising->addServiceUUID(pServiceConfig->getUUID());
    if (!pAdvertising->setName(domainName)) {
        ESP_LOGE(TAG, "Failed to set advertising name (length %u)", (unsigned)domainName.size());
    }
    pAdvertising->start();

    NimBLEDevice::setMTU(CRSF_MAX_PACKET_SIZE + 3);
    NimBLEDevice::setPower(DEFAULT_BLE_DBM_LOW_PWR);

    ESP_LOGI(TAG, "BLE initialized");
}

static void initWiFi() {
    WiFi.onEvent(onWiFiAPStarted, ARDUINO_EVENT_WIFI_AP_START);
    WiFi.onEvent(onWiFiStationConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    WiFi.onEvent(onWiFiStationDisconnected, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
    WiFiClass::mode(WIFI_AP);
    if (!WiFi.softAP(domainName.c_str(), password.c_str())) {
        ESP_LOGE(TAG, "WiFi.softAP failed (domain name length: %u)", (unsigned)domainName.size());
    }
    ESP_LOGI(TAG, "WiFi AP initialized name: %s, password: %s", domainName.c_str(), password.c_str());
}

static void initWebServer() {
    webServer.on("/update", HTTP_POST, handleUpdateEnd, handleUpdate);

    webServer.on("/settings", HTTP_POST, handleSetSettings);

    webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        String response = "{";

        response += "\"model\": \"" + String(MODEL) + "\", ";
        response += "\"firmware\": \"" + String(FIRMWARE) + "\", ";
        response += "\"chip\": \"" + String(ESP.getChipModel()) + "\", ";
        response += "\"chip_id\": " + String((unsigned)deviceChipId()) + ", ";

        response += "\"" + String(PREFERENCES_REC_DOMAIN_NAME) + "\": \"" + domainName.c_str() + "\", ";
        response += "\"" + String(PREFERENCES_REC_SERIAL_BAUDRATE) + "\": \"" + String(serialBaudrate) + "\", ";
        response += "\"" + String(PREFERENCES_REC_MODE) + "\": \"" + String(mode) + "\", ";
        response += "\"uart_status\": " + String((unsigned)uartDiagStatus);

        response += "}";

        request->send(200, "text/json", response);
    });

    webServer.on("/", [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const uint8_t *)data_index_html, data_index_html_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    ws.onEvent(onWsEvent);
    webServer.addHandler(&ws);
    webServer.begin();

    ESP_LOGI(TAG, "Web Server initialized at http://%s", WiFi.softAPIP().toString().c_str());
}

static void initPreferences() {
    preferences.begin(PREFERENCES_NAME, false);

    if (preferences.isKey(PREFERENCES_REC_SERIAL_BAUDRATE)) {
        const uint32_t storedBaudrate = preferences.getUInt(PREFERENCES_REC_SERIAL_BAUDRATE);
        if (isValidBaudrate(storedBaudrate)) {
            serialBaudrate = storedBaudrate;
        } else {
            preferences.remove(PREFERENCES_REC_SERIAL_BAUDRATE);
        }
    }

    if (!preferences.isKey(PREFERENCES_REC_DOMAIN_NAME)) {
        preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, domainName.c_str(), domainName.length());
    } else {
        char domain_name_buffer[DOMAIN_NAME_MAX_LENGTH + 1];
        const unsigned int buffer_length = preferences.getBytes(PREFERENCES_REC_DOMAIN_NAME, domain_name_buffer, DOMAIN_NAME_MAX_LENGTH + 1);
        if (!isValidDomainName(reinterpret_cast<const uint8_t *>(domain_name_buffer), buffer_length)) {
            ESP_LOGW(TAG, "Stored domain name invalid (length %u), falling back to default", buffer_length);
            domainName = DEFAULT_DOMAIN_NAME;
            preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, domainName.c_str(), domainName.length());
        } else {
            domainName.assign(domain_name_buffer, buffer_length);
        }
    }

    if (preferences.isKey(PREFERENCES_REC_MODE)) {
        const uint8_t storedMode = (uint8_t)preferences.getUInt(PREFERENCES_REC_MODE);
        if (isValidMode(storedMode)) {
            mode = storedMode;
        } else {
            mode = MODE_BLE;
            preferences.remove(PREFERENCES_REC_MODE);
        }
    }

    ESP_LOGI(TAG, "Preferences initialized");
}

static void sendBleData(const uint8_t *data, size_t size) {
    if (!bleTxSubscribed) {
        return;
    }

    if (size > bleCurrentMtu - 3) {
        if (!bleMtuWarned) {
            bleMtuWarned = true;
            ESP_LOGW(TAG, "Frame of %u bytes exceeds negotiated MTU %u, dropping long frames this connection", (unsigned)size, (unsigned)bleCurrentMtu);
        }
        return;
    }

    const bool ok = (bleActiveConnHandle != BLE_HS_CONN_HANDLE_NONE)
                        ? pCharacteristicTX->notify(data, size, bleActiveConnHandle)
                        : pCharacteristicTX->notify(data, size);
    if (!ok) {
        ESP_LOGW(TAG, "Failed to ble notify");
    }
}

static void sendWSData(const uint8_t *data, const size_t size) {
    if (ws.availableForWrite(wsClientId)) {
        ws.binaryAll(data, size);
    }
}

static void sendData(const uint8_t *data, const size_t size) {
    if (bleDeviceConnected) {
        sendBleData(data, size);
    } else if (wsClientId != 0) {
        sendWSData(data, size);
    }
}

static void initLog() {
    Serial.begin(DEFAULT_SERIAL_BAUDRATE);
    Serial.setDebugOutput(true);
    const uint32_t serialConnectStart = millis();
    while (!Serial && millis() - serialConnectStart < SERIAL_CONNECT_TIMEOUT_MS) {
        delay(10);
    }
    esp_log_level_set("*", ESP_LOG_INFO);
    delay(500);
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "CPU Freq: %i Mhz", getCpuFrequencyMhz());
    ESP_LOGI(TAG, "Chip Model: %s", ESP.getChipModel());
    ESP_LOGI(TAG, "Chip Revision: %d", ESP.getChipRevision());
    ESP_LOGI(TAG, "Chip Cores %d", ESP.getChipCores());
    ESP_LOGI(TAG, "Chip Temp: %.2f C", temperatureRead());
    ESP_LOGI(TAG, "Flash Chip Size: %d", ESP.getFlashChipSize());
    ESP_LOGI(TAG, "Flash Chip Speed: %d", ESP.getFlashChipSpeed());
    ESP_LOGI(TAG, "PSRAM Size: %d", ESP.getPsramSize());
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Features included: %s %s %s %s %s",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded flash," : "",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "2.4GHz WiFi," : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "Bluetooth LE," : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "Bluetooth Classic," : "",
             (chip_info.features & CHIP_FEATURE_IEEE802154) ? "IEEE 802.15.4," : "");
    ESP_LOGI(TAG, "====================================");
}

void setup() {
#ifdef MAIN_DEBUG
    initLog();
#else
    esp_log_level_set("*", ESP_LOG_NONE);
#endif

#ifdef BOARD_ESP32C3
    pinMode(LED_PIN, OUTPUT);
    pinMode(BOOT_PIN, INPUT_PULLUP);
#endif
    initPreferences();
    delay(500);
    initSerial();

    if (mode == MODE_WEB) {
        initWiFi();
        initWebServer();
#ifdef BOARD_ESP32C3
        digitalWrite(LED_PIN, LOW);
#endif
    } else {
        initBLE();
#ifdef BOARD_ESP32C3
        digitalWrite(LED_PIN, HIGH);
#endif
    }

    startTime = millis();
}

void IRAM_ATTR loop() {
    if (mode == MODE_WEB) {
        ws.cleanupClients();
    }
    if (pendingRestart && millis() - restartRequestTime >= RESTART_DELAY_MS) {
        esp_restart();
    }

    if (deviceShouldShutdown && millis() - startTime >= DEFAULT_TIMEOUT_MS) {
        ESP_LOGI(TAG, "Timeout reached, going to sleep, bye bye");
        esp_deep_sleep_start();
    }

#ifdef BOARD_ESP32C3
    {
        static int lastBootReading = HIGH;
        static unsigned long lastBootDebounce = 0;
        static bool bootPressed = false;
        static bool bootArmed = false;
        const int reading = digitalRead(BOOT_PIN);
        if (reading != lastBootReading) {
            lastBootDebounce = millis();
        }
        lastBootReading = reading;
        if (millis() - lastBootDebounce > BOOT_DEBOUNCE_MS) {
            const bool pressed = (reading == LOW);
            if (pressed) {
                bootPressed = true;
            } else {
                if (bootPressed && bootArmed) {
                    if (mode == MODE_BLE) {
                        preferences.putUInt(PREFERENCES_REC_MODE, MODE_WEB);
                        ESP_LOGI(TAG, "Rebooting in Web mode");
                    } else {
                        preferences.putUInt(PREFERENCES_REC_MODE, MODE_BLE);
                        ESP_LOGI(TAG, "Rebooting in BLE mode");
                    }
                    requestRestart();
                }
                bootPressed = false;
                bootArmed = true;
            }
        }
    }
#endif

    if (pendingDiagReset) {
        pendingDiagReset = false;
        uartDiagReset();
    }
    uartDiagEvaluate();

    if (pendingFlush) {
        pendingFlush = false;
        while (SerialPort.available()) {
            SerialPort.read();
        }
        crsfIndex = 0;
        packetCount = 0;
    }

    while (SerialPort.available()) {
        const uint8_t byte = SerialPort.read();
        diagBytes++;
        if (crsfIndex == 0 &&
            byte != CRSF_ADDRESS_RADIO &&
            byte != CRSF_ADDRESS_RX &&
            byte != CRSF_ADDRESS_TX &&
            byte != CRSF_SYNC_BYTE
        ) {
            continue;
        }

        crsfBuffer[crsfIndex++] = byte;
        if (crsfIndex == 2) {
            const uint8_t expectedLength = crsfBuffer[1];
            if (expectedLength > CRSF_MAX_PAYLOAD_SIZE || expectedLength < CRSF_MIN_PAYLOAD_SIZE) {
                ESP_LOGI(TAG, "CRSF incorrect packet size skipped length:(%d)", expectedLength);
                crsfIndex = 0;
                continue;
            }
        } else if (crsfIndex > 2) {
            const uint8_t expectedLength = crsfBuffer[1] + 2;
            if (crsfIndex == expectedLength) {
                const uint8_t inCrc = crsfBuffer[expectedLength - 1];
                const uint8_t crc = crsfCrc.calc(&crsfBuffer[2], expectedLength - 3);
                if (inCrc != crc) {
                    memset(crsfBuffer, 0, expectedLength);
                    crsfIndex = 0;
                    ESP_LOGI(TAG, "CRSF incorrect packet crc 0x%02x != 0x%02x", inCrc, crc);
                    continue;
                }

                diagFrames++;

                const uint8_t type = crsfBuffer[2];
                if (type == CRSF_PING_PACKET_ID || type == CRSF_RC_SYNC_PACKET_ID) {
                    ESP_LOGI(TAG, "CRSF ping or sync packet skipped type:(0x%02x) length:(%d)", type, expectedLength);
                } else {
                    packetCount++;
                    sendData(crsfBuffer, expectedLength);
                }

                memset(crsfBuffer, 0, expectedLength);
                crsfIndex = 0;
                continue;
            }
        }
    }

    if (!bleDeviceConnected && wsClientId == 0) {
        delay(20);
        return;
    }

    unsigned long now = millis();
    if (nextTimeLinkStats <= now) {
        ESP_LOGI(TAG, "Packet count in last period: %d", packetCount);
        nextTimeLinkStats = now + DEFAULT_LINK_STATS_PACKET_PERIOD_MS;
        if (packetCount == 0) {
            sendData(EMPTY_LINK_STATS_PACKET, EMPTY_LINK_STATS_PACKET_SIZE);
            ESP_LOGI(TAG, "Sending empty link stats packet");
        }
        packetCount = 0;
    }
}
