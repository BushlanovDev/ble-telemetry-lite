#include "main.h"

Preferences preferences;

HardwareSerial SerialPort(SERIAL_PORT);

uint8_t crsfBuffer[CRSF_MAX_PACKET_SIZE];
size_t crsfIndex = 0;
GENERIC_CRC8 crsfCrc(CRSF_CRC_POLY);

uint32_t serial_baudrate = DEFAULT_SERIAL_BAUDRATE;
std::string domain_name = DEFAULT_DOMAIN_NAME;
std::string password = DEFAULT_PASSWORD;

uint8_t otaDone = 0;
uint8_t mode = MODE_BLE;

bool bleDeviceConnected = false;
bool deviceShouldShutdown = true;

unsigned long startTime = 0;
unsigned long nextTimeLinkStats = 0;
unsigned long packetCount = 0;

AsyncWebServer webServer(DEFAULT_WEB_PORT);
AsyncWebSocket ws("/ws");
AsyncWebSocketClient* wsClient;

NimBLEAdvertising *pAdvertising;
NimBLEServer *pServer;

NimBLECharacteristic *pCharacteristicVendor;
NimBLECharacteristic *pCharacteristicModel;
NimBLECharacteristic *pCharacteristicFirmware;

NimBLECharacteristic *pCharacteristicTX;
NimBLECharacteristic *pCharacteristicRX;

NimBLECharacteristic *pCharacteristicBaudrate;
NimBLECharacteristic *pCharacteristicDomain;
NimBLECharacteristic *pCharacteristicMode;

class ServerCallbacks final : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        bleDeviceConnected = true;
        deviceShouldShutdown = false;
        pServer->updateConnParams(connInfo.getConnHandle(), 6, 6, 0, 500);
        NimBLEDevice::setPower(DEFAULT_BLE_HIGH_PWR);
        ESP_LOGI(TAG, "BLEServer onConnect power up and disable shutdown timer");
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        bleDeviceConnected = false;
        NimBLEDevice::setPower(DEFAULT_BLE_LOW_PWR);
        NimBLEDevice::startAdvertising();
        ESP_LOGI(TAG, "BLEServer onDisconnect power down");
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
        ESP_LOGI(TAG, "MTU updated: %u for connection ID: %u", MTU, connInfo.getConnHandle());
    }
};

class CharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        if (pCharacteristic->getUUID() == pCharacteristicBaudrate->getUUID())
        {
            serial_baudrate = *(uint32_t*)pCharacteristic->getValue().data();
            preferences.putUInt(PREFERENCES_REC_SERIAL_BAUDRATE, (uint32_t)serial_baudrate);
            SerialPort.updateBaudRate(serial_baudrate);
            ESP_LOGI(TAG, "SerialPort baudrate updated: %d", serial_baudrate);
        }
        else if (pCharacteristic->getUUID() == pCharacteristicDomain->getUUID())
        {
            domain_name.assign((char*)pCharacteristic->getValue().data(), pCharacteristic->getLength());
            preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, (char*)pCharacteristic->getValue().data(), pCharacteristic->getLength());
            NimBLEDevice::setDeviceName(domain_name);
            pAdvertising = NimBLEDevice::getAdvertising();
            pAdvertising->setName(domain_name);
            ESP_LOGI(TAG, "Domain name updated: %s", domain_name.c_str());
        }
        else if (pCharacteristic->getUUID() == pCharacteristicMode->getUUID())
        {
            mode = *(uint8_t*)pCharacteristic->getValue().data();
            preferences.putUInt(PREFERENCES_REC_MODE, (uint8_t)mode);
            ESP_LOGI(TAG, "Mode updated: %d", mode);
            delay(500);
            esp_restart();
        }
    }
};

static CharacteristicCallbacks chrCallbacks;

void handleUpdateEnd(AsyncWebServerRequest *request)
{
    if (Update.hasError())
    {
        request->send(502, "text/plain", Update.errorString());
    }
    else
    {
        AsyncWebServerResponse *response = request->beginResponse(307);
        response->addHeader("Refresh","10");
        response->addHeader("Location","/");
        request->send(response);
        ESP.restart();
    }
}

void handleUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
{
    size_t fsize = UPDATE_SIZE_UNKNOWN;
    if (request->hasArg("size"))
    {
        fsize = request->arg("size").toInt();
    }

    if (!index)
    {
        ESP_LOGI(TAG, "Receiving Update: %s, Size: %d", filename, fsize);
        if (!Update.begin(fsize))
        {
            ESP_LOGI(TAG, "Error: %s\n", Update.errorString());
            Update.printError(Serial);
        }
    }

    if (Update.write(data, len) != len)
    {
        ESP_LOGI(TAG, "Error: %s\n", Update.errorString());
        Update.printError(Serial);
    }

    if (final)
    {
        if (!Update.end(true))
        {
            ESP_LOGI(TAG, "Error: %s\n", Update.errorString());
            Update.printError(Serial);
        }
        else
        {
            ESP_LOGI(TAG, "Update Success: %u bytes\nRebooting...", fsize);
        }
    }
}

