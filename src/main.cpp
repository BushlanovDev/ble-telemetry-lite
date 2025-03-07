#include "main.h"

Preferences preferences;

HardwareSerial SerialPort(SERIAL_PORT);

uint8_t serial_buffer_rx[SERIAL_BUFFER_LENGTH];
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

WebServer server(DEFAULT_WEB_PORT);

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

void handleUpdateEnd()
{
    server.sendHeader("Connection", "close");
    if (Update.hasError())
    {
        server.send(502, "text/plain", Update.errorString());
    }
    else
    {
        server.sendHeader("Refresh", "10");
        server.sendHeader("Location", "/");
        server.send(307);
        ESP.restart();
    }
}

void handleUpdate()
{
    size_t fsize = UPDATE_SIZE_UNKNOWN;
    if (server.hasArg("size"))
    {
        fsize = server.arg("size").toInt();
    }

    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        ESP_LOGI(TAG, "Receiving Update: %s, Size: %d", upload.filename.c_str(), fsize);
        if (!Update.begin(fsize))
        {
            otaDone = 0;
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            Update.printError(Serial);
        }
        else
        {
            otaDone = 100 * Update.progress() / Update.size();
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (Update.end(true))
        {
            ESP_LOGI(TAG, "Update Success: %u bytes\nRebooting...", upload.totalSize);
        }
        else
        {
            ESP_LOGI(TAG, "Error: %s\n", Update.errorString());
            otaDone = 0;
        }
    }
}

void handleSetSettings() {
    server.sendHeader("Connection", "close");

    if (server.hasArg(PREFERENCES_REC_SERIAL_BAUDRATE))
    {
        serial_baudrate = (uint32_t)server.arg(PREFERENCES_REC_SERIAL_BAUDRATE).toInt();
        preferences.putUInt(PREFERENCES_REC_SERIAL_BAUDRATE, serial_baudrate);
        SerialPort.updateBaudRate(serial_baudrate);
        ESP_LOGI(TAG, "SerialPort baudrate updated: %d", serial_baudrate);
        server.send(200);
    }

    if (server.hasArg(PREFERENCES_REC_DOMAIN_NAME))
    {
        domain_name = server.arg(PREFERENCES_REC_DOMAIN_NAME).c_str();
        preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, domain_name.c_str(), domain_name.length());
        ESP_LOGI(TAG, "Domain name updated: %s", domain_name.c_str());
        server.send(200);
        delay(500);
        esp_restart();
    }

    if (server.hasArg(PREFERENCES_REC_MODE))
    {
        mode = (uint8_t)server.arg(PREFERENCES_REC_MODE).toInt();
        preferences.putUInt(PREFERENCES_REC_MODE, mode);
        ESP_LOGI(TAG, "Mode updated: %d", mode);
        server.send(200);
        delay(500);
        esp_restart();
    }
}

void initSerial()
{
    SerialPort.begin(serial_baudrate, SERIAL_MODE, SERIAL_PIN_RX, SERIAL_PIN_TX);
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

    NimBLEDevice::setMTU(SERIAL_BUFFER_LENGTH + 3);
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
    server.on("/update", HTTP_POST, handleUpdateEnd, handleUpdate);
    server.on("/settings", HTTP_POST, handleSetSettings);
    server.on("/settings", HTTP_GET, []() {
        String response = "{";

        response += "\"vendor\": \"" + String(VENDOR) + "\", ";
        response += "\"model\": \"" + String(MODEL) + "\", ";
        response += "\"firmware\": \"" + String(FIRMWARE) + "\", ";

        response += "\"" + String(PREFERENCES_REC_DOMAIN_NAME) + "\": \"" + domain_name.c_str() + "\", ";
        response += "\"" + String(PREFERENCES_REC_SERIAL_BAUDRATE) + "\": \"" + String(serial_baudrate) + "\", ";
        response += "\"" + String(PREFERENCES_REC_MODE) + "\": \"" + String(mode) + "\"";

        response += "}";

        server.sendHeader("Connection", "close");
        server.send(200, "text/json", response);
    });
    server.onNotFound([]() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", indexHtml);
    });
    server.begin();

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

void sendBleData(const uint8_t* data, size_t size)
{
    pCharacteristicTX->setValue(data, size);
    if (!pCharacteristicTX->notify())
    {
        ESP_LOGI(TAG, "Failed to ble notify");
    }
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

    if (mode == MODE_WEB)
    {
        server.handleClient();
    }

    if (!bleDeviceConnected)
    {
        delay(20);
        return;
    }

    if (SerialPort.available())
    {
        unsigned long now = millis();
        const size_t bytes = SerialPort.read(serial_buffer_rx, SERIAL_BUFFER_LENGTH);

        if (nextTimeLinkStats <= now)
        {
            ESP_LOGI(TAG, "Packet count in last period: %d", packetCount);
            nextTimeLinkStats = now + DEFAULT_BLE_LINKSTATS_PACKET_PERIOD_MS;
            if (packetCount == 0)
            {
                sendBleData(EMPTY_LINK_STATS_PACKET, EMPTY_LINK_STATS_PACKET_SIZE);
                ESP_LOGI(TAG, "Sending empty link stats packet");
            }
            packetCount = 0;
        }

        if (bytes < CRSF_MIN_PAYLOAD_SIZE)
        {
            ESP_LOGI(TAG, "CRSF error packet size: %d", bytes);
            return;
        }

        const uint8_t type = serial_buffer_rx[2];
        if (type == CRSF_PING_PACKET_ID || type == CRSF_RC_SYNC_PACKET_ID)
        {
            ESP_LOGI(TAG, "CRSF ping or sync packet skipped");
            return;
        }

        packetCount++;

        sendBleData((uint8_t*)&serial_buffer_rx, bytes);
    }
}
