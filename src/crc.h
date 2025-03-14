#pragma once

#include <stdint.h>
#include <Arduino.h>

#define crclen 256

class GENERIC_CRC8
{
private:
    uint8_t crc8tab[crclen]{};
    uint8_t crcpoly{};

public:
    explicit GENERIC_CRC8(uint8_t poly);
    uint8_t calc(const uint8_t data);
    uint8_t calc(const uint8_t *data, uint8_t len, uint8_t crc = 0);
};