void handleSetSettings(AsyncWebServerRequest *request)
{
    if (request->hasArg(PREFERENCES_REC_SERIAL_BAUDRATE))
    {
        serial_baudrate = (uint32_t)request->arg(PREFERENCES_REC_SERIAL_BAUDRATE).toInt();
        preferences.putUInt(PREFERENCES_REC_SERIAL_BAUDRATE, serial_baudrate);
        SerialPort.updateBaudRate(serial_baudrate);
        ESP_LOGI(TAG, "SerialPort baudrate updated: %d", serial_baudrate);
        request->send(200);
    }

    if (request->hasArg(PREFERENCES_REC_DOMAIN_NAME))
    {
        domain_name = request->arg(PREFERENCES_REC_DOMAIN_NAME).c_str();
        preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, domain_name.c_str(), domain_name.length());
        ESP_LOGI(TAG, "Domain name updated: %s", domain_name.c_str());
        request->send(200);
        delay(500);
        esp_restart();
    }

    if (request->hasArg(PREFERENCES_REC_MODE))
    {
        mode = (uint8_t)request->arg(PREFERENCES_REC_MODE).toInt();
        preferences.putUInt(PREFERENCES_REC_MODE, mode);
        ESP_LOGI(TAG, "Mode updated: %d", mode);
        request->send(200);
        delay(500);
        esp_restart();
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        ESP_LOGI(TAG, "WebSocket client connected from %s", client->remoteIP().toString().c_str());
        wsClient = client;
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        ESP_LOGI(TAG, "WebSocket client disconnected from %s", client->remoteIP().toString().c_str());
        wsClient = nullptr;
    }
}

void initSerial()
{
    SerialPort.begin(serial_baudrate, SERIAL_8N1, SERIAL_PIN_RX, SERIAL_PIN_TX);
    ESP_LOGI(TAG, "Serial initialized");
}

void initBLE()
{
    NimBLEDevice::init(domain_name);
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
    pCharacteristicRX = pServiceExchange->createCharacteristic("FFF7", NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicRX->setCallbacks(&chrCallbacks);

    NimBLEService *pServiceConfig = pServer->createService("FFF1");

    pCharacteristicBaudrate = pServiceConfig->createCharacteristic("FFF1", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicBaudrate->setCallbacks(&chrCallbacks);
    pCharacteristicBaudrate->setValue(serial_baudrate);

    pCharacteristicDomain = pServiceConfig->createCharacteristic("FFF2", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicDomain->setCallbacks(&chrCallbacks);
    pCharacteristicDomain->setValue(domain_name);

    pCharacteristicMode = pServiceConfig->createCharacteristic("FFF3", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicMode->setCallbacks(&chrCallbacks);
    pCharacteristicMode->setValue(mode);

    pServiceExchange->start();
    pServiceConfig->start();
    pServiceInformation->start();

    pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(pServiceExchange->getUUID());
    pAdvertising->addServiceUUID(pServiceConfig->getUUID());
    pAdvertising->setName(domain_name);
    pAdvertising->start();

    NimBLEDevice::setMTU(CRSF_MAX_PACKET_SIZE + 3);
    NimBLEDevice::setPower(DEFAULT_BLE_LOW_PWR);

    ESP_LOGI(TAG, "BLE initialized");
}

void initWiFi()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(domain_name.c_str(), password.c_str());

    ESP_LOGI(TAG, "WiFi AP initialized name: %s, password: %s", domain_name.c_str(), password.c_str());
}

void initWebServer()
{
    webServer.on("/update", HTTP_POST, handleUpdateEnd, handleUpdate);

    webServer.on("/settings", HTTP_POST, handleSetSettings);

    webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        String response = "{";

        response += "\"vendor\": \"" + String(VENDOR) + "\", ";
        response += "\"model\": \"" + String(MODEL) + "\", ";
        response += "\"firmware\": \"" + String(FIRMWARE) + "\", ";

        response += "\"" + String(PREFERENCES_REC_DOMAIN_NAME) + "\": \"" + domain_name.c_str() + "\", ";
        response += "\"" + String(PREFERENCES_REC_SERIAL_BAUDRATE) + "\": \"" + String(serial_baudrate) + "\", ";
        response += "\"" + String(PREFERENCES_REC_MODE) + "\": \"" + String(mode) + "\"";

        response += "}";

        request->send(200, "text/json", response);
    });

    webServer.on("/", [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", indexHtml);
    });

    ws.onEvent(onWsEvent);
    webServer.addHandler(&ws);
    webServer.begin();

    ESP_LOGI(TAG, "Web Server initialized at http://%s", WiFi.softAPIP().toString().c_str());
}

void initPreferences()
{
    preferences.begin(PREFERENCES_NAME, false);

    if (preferences.isKey(PREFERENCES_REC_SERIAL_BAUDRATE))
    {
        serial_baudrate = preferences.getUInt(PREFERENCES_REC_SERIAL_BAUDRATE);
    }

    if (!preferences.isKey(PREFERENCES_REC_DOMAIN_NAME))
    {
        preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, domain_name.c_str(), domain_name.length());
    }
    else
    {
        char domain_name_buffer[32];
        unsigned int buffer_length = preferences.getBytes(PREFERENCES_REC_DOMAIN_NAME, domain_name_buffer, 32);
        domain_name.assign(domain_name_buffer, buffer_length);
    }

    if (preferences.isKey(PREFERENCES_REC_MODE))
    {
        mode = (uint8_t)preferences.getUInt(PREFERENCES_REC_MODE);
    }

    ESP_LOGI(TAG, "Preferences initialized");
}

