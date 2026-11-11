/*
 * FallDetector_trials - wrist fall detector with trial logging
 * Target: Waveshare ESP32-S3-Touch-AMOLED-2.06 (QMI8658 IMU, CO5300 AMOLED, microSD)
 *
 * Samples the IMU at 238 Hz, runs the impact-triggered fall classifier (fall_detect.c) and
 * shows the result on the display. Every impact window and every button mark is logged to
 * the SD card so test sessions can be scored offline with src/session_analysis.
 *
 * Tasks: core 1 (loop) samples and classifies; core 0 runs the display and SD tasks.
 *
 * BOOT button
 *   while FALL! is shown     cancel the alert
 *   hold 1 s                 void the current trial
 *   one press                mark a real-fall trial   (yellow banner)
 *   two presses within 0.5 s mark a non-fall trial    (blue banner)
 *
 * Only the first FALL inside a trial window raises an alert; later ones in the same window
 * are logged with called = "suppressed". Outside a trial window every FALL raises an alert.
 *
 * SD card files (n = session number, incremented at each power-up)
 *   /events_nnn.csv   START, MARK, IMPACT, CANCEL and VOID rows; IMPACT rows carry the peak,
 *                     fall probability, decision and the 14 features
 *   /raw_nnn.bin      every IMU sample, 16 bytes each; convert with raw_to_csv.py
 *
 * Build: Arduino IDE, board "ESP32S3 Dev Module", USB CDC On Boot enabled, OPI PSRAM.
 * Libraries: SensorLib 0.3.1, Arduino_GFX 1.6.0 (from Waveshare's examples), SD_MMC (ESP32 core).
 * Insert a FAT32 microSD card before powering on. No alert is sent yet; "ALERT" marks where
 * the MQTT message will be published.
 */
#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <FS.h>
#include <SD_MMC.h>
#include "SensorQMI8658.hpp"
#include "Arduino_GFX_Library.h"
#include "esp_timer.h"
extern "C" {
#include "fall_detect.h"
}

static void displayTask(void *);
static void sdTask(void *);

// Pins, as in Waveshare's pin_config.h
#define IMU_SDA 15
#define IMU_SCL 14
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 11
#define LCD_CS 12
#define LCD_RESET 8
#define LCD_WIDTH 410
#define LCD_HEIGHT 502
#define SDMMC_CLK 2
#define SDMMC_CMD 1
#define SDMMC_DATA 3
#define BOOT_BTN 0

#define RAW_LOG 1              // 1 = log every IMU sample to the SD card (about 3.8 KB/s)
#define PRINT_RAW 0            // 1 = also print every sample over USB serial
#define CANCEL_WINDOW_MS 15000
#define SENT_SHOW_MS 5000
#define CANCEL_SHOW_MS 3000
#define DOUBLE_PRESS_MS 500     // second press within this time counts as a double press
#define HOLD_MS 1000            // hold time that voids the current trial
#define TRIAL_WINDOW_MS 15000   // trial length; analyze_session.py uses the same 15 s
#define BRIGHTNESS 120         // 0-255

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 22, 0, 0, 0);

static SensorQMI8658 qmi;
static fd_t fd;
static const double PERIOD_US = 1000000.0 / FD_FS;
static double next_us;

// State shared between cores (written by core 1, read by core 0)
struct Ev { uint32_t id, sec; float peak, p; bool fall; };
static Ev events[4];
static volatile uint32_t ev_count = 0;       // impact windows evaluated
static volatile uint32_t n_falls = 0, n_marks = 0;
static volatile uint32_t fall_ms = 0;        // millis() at the active fall, 0 = none
static volatile uint32_t cancel_ms = 0;
static volatile float live_a = 1.0f, live_g = 0.0f, live_rate = 0.0f;
static volatile uint32_t read_fail = 0;
static volatile int last_label = 0;          // 0 none, 1 fall trial, 2 non-fall trial
static volatile uint32_t trial_start_ms = 0;
static bool trial_fall_called = false;       // a FALL was already called in the current trial

// SD logging buffers
struct __attribute__((packed)) Rec { uint32_t t_ms; int16_t v[6]; };   // 16 bytes per IMU sample
#define RAW_N 2048                                                      // about 8.6 s of samples
static Rec rawbuf[RAW_N];
static volatile uint32_t raw_head = 0, raw_tail = 0, raw_drop = 0;
#define EVQ_N 16
static char evq[EVQ_N][220];
static volatile uint32_t evq_head = 0, evq_tail = 0;
static volatile int sd_state = 0;            // 0 starting, 1 logging, 2 no card / error
static volatile uint32_t sd_session = 0, sd_kb = 0;

