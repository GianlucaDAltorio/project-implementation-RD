/*
 * WaistBoard
 * Waist sensor board: ESP32 + MPU6050 (accel/gyro) + BMP280 (pressure) on I2C.
 *
 * Samples the MPU6050 at 238 Hz, like the watch and FallAllD, and the BMP280 about 25 times
 * a second, keeping the last 4 s of both. The watch connects over Bluetooth LE. After each
 * impact it writes "<id>,<age_ms>" (how long ago the wrist impact peak was) and reads back
 * "<id>,<status>,<15 values>": the 14 window features from fd_features.c and the height
 * change in metres, for a window centred on the waist's own peak near that moment.
 * Status 0 = ok, 1 = that window is no longer in the buffer.
 *
 * Keep the board still for the first 3 s: it measures the gyro offset. Mount it firmly;
 * its orientation does not matter.
 *
 * Wiring: SDA GPIO 21, SCL GPIO 22, both sensors on 3V3, MPU6050 AD0 and BMP280 SDO to GND.
 * Board: ESP32 Dev Module. No extra libraries. Serial Monitor at 115200 baud.
 */
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "esp_timer.h"
extern "C" {
#include "fd_features.h"
}

#define SDA_PIN 21
#define SCL_PIN 22
#define MPU_ADDR 0x68
#define BMP_ADDR 0x76

#define RING_N (4 * FD_FS)      // 4 s of IMU samples
#define BAR_N 128               // about 5 s of pressure samples
#define BAR_PERIOD_MS 40
#define LINK_MS 15              // assumed Bluetooth delay from the watch's write to here

// Must match the watch (FallDetector_trials.ino)
#define BLE_NAME     "FallWaist"
#define SERVICE_UUID "930fee35-698a-4c8e-8888-387e4d8b6a76"
#define REQ_UUID     "930fee35-698a-4c8e-8888-387e4d8b6a77"
#define FEAT_UUID    "930fee35-698a-4c8e-8888-387e4d8b6a78"

static float ring[RING_N][6];           // ax ay az (g), gx gy gz (deg/s)
static uint32_t ring_ms[RING_N];
static uint32_t n_samples = 0;
static float bar_p[BAR_N];
static uint32_t bar_ms[BAR_N], bar_n = 0, last_bar_ms = 0;
static float gbias[3] = {0, 0, 0};
static const double PERIOD_US = 1000000.0 / FD_FS;
static double next_us;

static BLECharacteristic *feat_char;
static volatile bool req_pending = false;
static volatile uint32_t req_id = 0, req_age = 0, req_rx_ms = 0;
static volatile bool connected = false;
static bool cur_active = false;
static uint32_t cur_id = 0, cur_target = 0, cur_t0 = 0;

