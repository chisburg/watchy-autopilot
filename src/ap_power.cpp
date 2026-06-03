#include "ap_power.h"

#if AP_DEBUG

#include <Arduino.h>
#include <WiFi.h>

#ifndef BATT_ADC_PIN
#define BATT_ADC_PIN 33
#endif

int apBatteryPercent(float voltage) {
  int pct = (int)((voltage - 3.30f) / (4.20f - 3.30f) * 100.0f);
  if (pct < 0) {
    return 0;
  }
  if (pct > 100) {
    return 100;
  }
  return pct;
}

float apReadBatteryV() {
  return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f * 2.0f;
}

void apLogBattery(const char *tag) {
  const float vbat = apReadBatteryV();
  AP_LOG("[PWR] %s vbat=%.2f pct=%d", tag, vbat, apBatteryPercent(vbat));
}

void apLogPower(const char *tag) {
  const float vbat = apReadBatteryV();
  const int pct = apBatteryPercent(vbat);
  if (WiFi.status() == WL_CONNECTED) {
    AP_LOG("[PWR] %s vbat=%.2f pct=%d rssi=%d wifi=%d", tag, vbat, pct,
           WiFi.RSSI(), (int)WiFi.status());
  } else {
    AP_LOG("[PWR] %s vbat=%.2f pct=%d wifi=%d", tag, vbat, pct,
           (int)WiFi.status());
  }
}

#endif