static void log_line(const char *s) {        // core 1 only
    uint32_t h = evq_head;
    if (h - evq_tail < EVQ_N) {
        strncpy(evq[h % EVQ_N], s, sizeof evq[0] - 1); evq[h % EVQ_N][sizeof evq[0] - 1] = 0;
        __sync_synchronize(); evq_head = h + 1;
    }
}
static int16_t to_counts(float v, float scale) {
    long c = lroundf(v / scale); if (c > 32767) c = 32767; if (c < -32768) c = -32768; return (int16_t)c;
}

// Detection and sampling (core 1)
static void on_fall_detected() {
    // TODO: after the cancel window, publish the MQTT alert
    n_falls++;
    cancel_ms = 0;
    uint32_t m = millis(); fall_ms = m ? m : 1;
    Serial.printf("FALL DETECTED  (p=%.2f, total=%u, t=%lu ms)\n", fd.last_prob, (unsigned)n_falls, (unsigned long)millis());
}

static void emit_mark(uint32_t t_ms, bool is_fall_trial) {
    n_marks++; last_label = is_fall_trial ? 1 : 2; trial_start_ms = t_ms; trial_fall_called = false;
    char line[96];
    snprintf(line, sizeof line, "MARK,%lu,%u,,,%s,,,,,,,,,,,,,,", (unsigned long)t_ms, (unsigned)n_marks, is_fall_trial ? "FALLTRIAL" : "NOFALL");
    log_line(line);
    Serial.printf("# MARK %u  %s\n", (unsigned)n_marks, is_fall_trial ? "FALL trial" : "NON-FALL trial");
}

static uint32_t seen_eval = 0, n_samples = 0;
static double sum_a = 0, sum_g = 0;
static int64_t last_pub_us = 0, last_status_us = 0;
static bool btn_prev = false, hold_done = true; static uint32_t btn_t = 0, pend_ms = 0, no_mark_until = 0, btn_down_ms = 0;

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);                // do not block when no PC is connected
    pinMode(BOOT_BTN, INPUT_PULLUP);

    gfx->begin();
    gfx->setBrightness(BRIGHTNESS);
    gfx->fillScreen(RGB565_BLACK);
    gfx->setTextSize(3); gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->setCursor(20, 200); gfx->print("Starting IMU...");

    bool ok = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IMU_SDA, IMU_SCL);
    if (!ok) ok = qmi.begin(Wire, QMI8658_H_SLAVE_ADDRESS, IMU_SDA, IMU_SCL);
    if (!ok) {
        gfx->fillScreen(RGB565_RED); gfx->setTextColor(RGB565_WHITE, RGB565_RED);
        gfx->setCursor(20, 200); gfx->print("IMU NOT FOUND");
        Serial.println("# ERROR: QMI8658 not found");
        while (true) delay(1000);
    }
    Wire.setClock(400000);
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_8G, SensorQMI8658::ACC_ODR_500Hz, SensorQMI8658::LPF_MODE_3);
    qmi.configGyroscope(SensorQMI8658::GYR_RANGE_1024DPS, SensorQMI8658::GYR_ODR_448_4Hz, SensorQMI8658::LPF_MODE_3);
    qmi.enableGyroscope();
    qmi.enableAccelerometer();
    delay(100);

    fd_init(&fd);
    gfx->fillScreen(RGB565_BLACK);
    xTaskCreatePinnedToCore(sdTask, "sd", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(displayTask, "disp", 8192, NULL, 1, NULL, 0);

    next_us = (double)esp_timer_get_time() + PERIOD_US;
    last_pub_us = last_status_us = esp_timer_get_time();
    Serial.println("# Running");
}