// I2C
static bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg); Wire.write(val);
    return Wire.endTransmission() == 0;
}
static bool readRegs(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

// MPU6050: +-8 g (4096 LSB/g) and +-2000 deg/s (16.4 LSB per deg/s), as in FallAllD
static bool mpuInit() {
    if (!writeReg(MPU_ADDR, 0x6B, 0x01)) return false;     // wake, gyro clock
    delay(50);
    writeReg(MPU_ADDR, 0x1A, 0x02);                         // low-pass about 94 Hz
    writeReg(MPU_ADDR, 0x1B, 0x18);                         // gyro +-2000 deg/s
    writeReg(MPU_ADDR, 0x1C, 0x10);                         // accel +-8 g
    writeReg(MPU_ADDR, 0x19, 0x00);                         // 1 kHz internal rate
    return true;
}
static bool mpuRead(float *v) {
    uint8_t b[14];
    if (!readRegs(MPU_ADDR, 0x3B, b, 14)) return false;
    for (int k = 0; k < 3; k++) {
        v[k] = (int16_t)((b[2 * k] << 8) | b[2 * k + 1]) / 4096.0f;
        v[3 + k] = (int16_t)((b[8 + 2 * k] << 8) | b[9 + 2 * k]) / 16.4f;
    }
    return true;
}

// BMP280: calibration and compensation from the Bosch datasheet
static uint16_t dig_T1, dig_P1;
static int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

static bool bmpInit() {
    uint8_t c[24];
    if (!readRegs(BMP_ADDR, 0x88, c, 24)) return false;
    auto u16 = [&](int i) { return (uint16_t)(c[i] | (c[i + 1] << 8)); };
    dig_T1 = u16(0);  dig_T2 = (int16_t)u16(2);  dig_T3 = (int16_t)u16(4);
    dig_P1 = u16(6);  dig_P2 = (int16_t)u16(8);  dig_P3 = (int16_t)u16(10); dig_P4 = (int16_t)u16(12);
    dig_P5 = (int16_t)u16(14); dig_P6 = (int16_t)u16(16); dig_P7 = (int16_t)u16(18);
    dig_P8 = (int16_t)u16(20); dig_P9 = (int16_t)u16(22);
    writeReg(BMP_ADDR, 0xF5, 0x08);   // filter x4, 0.5 ms standby
    writeReg(BMP_ADDR, 0xF4, 0x2F);   // temp x1, pressure x4, normal mode
    return true;
}
static bool bmpRead(float &hpa) {
    uint8_t d[6];
    if (!readRegs(BMP_ADDR, 0xF7, d, 6)) return false;
    int32_t adcP = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adcT = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    int32_t v1 = ((((adcT >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
    int32_t v2 = (((((adcT >> 4) - (int32_t)dig_T1) * ((adcT >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
    int32_t t_fine = v1 + v2;
    int64_t a = (int64_t)t_fine - 128000;
    int64_t b = a * a * (int64_t)dig_P6;
    b += (a * (int64_t)dig_P5) << 17;
    b += ((int64_t)dig_P4) << 35;
    a = ((a * a * (int64_t)dig_P3) >> 8) + ((a * (int64_t)dig_P2) << 12);
    a = ((((int64_t)1) << 47) + a) * (int64_t)dig_P1 >> 33;
    if (a == 0) return false;
    int64_t p = 1048576 - adcP;
    p = (((p << 31) - b) * 3125) / a;
    a = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    b = ((int64_t)dig_P8 * p) >> 19;
    p = ((p + a + b) >> 8) + ((int64_t)dig_P7 << 4);
    hpa = p / 25600.0f;
    return true;
}

// Bluetooth
class ServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer *) override { connected = true; Serial.println("# watch connected"); }
    void onDisconnect(BLEServer *s) override {
        connected = false; Serial.println("# watch disconnected");
        s->startAdvertising();
    }
};
class ReqCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        unsigned id, age;
        if (sscanf(c->getValue().c_str(), "%u,%u", &id, &age) == 2) {
            req_id = id; req_age = age; req_rx_ms = millis(); req_pending = true;
        }
    }
};

static void bleStart() {
    BLEDevice::init(BLE_NAME);
    BLEDevice::setMTU(185);
    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCB());
    BLEService *svc = server->createService(SERVICE_UUID);
    BLECharacteristic *req = svc->createCharacteristic(REQ_UUID, BLECharacteristic::PROPERTY_WRITE);
    req->setCallbacks(new ReqCB());
    feat_char = svc->createCharacteristic(FEAT_UUID, BLECharacteristic::PROPERTY_READ);
    feat_char->setValue("0,1");
    svc->start();
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
}

// Features for the window around target_ms. Returns false while that window is not complete
// yet; with last_try it answers status 1 instead.
static bool answer(uint32_t id, uint32_t target_ms, bool last_try) {
    static float win[FD_WIN * 6], t_s[BAR_N], p[BAR_N];
    char out[200];
    uint32_t newest = n_samples - 1, k = newest;
    while (k > 0 && newest - k < RING_N - 1 && (int32_t)(ring_ms[k % RING_N] - target_ms) > 0) k--;
    if (k + FD_ALIGN > newest && !last_try) return false;
    if (k >= FD_ALIGN && k + FD_ALIGN <= newest && newest - (k - FD_ALIGN) < RING_N) {   // own |a| peak nearby
        uint32_t c = k; float best = -1;
        for (uint32_t i = c - FD_ALIGN; i <= c + FD_ALIGN; i++) {
            const float *v = ring[i % RING_N];
            float m = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (m > best) { best = m; k = i; }
        }
    }
    if (k + FD_POST - 1 > newest && !last_try) return false;
    if (k < FD_PRE || newest - (k - FD_PRE) >= RING_N || k + FD_POST - 1 > newest) {
        snprintf(out, sizeof out, "%u,1", (unsigned)id);
        feat_char->setValue(out);
        Serial.printf("# request %u: window not in buffer\n", (unsigned)id);
        return true;
    }
    for (int i = 0; i < FD_WIN; i++) memcpy(win + 6 * i, ring[(k - FD_PRE + i) % RING_N], 6 * sizeof(float));
    float z[15];
    fd_features(win, z);
    int n = bar_n < BAR_N ? bar_n : BAR_N;
    for (int i = 0; i < n; i++) {
        uint32_t j = (bar_n - n + i) % BAR_N;
        t_s[i] = (int32_t)(bar_ms[j] - ring_ms[k % RING_N]) / 1000.0f;
        p[i] = bar_p[j];
    }
    z[14] = fd_height_change(t_s, p, n);
    int len = snprintf(out, sizeof out, "%u,0", (unsigned)id);
    for (int i = 0; i < 15 && len < (int)sizeof out - 12; i++) len += snprintf(out + len, sizeof out - len, ",%.5g", z[i]);
    feat_char->setValue(out);
    Serial.printf("# request %u: peak %.2f g, tilt %.0f deg, height change %+.2f m\n", (unsigned)id, z[0], z[12], z[14]);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);
    bool ok = mpuInit() && bmpInit();
    Serial.println(ok ? "# sensors ok" : "# ERROR: MPU6050 or BMP280 not found, check wiring");

    // Gyro offset: average while still; retry if the board moved
    for (int attempt = 0; attempt < 5; attempt++) {
        double s[3] = {0, 0, 0}, s2 = 0; int n = 0; float v[6];
        uint32_t t0 = millis();
        while (millis() - t0 < 3000) {
            if (mpuRead(v)) { for (int k = 0; k < 3; k++) s[k] += v[3 + k]; s2 += v[3] * v[3] + v[4] * v[4] + v[5] * v[5]; n++; }
            delay(5);
        }
        if (!n) break;
        for (int k = 0; k < 3; k++) gbias[k] = s[k] / n;
        float spread = sqrtf(fmaxf(s2 / n - (gbias[0] * gbias[0] + gbias[1] * gbias[1] + gbias[2] * gbias[2]), 0));
        Serial.printf("# gyro offset %.1f %.1f %.1f deg/s (spread %.1f)\n", gbias[0], gbias[1], gbias[2], spread);
        if (spread < 3) break;
        Serial.println("# board moved, measuring again");
    }

    bleStart();
    Serial.printf("# advertising as %s\n", BLE_NAME);
    next_us = (double)esp_timer_get_time() + PERIOD_US;
}

void loop() {
    int64_t now = esp_timer_get_time();
    int guard = 0;
    while ((double)now >= next_us && guard < 8) {
        float v[6];
        if (mpuRead(v)) {
            for (int k = 0; k < 3; k++) v[3 + k] -= gbias[k];
            memcpy(ring[n_samples % RING_N], v, sizeof v);
            ring_ms[n_samples % RING_N] = millis();
            n_samples++;
        }
        next_us += PERIOD_US;
        guard++;
    }
    if (guard >= 8) next_us = (double)now + PERIOD_US;

    uint32_t m = millis();
    if (m - last_bar_ms >= BAR_PERIOD_MS) {
        float hpa;
        if (bmpRead(hpa)) { bar_p[bar_n % BAR_N] = hpa; bar_ms[bar_n % BAR_N] = m; bar_n++; }
        last_bar_ms = m;
    }

    if (req_pending) {
        req_pending = false;
        cur_id = req_id; cur_target = req_rx_ms - req_age - LINK_MS; cur_t0 = millis(); cur_active = true;
    }
    if (cur_active && answer(cur_id, cur_target, millis() - cur_t0 > 800)) cur_active = false;
}
