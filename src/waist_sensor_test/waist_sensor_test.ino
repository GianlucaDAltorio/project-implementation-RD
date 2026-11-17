/*
 * waist_sensor_test
 * Checks the waist board: ESP32 + MPU6050 (accel/gyro) + BMP280 (pressure), both on I2C.
 *
 * At start-up it checks each chip's ID and runs a 2 s still test (keep the board still),
 * then prints PASS/FAIL. After that it prints readings 10 times a second.
 * Lift the board about 1 m: the height column should rise by roughly 1 m.
 *
 * Wiring: SDA GPIO 21, SCL GPIO 22, both sensors on 3V3, MPU6050 AD0 and BMP280 SDO to GND.
 * No extra libraries. Serial Monitor at 115200 baud.
 */
#include <Wire.h>

#define SDA_PIN 21
#define SCL_PIN 22
#define MPU_ADDR 0x68
#define BMP_ADDR 0x76

// I2C helpers
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
static bool present(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// MPU6050: +-8 g (4096 LSB/g), +-1000 dps (32.8 LSB/dps)
static bool mpuInit(uint8_t &who) {
    if (!readRegs(MPU_ADDR, 0x75, &who, 1)) return false;   // WHO_AM_I
    writeReg(MPU_ADDR, 0x6B, 0x00);                          // wake up
    delay(50);
    writeReg(MPU_ADDR, 0x1A, 0x03);                          // low-pass filter about 44 Hz
    writeReg(MPU_ADDR, 0x1B, 0x10);                          // gyro +-1000 dps
    writeReg(MPU_ADDR, 0x1C, 0x10);                          // accel +-8 g
    return true;
}
static bool mpuRead(float &ax, float &ay, float &az, float &gx, float &gy, float &gz) {
    uint8_t b[14];
    if (!readRegs(MPU_ADDR, 0x3B, b, 14)) return false;
    auto s16 = [&](int i) { return (int16_t)((b[i] << 8) | b[i + 1]); };
    ax = s16(0) / 4096.0f;  ay = s16(2) / 4096.0f;  az = s16(4) / 4096.0f;
    gx = s16(8) / 32.8f;    gy = s16(10) / 32.8f;   gz = s16(12) / 32.8f;
    return true;
}

// BMP280: calibration and compensation from the Bosch datasheet
static uint16_t dig_T1, dig_P1;
static int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static int32_t t_fine;

static bool bmpInit(uint8_t &id) {
    if (!readRegs(BMP_ADDR, 0xD0, &id, 1)) return false;
    uint8_t c[24];
    if (!readRegs(BMP_ADDR, 0x88, c, 24)) return false;
    auto u16 = [&](int i) { return (uint16_t)(c[i] | (c[i + 1] << 8)); };
    dig_T1 = u16(0);  dig_T2 = (int16_t)u16(2);  dig_T3 = (int16_t)u16(4);
    dig_P1 = u16(6);  dig_P2 = (int16_t)u16(8);  dig_P3 = (int16_t)u16(10); dig_P4 = (int16_t)u16(12);
    dig_P5 = (int16_t)u16(14); dig_P6 = (int16_t)u16(16); dig_P7 = (int16_t)u16(18);
    dig_P8 = (int16_t)u16(20); dig_P9 = (int16_t)u16(22);
    writeReg(BMP_ADDR, 0xF5, 0x10);   // filter x16, 0.5 ms standby
    writeReg(BMP_ADDR, 0xF4, 0x57);   // temp x2, pressure x16, normal mode
    delay(100);
    return true;
}
static bool bmpRead(float &tempC, float &pressPa) {
    uint8_t d[6];
    if (!readRegs(BMP_ADDR, 0xF7, d, 6)) return false;
    int32_t adcP = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adcT = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);

    int32_t v1 = ((((adcT >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
    int32_t v2 = (((((adcT >> 4) - (int32_t)dig_T1) * ((adcT >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
    t_fine = v1 + v2;
    tempC = ((t_fine * 5 + 128) >> 8) / 100.0f;

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
    pressPa = p / 256.0f;
    return true;
}
static float heightM(float pa) { return 44330.0f * (1.0f - powf(pa / 101325.0f, 0.1903f)); }

static bool mpuOk = false, bmpOk = false;
static float h0 = 0;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);
    Serial.println("\n=== Waist board sensor test ===");

    // Chip IDs
    uint8_t who = 0, id = 0;
    if (!present(MPU_ADDR)) Serial.println("FAIL  MPU6050 not found at 0x68 (check SDA/SCL, power, AD0 to GND)");
    else if (mpuInit(who)) {
        mpuOk = true;
        if (who == 0x68) Serial.println("PASS  MPU6050 found (WHO_AM_I 0x68)");
        else Serial.printf("WARN  MPU6050 answered with WHO_AM_I 0x%02X (likely a clone, readings may still be fine)\n", who);
    }
    if (!present(BMP_ADDR)) Serial.println("FAIL  BMP280 not found at 0x76 (check SDO to GND, CSB to 3V3; try 0x77)");
    else if (bmpInit(id)) {
        if (id == 0x58)      { bmpOk = true; Serial.println("PASS  BMP280 found (chip ID 0x58)"); }
        else if (id == 0x60) { bmpOk = true; Serial.println("PASS  BME280 found (chip ID 0x60), works the same here"); }
        else Serial.printf("FAIL  chip at 0x76 has ID 0x%02X, not a BMP280\n", id);
    }

    // Still test
    Serial.println("Keep the board still for 2 s...");
    double sa = 0, sg = 0, sp = 0; int n = 0, np = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < 2000) {
        float ax, ay, az, gx, gy, gz, tc, pa;
        if (mpuOk && mpuRead(ax, ay, az, gx, gy, gz)) {
            sa += sqrtf(ax * ax + ay * ay + az * az);
            sg += sqrtf(gx * gx + gy * gy + gz * gz);
            n++;
        }
        if (bmpOk && bmpRead(tc, pa)) { sp += pa; np++; }
        delay(10);
    }
    if (mpuOk) {
        float a = n ? sa / n : 0, g = n ? sg / n : 0;
        Serial.printf("%s  |a| at rest = %.3f g (expect 0.95-1.05)\n", (a > 0.95f && a < 1.05f) ? "PASS" : "FAIL", a);
        Serial.printf("%s  |g| at rest = %.1f dps (expect under 5; a few dps is normal gyro offset)\n", g < 5 ? "PASS" : "WARN", g);
    }
    if (bmpOk) {
        float p = np ? sp / np : 0;
        h0 = heightM(p);
        Serial.printf("%s  pressure = %.1f hPa (expect 950-1050 near sea level)\n", (p > 95000 && p < 105000) ? "PASS" : "FAIL", p / 100.0f);
    }
    Serial.println("\n|a| g    ax     ay     az    |  gx     gy     gz  dps |  hPa      degC  height m");
}

void loop() {
    float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0, tc = 0, pa = 0;
    bool m = mpuOk && mpuRead(ax, ay, az, gx, gy, gz);
    bool b = bmpOk && bmpRead(tc, pa);
    if (m) Serial.printf("%5.2f %6.2f %6.2f %6.2f | %6.1f %6.1f %6.1f |", sqrtf(ax * ax + ay * ay + az * az), ax, ay, az, gx, gy, gz);
    else   Serial.print("  MPU6050 read failed                          |");
    if (b) Serial.printf(" %7.2f  %5.1f  %+6.2f\n", pa / 100.0f, tc, heightM(pa) - h0);
    else   Serial.println(" BMP280 read failed");
    delay(100);
}
