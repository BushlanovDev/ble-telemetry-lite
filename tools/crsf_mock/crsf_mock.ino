// crsf-mock — mock drone telemetry (CRSF) emulator for testing ble-telemetry-lite.
//
// Wiring: TX_PIN (default GPIO4) -> RX of the device under test
//              (ESP32-C3 = GPIO3, ESP32-S3 = GPIO16), GND <-> GND.
// Board in Arduino IDE: any ESP32 (e.g. "ESP32C3 Dev Module"). No libraries needed.
//
// Stream: 0x14 link stats at 10 Hz; battery/GPS/attitude/flight-mode/baro+vario/
// airspeed/vario at 1 Hz each with offsets. In the real log: 0x14 every 240 ms, slow frames
// every 5632 ms in the order GPS->BATT->ATT->FLIGHT; here it is faster — livelier UI and
// headroom against the 250-ms filler window. About 17 frames/s total — the "silence window"
// is ruled out, the EMPTY packet is never sent. All fields are big-endian (see
// data/index.html of the receiver).

#include <Arduino.h>

// ----------------------------- settings ------------------------------------
#define TX_PIN           4         // mock TX -> receiver RX
#define BAUD_RATE        115200    // receiver default
#define GPS_LAT          59.939026 // center of the "flight circle" (enter your own point)
#define GPS_LON          30.315791
#define ORBIT_RADIUS_M   150.0f    // circle radius, m
#define ORBIT_PERIOD_S   60.0f     // circle period, s (~56 km/h)
#define BATTERY_MINUTES  20.0f     // 4.2 V -> 3.3 V over this time

#define PERIOD_LINK_STATS 100      // ms, 0x14
#define PERIOD_SLOW       1000     // ms, all other frames

// ----------------------------- CRSF framing --------------------------------
// Frame: [0xEA][len][type][payload][crc8], len = 1(type)+len(payload)+1(crc).
// CRC8 poly 0xD5, init 0 — same logic as src/crc.cpp of the receiver.
static uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
    }
    return crc;
}

static void sendFrame(uint8_t type, const uint8_t *payload, uint8_t len) {
    uint8_t frame[64];
    frame[0] = 0xEA;
    frame[1] = len + 2;
    frame[2] = type;
    memcpy(frame + 3, payload, len);
    frame[3 + len] = crc8(frame + 2, len + 1); // CRC over type+payload
    Serial1.write(frame, len + 4);
}

// big-endian helpers
static void putU16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void putU32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void putI16(uint8_t *p, int16_t v) { putU16(p, (uint16_t)v); }
static void putI32(uint8_t *p, int32_t v) { putU32(p, (uint32_t)v); }

// ----------------------------- drone model ---------------------------------
// Deterministic "noise" (sum of sines): the stream is reproducible, no random() needed.
static float wobble(float t, float seed) {
    return sinf(t * 1.7f + seed) * 0.6f + sinf(t * 0.37f + seed * 2.1f) * 0.4f; // ~[-1, 1]
}

// 0x14 link statistics, 10 bytes.
static void buildLinkStats(uint8_t *p) {
    float t = millis() / 1000.0f;
    int8_t upRssi = -51 + (int8_t)(wobble(t, 1.0f) * 5); // -56..-46 dBm (log: -57..-46)
    int8_t downRssi = -52 + (int8_t)(wobble(t, 5.0f) * 10); // -62..-42 dBm (log: -67..-38)
    uint8_t upLq = (uint8_t)(98 + wobble(t, 2.0f) * 2); // 96..100 %  (log: 100)
    uint8_t downLq = (uint8_t)(88 + wobble(t, 6.0f) * 8); // 80..96 %   (log: 68..96)
    p[0] = (uint8_t)upRssi; // "-dBm" packed in a byte, as in the log (0xCA = -54)
    p[1] = 0x00; // no antenna 2 (always 00 in the log)
    p[2] = upLq;
    p[3] = (uint8_t)(int8_t)(11 + (int8_t)(wobble(t, 3.0f) * 2)); // uplink SNR
    p[4] = 0; // active antenna 1
    p[5] = 0x07; // rf mode — a constant from the log
    p[6] = 0x02; // tx power = 25 mW — from the log (UI: getPowerLabel)
    p[7] = (uint8_t)downRssi;
    p[8] = downLq;
    p[9] = (uint8_t)(int8_t)(9 + (int8_t)(wobble(t, 4.0f) * 2)); // downlink SNR
}