void sendBleData(const uint8_t* data, size_t size)
{
    pCharacteristicTX->setValue(data, size);
    if (!pCharacteristicTX->notify())
    {
        ESP_LOGI(TAG, "Failed to ble notify");
    }
}

void sendWSData(const uint8_t* data, size_t size)
{
    if (wsClient->canSend())
    {
        ws.binaryAll(data, size);
    }
}

void sendData(const uint8_t *data, size_t size)
{
    if (bleDeviceConnected)
    {
        sendBleData(data, size);
    }
    else if (wsClient != nullptr)
    {
        sendWSData(data, size);
    }
}

void setup()
{
#ifdef MAIN_DEBUG
    Serial.begin(DEFAULT_SERIAL_BAUDRATE);
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
#else
    esp_log_level_set("*", ESP_LOG_NONE);
#endif

    pinMode(LED_PIN, OUTPUT);
#ifdef BOARD_ESP32C3
    pinMode(BOOT_PIN, INPUT);
#endif
    initPreferences();
    delay(500);
    initSerial();

    if (mode == MODE_BLE)
    {
        initBLE();
        digitalWrite(LED_PIN, HIGH);
    }
    else if (mode == MODE_WEB)
    {
        initWiFi();
        initWebServer();
        digitalWrite(LED_PIN, LOW);
    }

    startTime = millis();
}

void IRAM_ATTR loop()
{
    if (mode == MODE_WEB && deviceShouldShutdown && WiFi.softAPgetStationNum() > 0)
    {
        ESP_LOGI(TAG, "WiFi client connected, disabling shutdown timer");
        deviceShouldShutdown = false;
    }

    if (deviceShouldShutdown && millis() - startTime >= DEFAULT_TIMEOUT_MS)
    {
        ESP_LOGI(TAG, "Timeout reached, going to sleep, bye bye");
        esp_deep_sleep_start();
    }

#ifdef BOARD_ESP32C3
    if (digitalRead(BOOT_PIN) == 0)
    {
        preferences.putUInt(PREFERENCES_REC_MODE, MODE_BLE);
        esp_restart();
    }
#endif

    if (!bleDeviceConnected && wsClient == nullptr)
    {
        delay(20);
        return;
    }

    unsigned long now = millis();
    if (nextTimeLinkStats <= now)
    {
        ESP_LOGI(TAG, "Packet count in last period: %d", packetCount);
        nextTimeLinkStats = now + DEFAULT_BLE_LINKSTATS_PACKET_PERIOD_MS;
        if (packetCount == 0)
        {
            sendData(EMPTY_LINK_STATS_PACKET, EMPTY_LINK_STATS_PACKET_SIZE);
            ESP_LOGI(TAG, "Sending empty link stats packet");
        }
        packetCount = 0;
    }

    while (SerialPort.available())
    {
        const uint8_t byte = SerialPort.read();
        if (crsfIndex == 0 && byte != CRSF_ADDRESS_RADIO_TRANSMITTER)
        {
            return;
        }

        crsfBuffer[crsfIndex++] = byte;
        if (crsfIndex == 2)
        {
            const uint8_t expectedLength = crsfBuffer[1];
            if (expectedLength > CRSF_MAX_PAYLOAD_SIZE || expectedLength < CRSF_MIN_PAYLOAD_SIZE)
            {
                ESP_LOGI(TAG, "CRSF incorrect packet size skipped length:(%d)", expectedLength);
                crsfIndex = 0;
                return;
            }
        }
        else if (crsfIndex > 2)
        {
            const uint8_t expectedLength = crsfBuffer[1] + 2;
            if (crsfIndex == expectedLength)
            {
                const uint8_t inCrc = crsfBuffer[expectedLength - 1];
                const uint8_t crc = crsfCrc.calc(&crsfBuffer[2], expectedLength - 3);
                if (inCrc != crc)
                {
                    memset(crsfBuffer, 0, expectedLength);
                    crsfIndex = 0;
                    ESP_LOGI(TAG, "CRSF incorrect packet crc 0x%02x != 0x%02x", inCrc, crc);
                    return;
                }

                const uint8_t type = crsfBuffer[2];
                if (type == CRSF_PING_PACKET_ID || type == CRSF_RC_SYNC_PACKET_ID)
                {
                    ESP_LOGI(TAG, "CRSF ping or sync packet skipped type:(0x%02x) length:(%d)", type, expectedLength);
                }
                else
                {
                    packetCount++;
                    sendData(crsfBuffer, expectedLength);
                }

                memset(crsfBuffer, 0, expectedLength);
                crsfIndex = 0;

                return;
            }
        }
    }
}
