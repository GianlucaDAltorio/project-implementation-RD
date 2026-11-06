///////////////////////////////////////////////////////////////////////////
//  Wi-Fi network scanner for Waveshare ESP32-S3-Touch-AMOLED-2.06       //
//  Watch interface test: scans for 2.4 GHz networks and shows them in   //
//  a scrollable list.                                                   //
//  NOTICE: This code was produced by an AI model for quick testing      //
//  and troubleshooting to check if the watch can perform basic          //
//  upload/interface tasks.                                              //
///////////////////////////////////////////////////////////////////////////

#include <Wire.h>
#include <Arduino.h>
#include <WiFi.h>              // Wi-Fi scanning
#include "pin_config.h"        // Board pin definitions (from Waveshare libraries)
#include <lvgl.h>              // LVGL v9 graphics library

#include "Arduino_GFX_Library.h"       // CO5300 AMOLED display driver
#include "Arduino_DriveBus_Library.h"  // I2C driver for FT3168 touch
#include "lv_conf.h"

#include "HWCDC.h"
HWCDC USBSerial;               // Debug output over the native USB port

//  Display / LVGL globals (from Waveshare example 06)
uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;
lv_display_t *disp;
lv_color_t *disp_draw_buf;     // Full-screen drawing buffer

#define DIRECT_RENDER_MODE     // LVGL draws into a full-screen buffer

// QSPI bus connecting the ESP32 to the display
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

// CO5300 AMOLED display (410x502, 22-pixel column offset)
Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT,
                                      22, 0, 0, 0);

// I2C bus and FT3168 touch controller
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS,
                                                       DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// Called by hardware when the screen is touched: just sets a flag
void Arduino_IIC_Touch_Interrupt(void) {
  FT3168->IIC_Interrupt_Flag = true;
}

// Optional LVGL log output to the serial monitor
#if LV_USE_LOG != 0
void my_print(lv_log_level_t level, const char *buf) {
  LV_UNUSED(level);
  USBSerial.println(buf);
  USBSerial.flush();
}
#endif

// Gives LVGL a millisecond clock
uint32_t millis_cb(void) {
  return millis();
}

// LVGL calls this when it finishes drawing. In direct-render mode the actual
// copy to the screen happens in loop(), so we only signal "done" here.
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
#ifndef DIRECT_RENDER_MODE
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
#endif
  lv_disp_flush_ready(disp);
}

// LVGL calls this to read the touchscreen
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  int32_t touchX = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t touchY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

  if (FT3168->IIC_Interrupt_Flag == true) {   // A touch happened
    FT3168->IIC_Interrupt_Flag = false;
    data->state = LV_INDEV_STATE_PR;          // Pressed
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;         // Released
  }
}

// The CO5300 needs redraw areas aligned to even pixel boundaries
void rounder_event_cb(lv_event_t *e) {
  lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
  area->x1 = (area->x1 >> 1) << 1;
  area->y1 = (area->y1 >> 1) << 1;
  area->x2 = ((area->x2 >> 1) << 1) + 1;
  area->y2 = ((area->y2 >> 1) << 1) + 1;
}

// =====================================================================
//                       WI-FI SCANNER UI
// =====================================================================
static lv_obj_t *list;         // Scrollable list of networks
static lv_obj_t *lbl_status;   // Status text at the top ("Scanning...", "5 networks found")
static bool scanning = false;  // True while a scan is in progress

// Starts a non-blocking scan and clears the old list
void startScan() {
  if (scanning) return;                 // Ignore taps while a scan is running
  scanning = true;
  lv_obj_clean(list);                   // Remove old entries from the screen
  lv_label_set_text(lbl_status, "Scanning...");
  WiFi.scanDelete();                    // Free results from the previous scan
  WiFi.scanNetworks(true);              // true = async, returns immediately
}

// "Rescan" button handler
static void onRescan(lv_event_t *e) {
  startScan();
}