void loop() {
    int64_t now = esp_timer_get_time();
    int guard = 0;
    while ((double)now >= next_us && guard < 8) {
        float ax, ay, az, gx, gy, gz;
        if (qmi.getAccelerometer(ax, ay, az) && qmi.getGyroscope(gx, gy, gz)) {
#if PRINT_RAW
            Serial.printf("%lu,%.5f,%.5f,%.5f,%.3f,%.3f,%.3f\n", (unsigned long)millis(), ax, ay, az, gx, gy, gz);
#endif
#if RAW_LOG
            {   // scales: accel 8 g / 32768, gyro 1024 dps / 32768 (same in raw_to_csv.py)
                uint32_t h = raw_head;
                if (h - raw_tail < RAW_N) {
                    Rec &r = rawbuf[h % RAW_N]; r.t_ms = millis();
                    r.v[0] = to_counts(ax, 8.0f / 32768); r.v[1] = to_counts(ay, 8.0f / 32768); r.v[2] = to_counts(az, 8.0f / 32768);
                    r.v[3] = to_counts(gx, 1024.0f / 32768); r.v[4] = to_counts(gy, 1024.0f / 32768); r.v[5] = to_counts(gz, 1024.0f / 32768);
                    __sync_synchronize(); raw_head = h + 1;
                } else raw_drop++;
            }
#endif
            sum_a += sqrt((double)ax*ax + (double)ay*ay + (double)az*az);
            sum_g += sqrt((double)gx*gx + (double)gy*gy + (double)gz*gz);
            n_samples++;
            int fell = fd_push(&fd, ax, ay, az, gx, gy, gz);
            bool suppressed = false;
            if (fell && last_label != 0 && millis() - trial_start_ms < TRIAL_WINDOW_MS) {
                if (trial_fall_called) { fell = 0; suppressed = true; }   // one FALL per trial window
                else trial_fall_called = true;
            }
            if (fd.n_eval != seen_eval) {                       // a window was just evaluated
                seen_eval = fd.n_eval;
                uint32_t id = ev_count + 1;
                Ev &e = events[ev_count % 4];
                e.id = id; e.sec = millis() / 1000; e.peak = fd.last_peak_g; e.p = fd.last_prob; e.fall = fell != 0;
                char line[220]; int n = snprintf(line, sizeof line, "IMPACT,%lu,%u,%.3f,%.4f,%s", (unsigned long)millis(), (unsigned)id,
                                                 fd.last_peak_g, fd.last_prob, fell ? "FALL" : suppressed ? "suppressed" : "ignored");
                for (int k = 0; k < 14 && n < (int)sizeof line - 16; k++) n += snprintf(line + n, sizeof line - n, ",%.4f", fd.last_feat[k]);
                log_line(line);
                ev_count = id;
                Serial.printf("# impact #%u %.1f g  p=%.2f  %s\n", (unsigned)id, e.peak, e.p, fell ? "FALL" : suppressed ? "suppressed" : "ignored");
            }
            if (fell) on_fall_detected();
        } else {
            read_fail++;
        }
        next_us += PERIOD_US;
        guard++;
    }
    if (guard >= 8) next_us = (double)now + PERIOD_US;

    // BOOT button: cancel, or 1 press = fall trial, 2 presses = non-fall trial, hold = void
    bool b = digitalRead(BOOT_BTN) == LOW;
    uint32_t nowm = millis();
    if (b && !btn_prev && nowm - btn_t > 60) {                   // 60 ms debounce
        btn_t = nowm; btn_down_ms = nowm; hold_done = false;
        if (fall_ms && (nowm - fall_ms) < CANCEL_WINDOW_MS) {
            cancel_ms = nowm; fall_ms = 0; pend_ms = 0; no_mark_until = nowm + 1500; hold_done = true;   // ignore presses right after a cancel
            char line[64]; snprintf(line, sizeof line, "CANCEL,%lu,,,,,,,,,,,,,,,,,,", (unsigned long)nowm); log_line(line);
            Serial.println("# alert cancelled");
        } else if (nowm < no_mark_until) {
            /* too soon after a cancel */
        } else if (pend_ms && nowm - pend_ms < DOUBLE_PRESS_MS) {
            emit_mark(pend_ms, false); pend_ms = 0;               // second press: non-fall trial, stamped at the first press
        } else {
            pend_ms = nowm;                                       // first press: wait for a possible second
        }
    }
    btn_prev = b;
    if (pend_ms && millis() - pend_ms >= DOUBLE_PRESS_MS) { emit_mark(pend_ms, true); pend_ms = 0; }   // no second press: fall trial
    if (b && !hold_done && nowm - btn_down_ms >= HOLD_MS) {      // held: void the current trial
        hold_done = true; pend_ms = 0;
        if (last_label != 0) {
            char line[64]; snprintf(line, sizeof line, "VOID,%lu,%u,,,,,,,,,,,,,,,,,", (unsigned long)nowm, (unsigned)n_marks); log_line(line);
            Serial.printf("# trial %u VOIDED\n", (unsigned)n_marks);
            last_label = 0;
        }
    }

    if (now - last_pub_us >= 250000) {                           // live values, 4 Hz
        if (n_samples) {
            live_a = sum_a / n_samples; live_g = sum_g / n_samples;
            live_rate = n_samples / ((now - last_pub_us) / 1e6);
        }
        n_samples = 0; sum_a = sum_g = 0; last_pub_us = now;
    }
    if (now - last_status_us >= 2000000) {
        Serial.printf("# rate=%.1f Hz (want %d)  |a|=%.2f g  |g|=%.1f dps  read_fail=%u  falls=%u  raw_drop=%u\n",
                      (float)live_rate, FD_FS, (float)live_a, (float)live_g, (unsigned)read_fail, (unsigned)n_falls, (unsigned)raw_drop);
        last_status_us = now;
    }
}

