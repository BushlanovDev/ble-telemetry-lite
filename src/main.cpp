#include "main.h"
// #include <AsyncUDP.h>

Preferences preferences;

HardwareSerial SerialPort(SERIAL_PORT);

uint8_t serial_buffer_rx[SERIAL_BUFFER_LENGTH];
uint32_t serial_baudrate = DEFAULT_SERIAL_BAUDRATE;
std::string domain_name = DEFAULT_DOMAIN_NAME;
std::string password = DEFAULT_PASSWORD;

uint8_t otaDone = 0;
uint8_t mode = MODE_BLE;

bool deviceConnected = false;

/*
std::string protocol = DEFAULT_PROTOCOL;
uint16_t port = DEFAULT_PORT;

uint8_t pin_rx = 0xFF;
uint8_t pin_tx = 0xFF;
uint8_t pin_ppm = 0xFF;
uint8_t pin_key = 0xFF;
uint8_t pin_led = 0xFF;
uint8_t led_mode = 0x00;

AsyncUDP udp;
*/

WebServer server(80);

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

class ServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) override {
        deviceConnected = true;
        NimBLEDevice::setPower(DEFAULT_BLE_HIGH_PWR, ESP_BLE_PWR_TYPE_DEFAULT);
        ESP_LOGI(TAG, "BLEServer onConnect power up");
    }

    void onDisconnect(BLEServer *pServer) override {
        deviceConnected = false;
        NimBLEDevice::setPower(DEFAULT_BLE_LOW_PWR, ESP_BLE_PWR_TYPE_DEFAULT);
        ESP_LOGI(TAG, "BLEServer onDisconnect power down");
    }

    void onMTUChange(uint16_t MTU, ble_gap_conn_desc *desc) override {
        ESP_LOGI(TAG, "MTU updated: %u for connection ID: %u", MTU, desc->conn_handle);
    }
};

class CharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
    void IRAM_ATTR onWrite(NimBLECharacteristic* pCharacteristic) override {
        if (pCharacteristic->getUUID() == pCharacteristicBaudrate->getUUID())
        {
            serial_baudrate = *(uint32_t*)pCharacteristic->getValue().data();
            preferences.putUInt(PREFERENCES_REC_SERIAL_BAUDRATE, (uint32_t)serial_baudrate);
            SerialPort.updateBaudRate(serial_baudrate);
            ESP_LOGI(TAG, "SerialPort baudrate updated: %d", serial_baudrate);
        }
        else if (pCharacteristic->getUUID() == pCharacteristicDomain->getUUID())
        {
            domain_name.assign((char*)pCharacteristic->getValue().data(), pCharacteristic->getDataLength());
            preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, (char*)pCharacteristic->getValue().data(), pCharacteristic->getDataLength());
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

/*
void onUdpPacket(AsyncUDPPacket packet) {
    std::string str;
    str.assign((char*)packet.data(), packet.length());
    Serial.println(str.c_str());

    udp.broadcastTo(packet.data(), packet.length(), port + 1);
}
*/

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
    pAdvertising->addServiceUUID("FFF0");
    pAdvertising->addServiceUUID("FFF1");
    pAdvertising->start();

    NimBLEDevice::setMTU(SERIAL_BUFFER_LENGTH + 3);
    NimBLEDevice::setPower(DEFAULT_BLE_LOW_PWR, ESP_BLE_PWR_TYPE_DEFAULT);

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

/*
    if (!preferences.isKey(PREFERENCES_REC_PASSWORD))
    {
        preferences.putBytes(PREFERENCES_REC_PASSWORD, password.c_str(), password.length());
    }
    else
    {
        char password_buffer[32];
        int buffer_length = preferences.getBytes(PREFERENCES_REC_PASSWORD, password_buffer, 32);
        password.assign(password_buffer, buffer_length);
    }

    if (!preferences.isKey(PREFERENCES_REC_PROTOCOL))
    {
        preferences.putBytes(PREFERENCES_REC_PROTOCOL, protocol.c_str(), protocol.length());
    }
    else
    {
        char protocol_buffer[3];
        int buffer_length = preferences.getBytes(PREFERENCES_REC_PROTOCOL, protocol_buffer, 3);
        protocol.assign(protocol_buffer, buffer_length);
    }

    if (preferences.isKey(PREFERENCES_REC_PORT))
    {
        port = (uint16_t)preferences.getUInt(PREFERENCES_REC_PORT);
    }
*/
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
    ESP_LOGI(TAG, "Flash Chip Size : %d", ESP.getFlashChipSize());
    ESP_LOGI(TAG, "Flash Chip Speed : %d", ESP.getFlashChipSpeed());
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
    pinMode(BOOT_PIN, INPUT);
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
}

void IRAM_ATTR loop()
{
    if (digitalRead(BOOT_PIN) == 0)
    {
        preferences.putUInt(PREFERENCES_REC_MODE, MODE_BLE);
        esp_restart();
    }

    if (mode == MODE_WEB)
    {
        server.handleClient();
    }

    if (!deviceConnected)
    {
        delay(20);
        return;
    }

    if (SerialPort.available())
    {
        const size_t bytes = SerialPort.read(serial_buffer_rx, SERIAL_BUFFER_LENGTH);
        if (bytes < MIN_PAYLOAD_SIZE)
        {
            ESP_LOGI(TAG, "CRSF error packet size: %d", bytes);
            return;
        }

        const uint8_t type = serial_buffer_rx[2];
        if (type == PING_PACKET_ID || type == RC_SYNC_PACKET_ID)
        {
            ESP_LOGI(TAG, "CRSF ping or sync packet skipped");
            return;
        }

        //if (mode == 1) {
        //        udp.broadcastTo(serial_buffer_rx, bytes, port);
        //} else {
            pCharacteristicTX->notify(serial_buffer_rx, bytes, true);
        //}
    }
}
