/*
  watch_MQTT_test
  MQTT connection test for the watch. Joins Wi-Fi, syncs NTP, connects to the broker on
  the laptop and publishes a test alert each time BOOT is pressed. Output on Serial.
  Library: PubSubClient. Settings: secrets.h.
*/
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <sys/time.h>
#include "secrets.h"

const char* DEVICE_ID    = "watch01";
const char* TOPIC_ALERT  = "pi/dashboard/alert";
const char* TOPIC_STATUS = "pi/dashboard/status";
const int   BUTTON_PIN     = 0;   // BOOT button, active LOW

const char* MSG_ONLINE  = "{\"device\":\"watch01\",\"event\":\"status\",\"msg\":\"watch01 online\"}";
const char* MSG_OFFLINE = "{\"device\":\"watch01\",\"event\":\"status\",\"msg\":\"watch01 OFFLINE\"}";

WiFiClient net;
PubSubClient mqtt(net);

uint32_t alertCount = 0;
uint32_t lastReconnectTry = 0;
uint32_t lastPressMs = 0;
bool lastBtn = HIGH;

uint64_t epochMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK  IP=%s  RSSI=%d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

// NTP time is needed later to measure alert delay (REQ-FD-02).
void syncTime() {
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
  Serial.print("NTP sync");
  struct tm t;
  for (int i = 0; i < 15; i++) {
    if (getLocalTime(&t, 1000)) {
      Serial.println(" OK");
      return;
    }
    Serial.print(".");
  }
  Serial.println(" failed (alerts will have no valid timestamp)");
}

bool connectMQTT() {
  const char* user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
  const char* pass = strlen(MQTT_USER) ? MQTT_PASS : nullptr;

  // Last Will: broker publishes OFFLINE for us if the watch drops off.
  bool ok = mqtt.connect(DEVICE_ID, user, pass, TOPIC_STATUS, 1, true, MSG_OFFLINE);
  if (ok) {
    mqtt.publish(TOPIC_STATUS, MSG_ONLINE, true);
    Serial.println("MQTT connected");
  } else {
    Serial.printf("MQTT failed, state=%d\n", mqtt.state());
  }
  return ok;
}

void publishAlert() {
  alertCount++;

  char hhmmss[9] = "--:--:--";
  struct tm t;
  if (getLocalTime(&t, 0)) strftime(hhmmss, sizeof(hhmmss), "%H:%M:%S", &t);

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"device\":\"%s\",\"event\":\"test\",\"msg\":\"TEST ALERT from %s\","
           "\"n\":%lu,\"ts\":\"%s\",\"ts_ms\":%llu,\"rssi\":%d}",
           DEVICE_ID, DEVICE_ID, (unsigned long)alertCount, hhmmss,
           (unsigned long long)epochMs(), WiFi.RSSI());

  bool ok = mqtt.publish(TOPIC_ALERT, payload);
  Serial.printf("%s %s\n", ok ? "SENT  " : "FAILED", payload);
}

void setup() {
  Serial.begin(115200);
  delay(1500);   // let USB serial come up
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  connectWiFi();
  syncTime();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(5);
  connectMQTT();

  Serial.println("Press BOOT to send a test alert.");
}

void loop() {
  if (mqtt.connected()) {
    mqtt.loop();
  } else if (millis() - lastReconnectTry > 2000) {
    lastReconnectTry = millis();
    if (WiFi.status() == WL_CONNECTED) connectMQTT();
    else Serial.println("WiFi down, waiting...");
  }

  bool btn = digitalRead(BUTTON_PIN);
  if (lastBtn == HIGH && btn == LOW && millis() - lastPressMs > 250) {
    lastPressMs = millis();
    if (mqtt.connected()) publishAlert();
    else Serial.println("Not connected - alert NOT sent");
  }
  lastBtn = btn;
}