// SD card writer (core 0)
static void sdTask(void *) {
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    if (!SD_MMC.begin("/sdcard", true) || SD_MMC.cardType() == CARD_NONE) { sd_state = 2; Serial.println("# SD: no card / mount failed"); vTaskDelete(NULL); return; }
    char pe[40], pr[40]; uint32_t n = 1;
    for (; n < 1000; n++) { snprintf(pe, sizeof pe, "/events_%03u.csv", (unsigned)n); if (!SD_MMC.exists(pe)) break; }
    snprintf(pr, sizeof pr, "/raw_%03u.bin", (unsigned)n);
    File fe = SD_MMC.open(pe, FILE_WRITE);
#if RAW_LOG
    File fr = SD_MMC.open(pr, FILE_WRITE);
    if (!fe || !fr) { sd_state = 2; Serial.println("# SD: cannot create files"); vTaskDelete(NULL); return; }
#else
    if (!fe) { sd_state = 2; Serial.println("# SD: cannot create file"); vTaskDelete(NULL); return; }
#endif
    fe.print("kind,boot_ms,id,peak_g,p,called,peak,pre_min,pre_std,pre_max,post_dev,post_std,post_max,jerk,gyr_peak,gyr_pre,gyr_post,gyr_post_late,tilt_deg,dur_hi\n");
    fe.printf("START,%lu,%u,,,,,,,,,,,,,,,,,\n", (unsigned long)millis(), (unsigned)n);
    fe.flush();
    sd_session = n; sd_state = 1;
    Serial.printf("# SD: session %u -> %s\n", (unsigned)n, pe);

    uint32_t last_flush = millis(); uint64_t bytes = 0;
    for (;;) {
        bool wrote = false;
        while (evq_tail != evq_head) {                           // event rows: write and flush each
            fe.print(evq[evq_tail % EVQ_N]); fe.print("\n"); fe.flush();
            evq_tail = evq_tail + 1; wrote = true;
        }
#if RAW_LOG
        uint32_t h = raw_head, t = raw_tail;
        while (t != h) {                                         // raw samples, in contiguous chunks
            uint32_t idx = t % RAW_N, run = h - t; if (run > RAW_N - idx) run = RAW_N - idx; if (run > 256) run = 256;
            fr.write((const uint8_t *)&rawbuf[idx], run * sizeof(Rec));
            t += run; raw_tail = t; bytes += run * sizeof(Rec); wrote = true;
        }
        if (millis() - last_flush > 1000) { fr.flush(); last_flush = millis(); sd_kb = (uint32_t)(bytes / 1024); }
#endif
        if (!wrote) vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Display (core 0)
enum { S_READY, S_CHECK, S_FALL, S_SENT, S_CANCEL };

static int current_state(uint32_t t) {
    if (cancel_ms && t - cancel_ms < CANCEL_SHOW_MS) return S_CANCEL;
    uint32_t f = fall_ms;
    if (f) {
        uint32_t dt = t - f;
        if (dt < CANCEL_WINDOW_MS) return S_FALL;
        if (dt < CANCEL_WINDOW_MS + SENT_SHOW_MS) return S_SENT;
    }
    if (fd.state != 0) return S_CHECK;
    return S_READY;
}

struct Slot { char s[40]; uint16_t fg, bg; };
static Slot slots[20];
static void put(int id, int x, int y, int size, uint16_t fg, uint16_t bg, const char *s) {
    Slot &c = slots[id];
    if (c.fg == fg && c.bg == bg && strcmp(c.s, s) == 0) return;       // unchanged
    strncpy(c.s, s, 39); c.s[39] = 0; c.fg = fg; c.bg = bg;
    gfx->setTextSize(size); gfx->setTextColor(fg, bg); gfx->setCursor(x, y); gfx->print(s);
}
static void put_centered(int id, int y, int size, uint16_t fg, uint16_t bg, const char *s) {
    int x = (LCD_WIDTH - (int)strlen(s) * 6 * size) / 2; if (x < 0) x = 0;
    put(id, x, y, size, fg, bg, s);
}

static void displayTask(void *) {
    memset(slots, 0, sizeof slots);
    int shown_state = -1, shown_trial = -1;
    for (;;) {
        uint32_t t = millis();
        // trial banner
        int tt = last_label; uint32_t tel = t - trial_start_ms;
        if (tt && tel >= TRIAL_WINDOW_MS) tt = 0;
        uint16_t tbg = tt == 1 ? RGB565(255, 200, 0) : (tt == 2 ? RGB565(0, 150, 255) : RGB565(40, 40, 40));
        uint16_t tfg = tt == 0 ? RGB565_WHITE : RGB565_BLACK;
        if (tt != shown_trial) { gfx->fillRect(0, 0, LCD_WIDTH, 46, tbg); slots[0].s[0] = 0; slots[0].fg = 0xFFFF; shown_trial = tt; }
        char tb[40];
        if (tt) snprintf(tb, sizeof tb, "#%-2u %s  %2ds", (unsigned)n_marks, tt == 1 ? "REAL FALL" : "NON-FALL", (int)((TRIAL_WINDOW_MS - tel) / 1000));
        else    snprintf(tb, sizeof tb, "NO TRIAL");
        put_centered(0, 12, 3, tfg, tbg, tb);
        int st = current_state(t);
        uint16_t bg; const char *big; char sub[40];
        switch (st) {
            case S_CHECK:  bg = RGB565(200, 110, 0); big = "CHECKING"; snprintf(sub, sizeof sub, "impact seen, analysing"); break;
            case S_FALL: { bg = RGB565(200, 0, 0); big = "FALL!";
                           int left = (CANCEL_WINDOW_MS - (int)(t - fall_ms) + 999) / 1000; if (left < 0) left = 0;
                           snprintf(sub, sizeof sub, "BOOT = cancel   %2d s", left); break; }
            case S_SENT:   bg = RGB565(130, 0, 130); big = "ALERT"; snprintf(sub, sizeof sub, "would be sent now"); break;
            case S_CANCEL: bg = RGB565(0, 60, 170); big = "CANCELLED"; snprintf(sub, sizeof sub, "no alert sent"); break;
            default:       bg = RGB565(0, 90, 0);  big = "READY";  snprintf(sub, sizeof sub, "monitoring"); break;
        }
        if (st != shown_state) {                                  // state changed: repaint the box
            gfx->fillRect(0, 50, LCD_WIDTH, 150, bg);
            slots[1].s[0] = slots[2].s[0] = 0; slots[1].fg = slots[2].fg = 0xFFFF;
            shown_state = st;
        }
        put_centered(1, 80, 6, RGB565_WHITE, bg, big);
        put_centered(2, 160, 2, RGB565_WHITE, bg, sub);

        char b[48];
        snprintf(b, sizeof b, "|a| %5.2f g   ", (float)live_a);  put(3, 20, 212, 3, RGB565_WHITE, RGB565_BLACK, b);
        snprintf(b, sizeof b, "|g| %5.1f dps ", (float)live_g);  put(4, 20, 244, 3, RGB565_WHITE, RGB565_BLACK, b);

        snprintf(b, sizeof b, "IMPACTS (fall if p>=%.2f)", fd_threshold());
        put(5, 20, 285, 2, RGB565_CYAN, RGB565_BLACK, b);
        uint32_t n = ev_count;
        for (int i = 0; i < 4; i++) {
            if (n > (uint32_t)i) {
                Ev e = events[(n - 1 - i) % 4];
                snprintf(b, sizeof b, "#%-3u %4us %4.1fg p=%.2f %-6s ", (unsigned)e.id, (unsigned)e.sec, e.peak, e.p, e.fall ? "FALL" : "ignore");
                put(6 + i, 20, 313 + i * 28, 2, e.fall ? RGB565_RED : RGB565_WHITE, RGB565_BLACK, b);
            } else {
                put(6 + i, 20, 313 + i * 28, 2, RGB565_DARKGREY, RGB565_BLACK, "-                       ");
            }
        }
        const char *sds = sd_state == 1 ? "ok" : (sd_state == 2 ? "NONE" : "...");
        snprintf(b, sizeof b, "trials:%u  SD:%s %uKB   ", (unsigned)n_marks, sds, (unsigned)sd_kb);
        put(10, 20, 436, 2, sd_state == 2 ? RGB565_RED : RGB565_WHITE, RGB565_BLACK, b);
        snprintf(b, sizeof b, "falls:%u up:%lus %3.0fHz drop:%u  ", (unsigned)n_falls, (unsigned long)(t / 1000), (float)live_rate, (unsigned)raw_drop);
        put(11, 20, 466, 2, RGB565_DARKGREY, RGB565_BLACK, b);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
