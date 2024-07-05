#include "main.h"
#include <NimBLEDevice.h>

Preferences preferences;

HardwareSerial SerialPort(SERIAL_PORT);
uint8_t serial_buffer_rx[SERIAL_BUFFER_LENGTH];
uint32_t serial_baudrate = DEFAULT_SERIAL_BAUDRATE;


std::string domain_name = DEFAULT_DOMAIN_NAME;

NimBLECharacteristic *pCharacteristicDomain;
NimBLECharacteristic *pCharacteristicBaudrate;
NimBLECharacteristic *pCharacteristicTelemetry;
NimBLECharacteristic *pCharacteristicSBus;

void initSerial() {
    SerialPort.begin(serial_baudrate, SERIAL_MODE, SERIAL_PIN_RX, SERIAL_PIN_TX);
}


class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        if (pCharacteristic->getUUID() == pCharacteristicSBus->getUUID()) {
            SerialPort.print(pCharacteristic->getValue().c_str());
        } else if (pCharacteristic->getUUID() == pCharacteristicDomain->getUUID()) {
            preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, (char*)pCharacteristic->getValue().data(), pCharacteristic->getDataLength());
        } else if(pCharacteristic->getUUID() == pCharacteristicBaudrate->getUUID()) {
            serial_baudrate = *(uint32_t*)pCharacteristic->getValue().data();
            preferences.putUInt(PREFERENCES_REC_SERIAL_BAUDRATE, (uint32_t)serial_baudrate);
            SerialPort.updateBaudRate(serial_baudrate);
        }
    };
};

static CharacteristicCallbacks chrCallbacks;

void initBLE() {
    NimBLEDevice::init(domain_name);
    
    NimBLEServer *pServer = NimBLEDevice::createServer();

    NimBLEService *pServiceExchange = pServer->createService("FFF0");
    pCharacteristicTelemetry = pServiceExchange->createCharacteristic("FFF6", NIMBLE_PROPERTY::NOTIFY);
    pCharacteristicSBus = pServiceExchange->createCharacteristic("FFF7", NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicSBus->setCallbacks(&chrCallbacks);

    NimBLEService *pServiceConfig = pServer->createService("FFF1");
    pCharacteristicDomain = pServiceConfig->createCharacteristic("FFF2", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicDomain->setCallbacks(&chrCallbacks);
    pCharacteristicDomain->setValue(domain_name);

    pCharacteristicBaudrate = pServiceConfig->createCharacteristic("FFF4", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicBaudrate->setCallbacks(&chrCallbacks);
    pCharacteristicBaudrate->setValue(serial_baudrate);
    
    pServiceExchange->start();
    pServiceConfig->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("FFF0");
    pAdvertising->addServiceUUID("FFF1");
    pAdvertising->start(); 

    NimBLEDevice::setPower(ESP_PWR_LVL_P21, ESP_BLE_PWR_TYPE_DEFAULT); // +9db
}


void initPreferences() {
    preferences.begin(PREFERENCES_NAME, false);

    if (!preferences.isKey(PREFERENCES_REC_DOMAIN_NAME)) {
        preferences.putBytes(PREFERENCES_REC_DOMAIN_NAME, domain_name.c_str(), domain_name.length());
    } else {
        char domain_name_buffer[32];
        int domain_name_buffer_length = preferences.getBytes(PREFERENCES_REC_DOMAIN_NAME, domain_name_buffer, 32);
        domain_name.assign(domain_name_buffer, domain_name_buffer_length);
    }
    
    if (preferences.isKey(PREFERENCES_REC_SERIAL_BAUDRATE)) {
        serial_baudrate = preferences.getUInt(PREFERENCES_REC_SERIAL_BAUDRATE);
    }
}

void setup() {
    initPreferences();
    delay(1500);
    initSerial();
    initBLE();
}

void loop() {
    if (SerialPort.available()) {
        size_t bytes = SerialPort.read(serial_buffer_rx, 64);
        pCharacteristicTelemetry->notify(serial_buffer_rx, bytes, true);
    }
}