// 0x08 battery, 8 bytes BE: [V u16 0.1V][I u16 0.1A][mAh 3 bytes][rem u8 %]
static void buildBattery(uint8_t *p) {
    float t = millis() / 1000.0f;
    float flight = BATTERY_MINUTES * 60.0f;
    float v = 4.2f - 0.9f * (t / flight); // 4.2 -> 3.3 V
    if (v < 3.3f)
        v = 3.3f; // safety clamp for long runs
    float thr = 0.55f + 0.35f * sinf(2.0f * PI * t / ORBIT_PERIOD_S); // "throttle" along the circle
    float a = 4.0f + 14.0f * thr + wobble(t, 7.0f); // ~2..18 A
    uint32_t mah = (uint32_t)(10.0f * t / 3.6f); // ~10 A average -> mAh
    uint8_t rem = (uint8_t)constrain(99.0f * (1.0f - t / flight), 0.0f, 99.0f);
    putU16(p, (uint16_t)lroundf(v * 10.0f));
    putU16(p + 2, (uint16_t)lroundf(a * 10.0f));
    p[4] = (uint8_t)(mah >> 16);
    p[5] = (uint8_t)(mah >> 8);
    p[6] = (uint8_t)mah;
    p[7] = rem;
}

// 0x02 GPS, 15 bytes BE: flying in a circle around the point from the header
static void buildGps(uint8_t *p) {
    float t = millis() / 1000.0f;
    float ang = 2.0f * PI * fmodf(t, ORBIT_PERIOD_S) / ORBIT_PERIOD_S;
    double lat = GPS_LAT + (double)(ORBIT_RADIUS_M * cosf(ang)) / 111320.0;
    double lon = GPS_LON + (double)(ORBIT_RADIUS_M * sinf(ang)) /
                 (111320.0 * cosf(GPS_LAT * DEG_TO_RAD));
    uint16_t spd = (uint16_t)lroundf((2.0f * PI * ORBIT_RADIUS_M / ORBIT_PERIOD_S * 3.6f +
                                      wobble(t, 8.0f) * 3.0f) * 10.0f); // ~56 km/h, unit 0.1 km/h
    uint16_t hdg = (uint16_t)((int)(ang * 180.0f / PI + 90.0f) % 360); // tangent, degrees
    int16_t altM = (int16_t)lroundf(100.0f + wobble(t, 9.0f) * 15.0f); // 85..115 m
    putI32(p, (int32_t)llround(lat * 1e7));
    putI32(p + 4, (int32_t)llround(lon * 1e7));
    putU16(p + 8, spd);
    putU16(p + 10, (uint16_t)(hdg * 100)); // CRSF: hundredths of a degree (35999 max — fits)
    putI16(p + 12, (int16_t)(altM + 1000)); // the receiver subtracts 1000
    p[14] = 14; // satellites
}

// 0x1E attitude, 6 bytes BE: radians*1e4 (the web UI doesn't read it — for BLE clients)
static void buildAttitude(uint8_t *p) {
    float t = millis() / 1000.0f;
    float ang = 2.0f * PI * fmodf(t, ORBIT_PERIOD_S) / ORBIT_PERIOD_S;
    float yaw = ang; // 0..2PI
    if (yaw > PI)
        yaw -= 2.0f * PI; // -> [-PI, PI]
    putI16(p, (int16_t)lroundf(sinf(t * 0.9f) * 0.09f * 10000.0f)); // pitch ~±5°
    putI16(p + 2, (int16_t)lroundf(sinf(t * 1.3f + 1.0f) * 0.09f * 10000.0f)); // roll ~±5°
    putI16(p + 4, (int16_t)lroundf(yaw * 10000.0f));
}

// 0x21 flight mode: ASCII string with '\0'; cycles every 8 s — so updates are visible in the UI
static const char *const FLIGHT_MODES[] = {"ARM", "ACRO*", "RTH*", "!FS!"};
static void buildFlightMode(uint8_t *p) {
    const char *s = FLIGHT_MODES[(millis() / 8000) % (sizeof(FLIGHT_MODES) / sizeof(FLIGHT_MODES[0]))];
    memset(p, 0, 6); // plen 6: string + '\0' + zero padding
    memcpy(p, s, strlen(s) + 1);
}

