#include "main.h"
#include <NimBLEDevice.h>
// #include <WiFi.h>
// #include <WiFiAP.h>
// #include <AsyncUDP.h>

Preferences preferences;

HardwareSerial SerialPort(SERIAL_PORT);

uint8_t serial_buffer_rx[SERIAL_BUFFER_LENGTH];
uint32_t serial_baudrate = DEFAULT_SERIAL_BAUDRATE;
std::string domain_name = DEFAULT_DOMAIN_NAME;

std::string password = DEFAULT_PASSWORD;
std::string protocol = DEFAULT_PROTOCOL;
uint16_t port = DEFAULT_PORT;

uint8_t mode = 0;

uint8_t pin_rx = 0xFF;
uint8_t pin_tx = 0xFF;
uint8_t pin_ppm = 0xFF;
uint8_t pin_key = 0xFF;
uint8_t pin_led = 0xFF;
uint8_t led_mode = 0x00;
/*
AsyncUDP udp;
*/

NimBLEAdvertising *pAdvertising;
NimBLEServer *pServer;

NimBLECharacteristic *pCharacteristicVendor;
NimBLECharacteristic *pCharacteristicModel;
NimBLECharacteristic *pCharacteristicFirmware;

NimBLECharacteristic *pCharacteristicTX;
NimBLECharacteristic *pCharacteristicRX;

NimBLECharacteristic *pCharacteristicBaudrate;
NimBLECharacteristic *pCharacteristicDomain;

#ifdef MAIN_DEBUG
class ServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) override {
        ESP_LOGI(TAG, "onConnect");
    }

    void onDisconnect(BLEServer *pServer) override {
        ESP_LOGI(TAG, "onDisconnect");
    }
};
#endif

class CharacteristicCallbacks final : public NimBLECharacteristicCallbacks {
    void IRAM_ATTR onWrite(NimBLECharacteristic* pCharacteristic) override {
        if (pCharacteristic->getUUID() == pCharacteristicBaudrate->getUUID())
        {
            serial_baudrate = *(uint32_t*)pCharacteristic->getValue().data();
            preferences.putUInt(PREFERENCES_REC_SERIAL_BAUDRATE, (uint32_t)serial_baudrate);
            SerialPort.updateBaudRate(serial_baudrate);
        }
        else if (pCharacteristic->getUUID() == pCharacteristicDomain->getUUID())
        {
            domain_name.assign((char*)pCharacteristic->getValue().data(), pCharacteristic->getDataLength());
            preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, (char*)pCharacteristic->getValue().data(), pCharacteristic->getDataLength());
            NimBLEDevice::setDeviceName(domain_name);
            pAdvertising = NimBLEDevice::getAdvertising();
            pAdvertising->setName(domain_name);
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

void initSerial()
{
    SerialPort.begin(serial_baudrate, SERIAL_MODE, SERIAL_PIN_RX, SERIAL_PIN_TX);
    ESP_LOGI(TAG, "Serial initialized");
}

void initBLE()
{
    NimBLEDevice::init(domain_name);
    pServer = NimBLEDevice::createServer();
#ifdef MAIN_DEBUG
    pServer->setCallbacks(new ServerCallbacks());
#endif

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

    pServiceExchange->start();
    pServiceConfig->start();
    pServiceInformation->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("FFF0");
    pAdvertising->addServiceUUID("FFF1");
    pAdvertising->start();

    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT); // +9db

    ESP_LOGI(TAG, "BLE initialized");
}

/*
void initWiFi() {
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(domain_name.c_str(), password.c_str());

    IPAddress myIP = WiFi.softAPIP();
    Serial.println(myIP);

    udp.onPacket(onUdpPacket);
    udp.listenMulticast(myIP, port);
}
*/

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
        int buffer_length = preferences.getBytes(PREFERENCES_REC_DOMAIN_NAME, domain_name_buffer, 32);
        domain_name.assign(domain_name_buffer, buffer_length);
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

    if (preferences.isKey(PREFERENCES_REC_MODE))
    {
        mode = (uint8_t)preferences.getUInt(PREFERENCES_REC_MODE);
    }
*/
}

void setup()
{
#ifdef MAIN_DEBUG
    Serial.begin(115200);
    esp_log_level_set("*", ESP_LOG_INFO);
#else
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    delay(500);

    pinMode(LED_PIN, OUTPUT);
    pinMode(BOOT_PIN, INPUT);
    initPreferences();
    delay(500);
    initSerial();

    //if (mode > 0) {
    //    digitalWrite(LED_PIN, 1);
    //    initWiFi();
    //} else {
        initBLE();
    //}
}

void IRAM_ATTR loop()
{
    if (digitalRead(BOOT_PIN) == 0)
    {
        preferences.putUInt(PREFERENCES_REC_MODE, 0);
        esp_restart();
    }

    if (SerialPort.available())
    {
        size_t bytes = SerialPort.read(serial_buffer_rx, 64);
        //if (mode == 1) {
        //        udp.broadcastTo(serial_buffer_rx, bytes, port);
        //} else {
            pCharacteristicTX->notify(serial_buffer_rx, bytes, true);
        //}
    }
}
