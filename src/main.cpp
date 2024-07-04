#include "main.h"
#include <NimBLEDevice.h>

Preferences preferences;

NimBLECharacteristic *pCharacteristicDomain;
NimBLECharacteristic *pCharacteristicSpeed;
NimBLECharacteristic *pCharacteristicTelemetry;



std::string domain_name = "BLE Telemetry Light";

uint32_t serial_speed = 115200;
uint8_t serial_pin_tx = 4;
uint8_t serial_pin_rx = 3;



uint8_t buffer_rx[64] ;
uint8_t buffer_tx[64] ;

HardwareSerial SerialPort(1);

void initSerial() {
    SerialPort.begin(serial_speed, SERIAL_8N1, serial_pin_rx, serial_pin_tx);
}


class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        if (pCharacteristic->getUUID() == pCharacteristicTelemetry->getUUID()) {
            SerialPort.print(pCharacteristic->getValue().c_str());
        } else if (pCharacteristic->getUUID() == pCharacteristicDomain->getUUID()) {
            preferences.putBytes("domain_name", (char*)pCharacteristic->getValue().data(), pCharacteristic->getDataLength());
        } else if(pCharacteristic->getUUID() == pCharacteristicSpeed->getUUID()) {
            serial_speed = *(uint32_t*)pCharacteristic->getValue().data();
            preferences.putUInt("serial_baudrate", (uint32_t)serial_speed);
            SerialPort.updateBaudRate(serial_speed);
            SerialPort.end();
            initSerial();
        }
    };
};

static CharacteristicCallbacks chrCallbacks;

void initBLE() {
    NimBLEDevice::init(domain_name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // +9db
    NimBLEServer *pServer = NimBLEDevice::createServer();
    NimBLEService *pServiceTelemetry = pServer->createService("FFF0");
    pCharacteristicTelemetry = pServiceTelemetry->createCharacteristic("FFF6", NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::WRITE_NR);
    

    NimBLEService *pServiceConfig = pServer->createService("FFF1");
    pCharacteristicDomain = pServiceConfig->createCharacteristic("FFF2", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
    pCharacteristicSpeed = pServiceConfig->createCharacteristic("FFF4", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);

    pCharacteristicTelemetry->setCallbacks(&chrCallbacks);

    pCharacteristicDomain->setCallbacks(&chrCallbacks);
    pCharacteristicDomain->setValue(domain_name);
    pCharacteristicSpeed->setCallbacks(&chrCallbacks);
    pCharacteristicSpeed->setValue(serial_speed);

    
    pServiceTelemetry->start();
    pServiceConfig->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("FFF0");
    pAdvertising->addServiceUUID("FFF1");
    pAdvertising->start(); 
}


void initPreferences() {
    preferences.begin("dronecontrol", false);

    if (!preferences.isKey("domain_name")) {
        preferences.putBytes("domain_name", domain_name.c_str(), domain_name.length());
    } else {
        char domain_name_buffer[32];
        int domain_name_buffer_length = preferences.getBytes("domain_name", domain_name_buffer, 32);
        domain_name.assign(domain_name_buffer, domain_name_buffer_length);
    }
    
    if (preferences.isKey("serial_baudrate")) {
        serial_speed = preferences.getUInt("serial_baudrate");
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
        size_t bytes = SerialPort.read(buffer_rx, 64);
        pCharacteristicTelemetry->notify(buffer_rx, bytes, true);
    }
}