// Vertical speed of the model, ±3 m/s. Sampled once a second together with the GPS
// (g_varioCms), so that 0x07 and the vario byte of 0x09 show the same value.
static float varioMs(void) { return wobble(millis() / 1000.0f, 11.0f) * 3.0f; }

// 0x09 baro altitude: the packed format from the CRSF spec — this is how INAV/Betaflight
// master send it (BF: 3 bytes with vario; INAV: truncated to 2 without vario). The inverse of
// unpackAlt09/unpackVario09 from data/index.html of the receiver: MSB=1 -> whole meters;
// MSB=0 -> decimeters with a 10000 offset (= -1000 m); vario is an int8 on a log scale.
static uint16_t packAlt09(int32_t altM) {
    int32_t dm = altM * 10;
    if (dm < -10000) return 0; // below -1000 m
    if (dm > 0x7ffe * 10 - 5) return 0xfffe; // above ~32765 m (format maximum)
    if (dm < (int32_t)0x8000 - 10000) return (uint16_t)(dm + 10000); // decimeter precision
    return (uint16_t)(((dm + 5) / 10) | 0x8000); // meter precision
}
static int8_t packVario09(float vsMs) {
    float cm = vsMs * 100.0f;
    int8_t mag = (int8_t)(logf(fabsf(cm) / 100.0f + 1.0f) / 0.026f);
    return cm < 0 ? (int8_t)-mag : mag;
}

static int16_t g_altM = 100;   // updated from 0x02 (GPS altitude)
static uint16_t g_spd = 56;    // updated from 0x02 (speed)
static int16_t g_varioCms = 0; // updated from 0x02 (vario)
static void buildVario(uint8_t *p) { putI16(p, g_varioCms); } // 0x07, cm/s
static void buildBaroAlt(uint8_t *p) {                          // 0x09, 3 bytes
    putU16(p, packAlt09(g_altM));
    p[2] = (uint8_t)packVario09(g_varioCms / 100.0f);
}
static void buildAirspeed(uint8_t *p) { putU16(p, g_spd); } // 0x0A, 0.1 km/h

// ----------------------------- scheduler -----------------------------------
struct Slot {
    uint8_t type; // CRSF frame type
    uint8_t plen; // payload length
    uint16_t period; // ms
    uint32_t next; // when to send (initialization = start offset)
    void (*build)(uint8_t *p);
};

static Slot slots[] = {
        {0x14, 10, PERIOD_LINK_STATS, 0, buildLinkStats},
        {0x02, 15, PERIOD_SLOW, 0, buildGps}, // slow-frame order — as in the real
        {0x08, 8, PERIOD_SLOW, 160, buildBattery}, // stream: GPS -> BATT -> ATT -> MODE
        {0x1E, 6, PERIOD_SLOW, 330, buildAttitude},
        {0x21, 6, PERIOD_SLOW, 500, buildFlightMode},
        {0x09, 3, PERIOD_SLOW, 660, buildBaroAlt}, // packed, like INAV/BF (no 0x09 in the real log)
        {0x0A, 2, PERIOD_SLOW, 830, buildAirspeed}, // optional (not present in the real log)
        {0x07, 2, PERIOD_SLOW, 900, buildVario},   // vario, like INAV/BF (not present in the real log)
};

void setup() {
    Serial.begin(115200); // USB banner, just for convenience
    Serial.println("crsf-mock: orbit flight started");
    Serial1.begin(BAUD_RATE, SERIAL_8N1, -1, TX_PIN); // UART, TX only
}

void loop() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
        Slot &s = slots[i];
        if ((int32_t)(now - s.next) >= 0) {
            uint8_t p[16];
            s.build(p);
            sendFrame(s.type, p, s.plen);
            // GPS holds altitude/speed for 0x09/0x0A — grab them on every update
            if (s.type == 0x02) {
                g_altM = (int16_t)((p[12] << 8 | p[13]) - 1000);
                g_spd = (uint16_t)(p[8] << 8 | p[9]);
                g_varioCms = (int16_t)lroundf(varioMs() * 100.0f);
            }
            s.next += s.period;
            if ((int32_t)(now - s.next) > 0) // guard against a "burst" after a lag
                s.next = now + s.period;
        }
    }
}