// Timer that runs every 500 ms inside LVGL. It checks if the scan has
// finished and, if so, fills the list with the results.
static void pollTimer(lv_timer_t *t) {
  if (!scanning) return;

  int n = WiFi.scanComplete();          // >=0: networks found, -1: still scanning
  if (n >= 0) {
    scanning = false;

    for (int i = 0; i < n; i++) {
      // Build one line of text: name, signal strength, and * if secured
      bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      char text[96];
      snprintf(text, sizeof(text), "%s  (%d dBm)%s",
               WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i), open ? "" : "  *");

      // Add the line to the list with a Wi-Fi icon
      lv_list_add_button(list, LV_SYMBOL_WIFI, text);

      // Also print it to the serial monitor (useful for the logbook)
      USBSerial.printf("%d: %s  %d dBm  %s\n", i + 1, WiFi.SSID(i).c_str(),
                       (int)WiFi.RSSI(i), open ? "open" : "secured");
    }

    lv_label_set_text_fmt(lbl_status, "%d networks found", n);
  } else if (n == WIFI_SCAN_FAILED) {   // -2: scan could not start
    scanning = false;
    lv_label_set_text(lbl_status, "Scan failed");
  }
}

// Creates the screen: status text, network list, and Rescan button
void buildWifiUI() {
  lv_obj_t *scr = lv_screen_active();

  // Status text at the top
  lbl_status = lv_label_create(scr);
  lv_label_set_text(lbl_status, "Wi-Fi");
  lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 15);

  // Scrollable list in the middle
  list = lv_list_create(scr);
  lv_obj_set_size(list, 380, 340);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);

  // Rescan button at the bottom
  lv_obj_t *btn_rescan = lv_button_create(scr);
  lv_obj_align(btn_rescan, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_add_event_cb(btn_rescan, onRescan, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl = lv_label_create(btn_rescan);
  lv_label_set_text(lbl, "Rescan");

  // Start the timer that checks for finished scans
  lv_timer_create(pollTimer, 500, NULL);
}

// =====================================================================

void setup() {
#ifdef DEV_DEVICE_INIT
  DEV_DEVICE_INIT();
#endif

  USBSerial.begin(115200);
  USBSerial.println("Wi-Fi scan test");

  // --- Display ---
  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(RGB565_BLACK);

  // --- Touch ---
  Wire.begin(IIC_SDA, IIC_SCL);
  while (FT3168->begin() == false) {
    USBSerial.println("FT3168 initialization fail");
    delay(2000);
  }
  USBSerial.println("FT3168 initialization successfully");
  FT3168->IIC_Write_Device_State(FT3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                 FT3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);

  // --- LVGL ---
  lv_init();
  lv_tick_set_cb(millis_cb);

#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print);
#endif

  screenWidth = gfx->width();
  screenHeight = gfx->height();

#ifdef DIRECT_RENDER_MODE
  bufSize = screenWidth * screenHeight;
#else
  bufSize = screenWidth * 50;
#endif

  // Allocate the drawing buffer (2 bytes per pixel, RGB565)
  disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {   // Not enough internal RAM: use PSRAM instead
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_8BIT);
  }

  if (!disp_draw_buf) {
    USBSerial.println("LVGL disp_draw_buf allocate failed!");
  } else {
    // Register the display with LVGL
    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, my_disp_flush);
#ifdef DIRECT_RENDER_MODE
    lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
#else
    lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    // Register the touchscreen with LVGL
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    // --- Wi-Fi scanner ---
    WiFi.mode(WIFI_STA);   // Station mode is required for scanning
    buildWifiUI();         // Draw the screen
    startScan();           // Scan once at startup
  }

  USBSerial.println("Setup done");
}

void loop() {
  lv_task_handler();       // Let LVGL process touches, timers, and drawing

#ifdef DIRECT_RENDER_MODE
  // Copy the finished frame to the AMOLED screen
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, screenWidth, screenHeight);
#endif

  delay(5);
}
