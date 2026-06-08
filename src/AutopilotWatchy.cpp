#include "AutopilotWatchy.h"

#include "ap_power.h"
#include "display_session.h"
#include <cstring>
#include <stdarg.h>

#include "esp_sleep.h"

#if !SIM_MODE
#include <WiFi.h>
#endif

#ifdef ARDUINO_ESP32S3_DEV
#define BTN_ACTIVE 0
#else
#define BTN_ACTIVE 1
#endif

// Use Watchy config.h masks (BIT64) — avoid broken GPIO_SEL_* in some PIO variants
#ifndef AP_MENU_BTN_MASK
#define AP_MENU_BTN_MASK (BIT64(MENU_BTN_PIN))
#define AP_UP_BTN_MASK (BIT64(UP_BTN_PIN))
#define AP_DOWN_BTN_MASK (BIT64(DOWN_BTN_PIN))
#define AP_BACK_BTN_MASK (BIT64(BACK_BTN_PIN))
#endif

RTC_DATA_ATTR float AutopilotWatchy::targetHeadingDeg = 335.0f;
RTC_DATA_ATTR float AutopilotWatchy::windAngleDeg = 0.0f;
RTC_DATA_ATTR ApDisplayState AutopilotWatchy::apState = ApDisplayState::Auto;
RTC_DATA_ATTR bool AutopilotWatchy::targetValid = true;
RTC_DATA_ATTR bool AutopilotWatchy::windValid = false;
RTC_DATA_ATTR bool AutopilotWatchy::skLinked = false;
RTC_DATA_ATTR char AutopilotWatchy::profileLabel[8] = "";
RTC_DATA_ATTR int8_t AutopilotWatchy::pendingWakeHeadingDelta = 0;
RTC_DATA_ATTR int8_t AutopilotWatchy::pendingWakeMenuPresses = 0;

namespace {

volatile uint8_t wakeHeadingEdges = 0;

bool headingBtnPressed(uint64_t btnMask) {
  if (btnMask == AP_DOWN_BTN_MASK) {
    return digitalRead(DOWN_BTN_PIN) == BTN_ACTIVE;
  }
  if (btnMask == AP_UP_BTN_MASK) {
    return digitalRead(UP_BTN_PIN) == BTN_ACTIVE;
  }
  return false;
}

void IRAM_ATTR wakeHeadingEdgeIsr() {
  static uint32_t lastUs = 0;
  const uint32_t now = micros();
  if (now - lastUs < (BTN_DEBOUNCE_MS * 1000UL)) {
    return;
  }
  lastUs = now;
  if (wakeHeadingEdges < 10) {
    wakeHeadingEdges++;
  }
}

} // namespace

void AutopilotWatchy::captureWakeHeadingDeltaEarly() {
  pendingWakeHeadingDelta = 0;
  wakeHeadingEdges = 0;
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) {
    return;
  }
  const uint64_t w = esp_sleep_get_ext1_wakeup_status();
  if (w != AP_UP_BTN_MASK && w != AP_DOWN_BTN_MASK) {
    return;
  }

  const bool up = (w == AP_UP_BTN_MASK);
  const int pin = up ? UP_BTN_PIN : DOWN_BTN_PIN;
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);

  wakeHeadingEdges = 1;
  attachInterrupt(digitalPinToInterrupt(pin), wakeHeadingEdgeIsr, RISING);

  unsigned long deadline = millis() + BTN_WAKE_BURST_MS;
  while ((long)(deadline - millis()) > 0) {
    if (headingBtnPressed(w)) {
      unsigned long heldMs = 0;
      while (headingBtnPressed(w)) {
        delay(10);
        heldMs += 10;
        if (heldMs >= BTN_HOLD_ADJUST_MS) {
          detachInterrupt(digitalPinToInterrupt(pin));
          while (headingBtnPressed(w)) {
            delay(5);
          }
          pendingWakeHeadingDelta = up ? 10 : -10;
          return;
        }
      }
    }
    delay(5);
  }

  detachInterrupt(digitalPinToInterrupt(pin));

  int count = (int)wakeHeadingEdges;
  if (count > 5) {
    count = 5;
  }
  pendingWakeHeadingDelta = (int8_t)(up ? count : -count);
}

void AutopilotWatchy::captureWakeMenuPressesEarly() {
  pendingWakeMenuPresses = 0;
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) {
    return;
  }
  if (esp_sleep_get_ext1_wakeup_status() != AP_MENU_BTN_MASK) {
    return;
  }

  pinMode(MENU_BTN_PIN, INPUT);

  unsigned long heldMs = 0;
  while (digitalRead(MENU_BTN_PIN) == BTN_ACTIVE) {
    delay(10);
    heldMs += 10;
    if (heldMs >= BTN_HOLD_STANDBY_MS) {
      pendingWakeMenuPresses = -1;
      return;
    }
  }

  int presses = 1;
  unsigned long deadline = millis() + BTN_WAKE_BURST_MS;
  while ((long)(deadline - millis()) > 0) {
    if (digitalRead(MENU_BTN_PIN) == BTN_ACTIVE) {
      delay(BTN_DEBOUNCE_MS);
      while (digitalRead(MENU_BTN_PIN) == BTN_ACTIVE) {
        delay(5);
      }
      presses++;
      if (presses >= 2) {
        break;
      }
      deadline = millis() + BTN_SELECT_MULTI_WINDOW_MS;
    }
    delay(5);
  }
  pendingWakeMenuPresses = (int8_t)presses;
}

int AutopilotWatchy::consumePendingWakeMenuPresses() {
  const int presses = (int)pendingWakeMenuPresses;
  pendingWakeMenuPresses = 0;
  return presses;
}

ApCommand AutopilotWatchy::menuCmdFromLongHold() {
  if (apState == ApDisplayState::Standby) {
    return ApCommand::SetAuto;
  }
  if (apState == ApDisplayState::Auto || apState == ApDisplayState::Wind) {
    return ApCommand::SetStandby;
  }
  return ApCommand::None;
}

ApCommand AutopilotWatchy::menuCmdFromPressCount(int presses) {
  if (presses < 0) {
    return menuCmdFromLongHold();
  }
  if (presses >= 2) {
    if (apState == ApDisplayState::Auto) {
      return ApCommand::SetWind;
    }
    if (apState == ApDisplayState::Wind) {
      return ApCommand::SetAuto;
    }
    return ApCommand::None;
  }
  return ApCommand::None;
}

int AutopilotWatchy::consumePendingWakeHeadingDelta() {
  const int delta = (int)pendingWakeHeadingDelta;
  pendingWakeHeadingDelta = 0;
  return delta;
}

int AutopilotWatchy::collectWakeHeadingDeltaImpl(uint64_t btnMask,
                                                 int initialDelta,
                                                 uint16_t windowMs) {
  const bool up = (btnMask == AP_UP_BTN_MASK);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);

  if (initialDelta == 10 || initialDelta == -10) {
    return initialDelta;
  }

  int count = (initialDelta != 0) ? abs(initialDelta) : 0;

  if (headingBtnPressed(btnMask)) {
    unsigned long heldMs = 0;
    while (headingBtnPressed(btnMask)) {
      delay(10);
      heldMs += 10;
      if (heldMs >= BTN_HOLD_ADJUST_MS) {
        while (headingBtnPressed(btnMask)) {
          delay(5);
        }
        return up ? 10 : -10;
      }
    }
    if (count == 0) {
      count = 1;
    } else {
      count++;
    }
  } else if (count == 0) {
    count = 1;
  }

  unsigned long deadline = millis() + windowMs;
  while ((long)(deadline - millis()) > 0) {
    if (headingBtnPressed(btnMask)) {
      unsigned long heldMs = 0;
      while (headingBtnPressed(btnMask)) {
        delay(10);
        heldMs += 10;
        if (heldMs >= BTN_HOLD_ADJUST_MS) {
          while (headingBtnPressed(btnMask)) {
            delay(5);
          }
          return up ? 10 : -10;
        }
      }
      count++;
      deadline = millis() + windowMs;
    }
    delay(5);
  }

  return up ? count : -count;
}

const char *AutopilotWatchy::btnName(uint64_t mask) {
  if (mask == AP_MENU_BTN_MASK) {
    return "SELECT";
  }
  if (mask == AP_UP_BTN_MASK) {
    return "UP";
  }
  if (mask == AP_DOWN_BTN_MASK) {
    return "DOWN";
  }
  if (mask == AP_BACK_BTN_MASK) {
    return "BACK";
  }
  return "?";
}

void AutopilotWatchy::logState(const char *tag) {
#if AP_DEBUG
  apLogPower(tag);
  const int hdg = (int)roundf(apNormalizeHeadingDeg(targetHeadingDeg));
  AP_LOG("%s state=%s target=%03d valid=%d sk=%d", tag, stateLabel(), hdg,
         (int)targetValid, (int)skLinked);
#endif
}

void AutopilotWatchy::formatHeading(float deg, char *buf, size_t len) const {
  const int rounded = (int)roundf(apNormalizeHeadingDeg(deg));
  snprintf(buf, len, "%03d", rounded);
}

const char *AutopilotWatchy::stateLabel() const {
  switch (apState) {
  case ApDisplayState::Standby:
    return "STANDBY";
  case ApDisplayState::Auto:
    return "AUTO";
  case ApDisplayState::Wind:
    return "WIND";
  case ApDisplayState::Track:
    return "TRACK";
  default:
    return "UNKNOWN";
  }
}

void AutopilotWatchy::drawClock() {
  RTC.read(currentTime);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(4, 18);
  if (currentTime.Hour < 10) {
    display.print('0');
  }
  display.print(currentTime.Hour);
  display.print(':');
  if (currentTime.Minute < 10) {
    display.print('0');
  }
  display.print(currentTime.Minute);
}

void AutopilotWatchy::drawBattery() {
  const float vbat = getBatteryVoltage();
  int pct = (int)roundf((vbat - 3.30f) / (4.20f - 3.30f) * 100.0f);
  if (pct < 0) {
    pct = 0;
  } else if (pct > 100) {
    pct = 100;
  }

  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);

  display.setFont(&FreeMonoBold9pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(pctBuf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(196 - (int)w, 18);
  display.print(pctBuf);
}

void AutopilotWatchy::drawWatchFace() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  drawClock();
  drawBattery();

  char buf[16];

  if (apState == ApDisplayState::Wind) {
    if (windValid) {
      const int wind = (int)roundf(windAngleDeg);
      snprintf(buf, sizeof(buf), "%03d", (int)abs(wind) % 1000);
    } else {
      snprintf(buf, sizeof(buf), "---");
    }
  } else if (targetValid) {
    formatHeading(targetHeadingDeg, buf, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "---");
  }

  display.setFont(&DSEG7_Classic_Bold_53);
  display.setCursor(20, 85);
  display.print(buf);

  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(70, 130);
  display.print(stateLabel());

  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(5, 190);
#if SIM_MODE
  display.print("SIM");
#else
  if (skLinked && profileLabel[0] != '\0') {
    display.print(profileLabel);
  } else {
    display.print("NO SK");
  }
#endif
}

bool AutopilotWatchy::isPressed(uint64_t btnMask) {
  if (btnMask == AP_MENU_BTN_MASK) {
    return digitalRead(MENU_BTN_PIN) == BTN_ACTIVE;
  }
  if (btnMask == AP_UP_BTN_MASK) {
    return digitalRead(UP_BTN_PIN) == BTN_ACTIVE;
  }
  if (btnMask == AP_DOWN_BTN_MASK) {
    return digitalRead(DOWN_BTN_PIN) == BTN_ACTIVE;
  }
  return false;
}

int AutopilotWatchy::countPresses(uint64_t btnMask, uint16_t windowMs,
                                  uint8_t maxPresses) {
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);

  while (isPressed(btnMask)) {
    delay(5);
  }

  int presses = 1;
  unsigned long deadline = millis() + windowMs;

  while ((long)(deadline - millis()) > 0) {
    if (isPressed(btnMask)) {
      delay(BTN_DEBOUNCE_MS);
      while (isPressed(btnMask)) {
        delay(5);
      }
      presses++;
      if (maxPresses != 255 && presses >= maxPresses) {
        return presses;
      }
      deadline = millis() + windowMs;
    }
    delay(5);
  }
  return presses;
}

ApCommand AutopilotWatchy::resolveUpDown(bool up) {
  uint64_t mask = up ? AP_UP_BTN_MASK : AP_DOWN_BTN_MASK;
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);

  unsigned long heldMs = 0;
  while (isPressed(mask)) {
    delay(10);
    heldMs += 10;
    if (heldMs >= BTN_HOLD_ADJUST_MS) {
      while (isPressed(mask)) {
        delay(5);
      }
      return up ? ApCommand::AdjustPlus10 : ApCommand::AdjustMinus10;
    }
  }

  return up ? ApCommand::AdjustPlus1 : ApCommand::AdjustMinus1;
}

ApCommand AutopilotWatchy::resolveSelect(bool *menuLongHold) {
  if (menuLongHold) {
    *menuLongHold = false;
  }
  pinMode(MENU_BTN_PIN, INPUT);
  unsigned long heldMs = 0;

  while (isPressed(AP_MENU_BTN_MASK)) {
    delay(10);
    heldMs += 10;
    if (heldMs >= BTN_HOLD_STANDBY_MS) {
      if (menuLongHold) {
        *menuLongHold = true;
      }
      return menuCmdFromLongHold();
    }
  }

  const int presses =
      countPresses(AP_MENU_BTN_MASK, BTN_SELECT_MULTI_WINDOW_MS, 2);
  return menuCmdFromPressCount(presses);
}

int AutopilotWatchy::sessionCollectHeadingDelta(uint64_t btnMask) {
  const bool up = (btnMask == AP_UP_BTN_MASK);

  unsigned long heldMs = 0;
  while (isPressed(btnMask)) {
    delay(10);
    heldMs += 10;
    if (heldMs >= BTN_HOLD_ADJUST_MS) {
      while (isPressed(btnMask)) {
        delay(5);
      }
      return up ? 10 : -10;
    }
  }

  int count = 1;
  unsigned long deadline = millis() + BTN_SESSION_BURST_MS;
  while ((long)(deadline - millis()) > 0) {
    if (isPressed(btnMask)) {
      heldMs = 0;
      while (isPressed(btnMask)) {
        delay(10);
        heldMs += 10;
        if (heldMs >= BTN_HOLD_ADJUST_MS) {
          while (isPressed(btnMask)) {
            delay(5);
          }
          return up ? 10 : -10;
        }
      }
      count++;
      deadline = millis() + BTN_SESSION_BURST_MS;
    }
    delay(5);
  }

  return up ? count : -count;
}

int AutopilotWatchy::collectWakeHeadingDelta(uint64_t btnMask) {
  return collectWakeHeadingDeltaImpl(btnMask);
}

void AutopilotWatchy::handleSessionHeadingPress(uint64_t btnMask) {
  const int delta = sessionCollectHeadingDelta(btnMask);
  if (delta == 0) {
    return;
  }
  executeSessionHeadingDelta(delta);
}

bool AutopilotWatchy::executeSessionHeadingDelta(int delta) {
  if (apState == ApDisplayState::Standby) {
    AP_LOG("ignore heading adjust in STANDBY");
    return false;
  }
  if (delta == 0) {
    return false;
  }

  const int sign = delta > 0 ? 1 : -1;
  const bool isTen = (delta == 10 || delta == -10);
  const int steps = isTen ? 1 : (delta < 0 ? -delta : delta);
  if (steps > 5) {
    AP_LOG("ignore heading burst >5");
    return false;
  }

  bool anyOk = false;

  for (int i = 0; i < steps; i++) {
    const int putDeg = isTen ? delta : sign;
    AP_LOG("command %s%d (%d/%d)", putDeg > 0 ? "+" : "", putDeg, i + 1,
           steps);

#if SIM_MODE
    targetHeadingDeg += (float)putDeg;
    targetHeadingDeg = apNormalizeHeadingDeg(targetHeadingDeg);
    targetValid = true;
    anyOk = true;
#else
    if (!SignalKClient::putAdjustHeading(putDeg)) {
      AP_LOG("command failed");
      if (!anyOk) {
        refreshFromSignalK();
        logState("after");
        return false;
      }
      break;
    }
    targetHeadingDeg += (float)putDeg;
    targetHeadingDeg = apNormalizeHeadingDeg(targetHeadingDeg);
    targetValid = true;
    anyOk = true;
#endif
  }

  if (anyOk) {
    logState("after");
    if (isTen) {
      vibeConfirmPause();
      vibeLong();
    }
    sessionNeedsDisplay = true;
  }
  return anyOk;
}

uint64_t AutopilotWatchy::pollButtonDown(uint32_t timeoutMs) {
  unsigned long deadline = millis() + timeoutMs;
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);

  while ((long)(deadline - millis()) > 0) {
    if (isPressed(AP_UP_BTN_MASK)) {
      return AP_UP_BTN_MASK;
    }
    if (isPressed(AP_DOWN_BTN_MASK)) {
      return AP_DOWN_BTN_MASK;
    }
    if (isPressed(AP_MENU_BTN_MASK)) {
      return AP_MENU_BTN_MASK;
    }
    if (digitalRead(BACK_BTN_PIN) == BTN_ACTIVE) {
      return AP_BACK_BTN_MASK;
    }
    delay(5);
  }
  return 0;
}

void AutopilotWatchy::waitButtonsReleased() {
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  while (isPressed(AP_MENU_BTN_MASK) || isPressed(AP_UP_BTN_MASK) ||
         isPressed(AP_DOWN_BTN_MASK) ||
         digitalRead(BACK_BTN_PIN) == BTN_ACTIVE) {
    delay(5);
  }
  delay(BTN_DEBOUNCE_MS);
}

bool AutopilotWatchy::syncLiveDataBeforeDisplay() {
#if SIM_MODE
  return false;
#else
  const float prevTarget = targetHeadingDeg;
  const ApDisplayState prevState = apState;
  const bool prevSk = skLinked;
  const bool prevWindValid = windValid;
  const float prevWind = windAngleDeg;
  char prevProfile[8];
  strncpy(prevProfile, profileLabel, sizeof(prevProfile));
  prevProfile[sizeof(prevProfile) - 1] = '\0';

  apLogBattery("sk sync start");
  if (SignalKClient::ensureConnected()) {
    syncClockFromNtp();
    if (!refreshFromSignalK()) {
      AP_LOG("SK read failed");
    }
  } else {
    skLinked = false;
    profileLabel[0] = '\0';
    AP_LOG("WiFi connect failed");
    apLogPower("sk sync fail");
  }
  apLogPower("sk sync done");

  if (strcmp(prevProfile, profileLabel) != 0 || prevSk != skLinked ||
      prevState != apState || prevWindValid != windValid ||
      (windValid && prevWind != windAngleDeg) ||
      (targetValid && prevTarget != targetHeadingDeg)) {
    return true;
  }
  return false;
#endif
}

bool AutopilotWatchy::displayNeedsRecovery() const {
#if SIM_MODE
  (void)this;
  return false;
#else
  if (lastWifiRssiDbm <= WIFI_DISPLAY_RSSI_WEAK_DBM) {
    return true;
  }
  return apReadBatteryV() < WIFI_DISPLAY_VBAT_WEAK_V;
#endif
}

uint16_t AutopilotWatchy::displaySettleMs() const {
  return displayNeedsRecovery() ? WIFI_DISPLAY_SETTLE_WEAK_MS
                                : WIFI_DISPLAY_SETTLE_MS;
}

void AutopilotWatchy::paintWatchFaceFromCache() {
  display.epd2.setBusyCallback(0);
  display.epd2.initWatchyFull();
  display.setFullWindow();
  RTC.read(currentTime);
  const unsigned long t0 = millis();
  drawWatchFace();
  display.display(false);
  display.epd2.setBusyCallback(WatchyDisplay::busyCallback);
  guiState = WATCHFACE_STATE;
  AP_LOG("cache display %lums", millis() - t0);
}

void AutopilotWatchy::paintWatchFaceFull(bool reinitPanel) {
  (void)reinitPanel;
  display.epd2.setBusyCallback(0);
  if (displayNeedsRecovery()) {
    AP_LOG("display weak recovery rssi=%d", (int)lastWifiRssiDbm);
  }
  display.epd2.initWatchyFull();
  display.setFullWindow();
  RTC.read(currentTime);
  const unsigned long t0 = millis();
  drawWatchFace();
  display.display(false);
  display.epd2.setBusyCallback(WatchyDisplay::busyCallback);
  guiState = WATCHFACE_STATE;
  AP_LOG("display refresh %lums", millis() - t0);
  apLogBattery("display done");
}

void AutopilotWatchy::disconnectWifiBeforeDisplay() {
  gDisplayWifiSession = false;
#if !SIM_MODE
  lastWifiRssiDbm = -127;
  if (WiFi.status() == WL_CONNECTED) {
    lastWifiRssiDbm = (int8_t)WiFi.RSSI();
  }
  apLogPower("pre display wifi off");
  SignalKClient::markWifiStale();
  SignalKClient::disconnect();
  const uint16_t settleMs = displaySettleMs();
  AP_LOG("display settle %ums rssi=%d recovery=%d", settleMs,
         (int)lastWifiRssiDbm, (int)displayNeedsRecovery());
  delay(settleMs);
  apLogBattery("post settle");
#endif
}

void AutopilotWatchy::refreshDisplaySafe() {
  delay(VIB_POST_MS);
  disconnectWifiBeforeDisplay();
  RTC.read(currentTime);
  AP_LOG("display %02d:%02d target=%03d state=%s sk=%d", currentTime.Hour,
         currentTime.Minute, (int)roundf(targetHeadingDeg), stateLabel(),
         (int)skLinked);
  paintWatchFaceFull(true);
  AP_LOG("display done");
}

void AutopilotWatchy::bootSyncBeforeDisplay() {
#if !SIM_MODE
  skLinked = false;
  profileLabel[0] = '\0';
  AP_LOG("boot live sync");
  syncLiveDataBeforeDisplay();
  disconnectWifiBeforeDisplay();
#else
  paintWatchFaceFull(true);
  return;
#endif
  paintWatchFaceFull(true);
}

void AutopilotWatchy::syncClockFromNtp() {
#if !SIM_MODE
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (syncNTP(settings.gmtOffset)) {
    AP_LOG("NTP sync ok");
  } else {
    AP_LOG("NTP sync failed");
  }
#endif
}

void AutopilotWatchy::finishSessionWithDisplay() {
  if (!sessionNeedsDisplay) {
    disconnectBeforeSleep();
    return;
  }
  sessionNeedsDisplay = false;
  AP_LOG("session display");
#if !SIM_MODE
  syncLiveDataBeforeDisplay();
#endif
  refreshDisplaySafe();
  disconnectBeforeSleep();
}

void AutopilotWatchy::disconnectBeforeSleep() {
#if !SIM_MODE
  gDisplayWifiSession = false;
  SignalKClient::disconnect();
#endif
}

void AutopilotWatchy::runActiveSession() {
  apLogPower("session start");
  AP_LOG("session start %ds", ACTIVE_SESSION_MS / 1000);
  detachInterrupt(digitalPinToInterrupt(UP_BTN_PIN));
  detachInterrupt(digitalPinToInterrupt(DOWN_BTN_PIN));
  activeSession = true;
#if !SIM_MODE
  if (!SignalKClient::ensureConnected()) {
    skLinked = false;
    AP_LOG("session wifi reconnect failed");
  }
#endif
  gDisplayWifiSession = true;
  SignalKClient::setSessionMode(true);

  unsigned long sessionEnd = millis() + ACTIVE_SESSION_MS;

  RTC.read(currentTime);
  uint8_t lastClockMinute = currentTime.Minute;

  while ((long)(sessionEnd - millis()) > 0) {
    RTC.read(currentTime);
    if (currentTime.Minute != lastClockMinute) {
      lastClockMinute = currentTime.Minute;
      AP_LOG("clock tick %02d:%02d", currentTime.Hour, currentTime.Minute);
    }

    uint64_t remaining = sessionEnd - millis();
    uint32_t slice = remaining > 50 ? 50 : (uint32_t)remaining;
    uint64_t btn = pollButtonDown(slice);

    if (btn == 0) {
      continue;
    }
    if (btn == AP_BACK_BTN_MASK) {
      while (digitalRead(BACK_BTN_PIN) == BTN_ACTIVE) {
        delay(5);
      }
      break;
    }

    if (btn == AP_UP_BTN_MASK || btn == AP_DOWN_BTN_MASK) {
      handleSessionHeadingPress(btn);
      sessionEnd = millis() + ACTIVE_SESSION_MS;
      continue;
    }

    bool menuLongHold = false;
    ApCommand cmd = ApCommand::None;
    if (btn == AP_MENU_BTN_MASK) {
      cmd = resolveSelect(&menuLongHold);
    } else {
      cmd = resolveCommand(btn);
    }
    if (cmd != ApCommand::None) {
      const bool longHoldCmd = menuLongHold && isModeCommand(cmd);
      executeSessionCommand(cmd, menuLongHold);
      if (longHoldCmd) {
        waitButtonsReleased();
      }
      sessionEnd = millis() + ACTIVE_SESSION_MS;
    }
  }

  AP_LOG("session end");
  apLogPower("session end");
  gDisplayWifiSession = false;
  SignalKClient::setSessionMode(false);
  activeSession = false;
}

ApCommand AutopilotWatchy::resolveCommand(uint64_t wakeupBit) {
  if (wakeupBit & AP_UP_BTN_MASK) {
    return resolveUpDown(true);
  }
  if (wakeupBit & AP_DOWN_BTN_MASK) {
    return resolveUpDown(false);
  }
  if (wakeupBit & AP_MENU_BTN_MASK) {
    return resolveSelect();
  }
  return ApCommand::None;
}

void AutopilotWatchy::vibePulse(uint16_t onMs) {
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  digitalWrite(VIB_MOTOR_PIN, HIGH);
  delay(onMs);
  digitalWrite(VIB_MOTOR_PIN, LOW);
}

void AutopilotWatchy::vibeWake() {
  vibePulse(VIB_WAKE_MS);
}

void AutopilotWatchy::vibeConfirmPause() {
  delay(VIB_CONFIRM_DELAY_MS);
}

void AutopilotWatchy::vibeSingle() {
  vibePulse(VIB_SINGLE_MS);
}

void AutopilotWatchy::vibeDouble() {
  vibePulse(VIB_DOUBLE_MS);
  delay(VIB_GAP_MS);
  vibePulse(VIB_DOUBLE_MS);
}

void AutopilotWatchy::vibeLong() {
  vibePulse(VIB_LONG_MS);
}

void AutopilotWatchy::vibeExtraLong() {
  vibePulse(VIB_EXTRA_LONG_MS);
}

bool AutopilotWatchy::isModeCommand(ApCommand cmd) {
  return cmd == ApCommand::SetAuto || cmd == ApCommand::SetStandby ||
         cmd == ApCommand::SetWind;
}

void AutopilotWatchy::vibeAfterCommand(ApCommand cmd, bool menuLongHold) {
  vibeConfirmPause();
  switch (cmd) {
  case ApCommand::SetStandby:
    vibeExtraLong();
    break;
  case ApCommand::SetAuto:
    if (menuLongHold) {
      vibeExtraLong();
    } else {
      vibeDouble();
    }
    break;
  case ApCommand::SetWind:
    vibeDouble();
    break;
  case ApCommand::AdjustPlus10:
  case ApCommand::AdjustMinus10:
    vibeLong();
    break;
  case ApCommand::AdjustPlus1:
  case ApCommand::AdjustMinus1:
    vibeSingle();
    break;
  default:
    break;
  }
}

void AutopilotWatchy::applySkSnapshot(const SkAutopilotSnapshot &snap) {
  if (!snap.ok) {
    return;
  }

  strncpy(profileLabel, snap.profileLabel, sizeof(profileLabel) - 1);
  profileLabel[sizeof(profileLabel) - 1] = '\0';

  if (strcmp(snap.state, "standby") == 0) {
    apState = ApDisplayState::Standby;
  } else if (strcmp(snap.state, "auto") == 0) {
    apState = ApDisplayState::Auto;
    targetValid = true;
  } else if (strcmp(snap.state, "wind") == 0) {
    apState = ApDisplayState::Wind;
    targetValid = false;
    if (snap.windValid) {
      windAngleDeg = snap.windAngleDeg;
      windValid = true;
    } else {
      windValid = false;
    }
  } else {
    apState = ApDisplayState::Unknown;
  }

  if (snap.targetValid && apState != ApDisplayState::Wind) {
    targetHeadingDeg = apNormalizeHeadingDeg(snap.targetHeadingDeg);
    targetValid = true;
  } else {
    targetValid = false;
  }
}

bool AutopilotWatchy::refreshFromSignalK() {
  SkAutopilotSnapshot snap;
  if (!SignalKClient::readAutopilot(snap)) {
    skLinked = false;
    AP_LOG("SK read failed");
    return false;
  }
  applySkSnapshot(snap);
  skLinked = true;
  return true;
}

bool AutopilotWatchy::putCommand(ApCommand cmd) {
  switch (cmd) {
  case ApCommand::AdjustPlus1:
  case ApCommand::AdjustMinus1:
  case ApCommand::AdjustPlus10:
  case ApCommand::AdjustMinus10:
    if (apState == ApDisplayState::Standby) {
      AP_LOG("ignore heading adjust in STANDBY");
      return false;
    }
    break;
  default:
    break;
  }

  switch (cmd) {
  case ApCommand::AdjustPlus1:
    return SignalKClient::putAdjustHeading(1);
  case ApCommand::AdjustMinus1:
    return SignalKClient::putAdjustHeading(-1);
  case ApCommand::AdjustPlus10:
    return SignalKClient::putAdjustHeading(10);
  case ApCommand::AdjustMinus10:
    return SignalKClient::putAdjustHeading(-10);
  case ApCommand::SetAuto:
    return SignalKClient::putAutopilotState("auto");
  case ApCommand::SetStandby:
    return SignalKClient::putAutopilotState("standby");
  case ApCommand::SetWind:
    return SignalKClient::putAutopilotState("wind");
  default:
    return false;
  }
}

bool AutopilotWatchy::liveApply(ApCommand cmd) {
  const bool ok = putCommand(cmd);
  if (ok && !refreshFromSignalK()) {
    AP_LOG("SK read after PUT failed");
  }
  return ok;
}

void AutopilotWatchy::simApply(ApCommand cmd) {
  switch (cmd) {
  case ApCommand::AdjustPlus1:
  case ApCommand::AdjustMinus1:
  case ApCommand::AdjustPlus10:
  case ApCommand::AdjustMinus10:
    if (apState == ApDisplayState::Standby) {
      AP_LOG("ignore heading adjust in STANDBY");
      return;
    }
    break;
  default:
    break;
  }

  switch (cmd) {
  case ApCommand::AdjustPlus1:
    targetHeadingDeg += 1.0f;
    break;
  case ApCommand::AdjustMinus1:
    targetHeadingDeg -= 1.0f;
    break;
  case ApCommand::AdjustPlus10:
    targetHeadingDeg += 10.0f;
    break;
  case ApCommand::AdjustMinus10:
    targetHeadingDeg -= 10.0f;
    break;
  case ApCommand::SetAuto:
    apState = ApDisplayState::Auto;
    targetValid = true;
    break;
  case ApCommand::SetStandby:
    apState = ApDisplayState::Standby;
    break;
  case ApCommand::SetWind:
    apState = ApDisplayState::Wind;
    targetValid = false;
    break;
  default:
    break;
  }

  if (targetHeadingDeg >= 360.0f || targetHeadingDeg < 0.0f) {
    targetHeadingDeg = apNormalizeHeadingDeg(targetHeadingDeg);
  }
}

void AutopilotWatchy::executeCommand(ApCommand cmd, bool menuLongHold) {
  if (cmd == ApCommand::None) {
    return;
  }

  const char *name = "?";
  switch (cmd) {
  case ApCommand::AdjustPlus1:
    name = "+1";
    break;
  case ApCommand::AdjustMinus1:
    name = "-1";
    break;
  case ApCommand::AdjustPlus10:
    name = "+10";
    break;
  case ApCommand::AdjustMinus10:
    name = "-10";
    break;
  case ApCommand::SetAuto:
    name = "AUTO";
    break;
  case ApCommand::SetStandby:
    name = "STANDBY";
    break;
  case ApCommand::SetWind:
    name = "WIND";
    break;
  default:
    break;
  }

  AP_LOG("command %s", name);

#if SIM_MODE
  simApply(cmd);
  logState("after");
  if (cmd == ApCommand::AdjustPlus1 || cmd == ApCommand::AdjustMinus1 ||
      cmd == ApCommand::AdjustPlus10 || cmd == ApCommand::AdjustMinus10) {
    const int delta = (cmd == ApCommand::AdjustPlus10 ||
                       cmd == ApCommand::AdjustPlus1)
                          ? (cmd == ApCommand::AdjustPlus10 ? 10 : 1)
                          : (cmd == ApCommand::AdjustMinus10 ? -10 : -1);
    executeSessionHeadingDelta(delta);
  } else {
    vibeAfterCommand(cmd, menuLongHold);
    sessionNeedsDisplay = true;
  }
#else
  if (cmd == ApCommand::AdjustPlus1 || cmd == ApCommand::AdjustMinus1 ||
      cmd == ApCommand::AdjustPlus10 || cmd == ApCommand::AdjustMinus10) {
    const int delta = (cmd == ApCommand::AdjustPlus10 ||
                       cmd == ApCommand::AdjustPlus1)
                          ? (cmd == ApCommand::AdjustPlus10 ? 10 : 1)
                          : (cmd == ApCommand::AdjustMinus10 ? -10 : -1);
    executeSessionHeadingDelta(delta);
  } else {
    const bool ok = putCommand(cmd);
    if (ok) {
      if (!refreshFromSignalK()) {
        AP_LOG("SK read after PUT failed");
      }
      logState("after");
      vibeAfterCommand(cmd, menuLongHold);
      sessionNeedsDisplay = true;
    } else {
      AP_LOG("command failed — no vibration");
      refreshFromSignalK();
      logState("after");
    }
  }
#endif
}

void AutopilotWatchy::executeSessionCommand(ApCommand cmd, bool menuLongHold) {
  if (cmd == ApCommand::None) {
    return;
  }

  const char *name = "?";
  switch (cmd) {
  case ApCommand::AdjustPlus1:
    name = "+1";
    break;
  case ApCommand::AdjustMinus1:
    name = "-1";
    break;
  case ApCommand::AdjustPlus10:
    name = "+10";
    break;
  case ApCommand::AdjustMinus10:
    name = "-10";
    break;
  case ApCommand::SetAuto:
    name = "AUTO";
    break;
  case ApCommand::SetStandby:
    name = "STANDBY";
    break;
  case ApCommand::SetWind:
    name = "WIND";
    break;
  default:
    break;
  }

  AP_LOG("command %s", name);

#if SIM_MODE
  if (cmd == ApCommand::AdjustPlus1 || cmd == ApCommand::AdjustMinus1 ||
      cmd == ApCommand::AdjustPlus10 || cmd == ApCommand::AdjustMinus10) {
    const int delta = (cmd == ApCommand::AdjustPlus10 ||
                       cmd == ApCommand::AdjustPlus1)
                          ? (cmd == ApCommand::AdjustPlus10 ? 10 : 1)
                          : (cmd == ApCommand::AdjustMinus10 ? -10 : -1);
    executeSessionHeadingDelta(delta);
  } else {
    simApply(cmd);
    logState("after");
    vibeAfterCommand(cmd, menuLongHold);
    sessionNeedsDisplay = true;
  }
#else
  if (cmd == ApCommand::AdjustPlus1 || cmd == ApCommand::AdjustMinus1 ||
      cmd == ApCommand::AdjustPlus10 || cmd == ApCommand::AdjustMinus10) {
    const int delta = (cmd == ApCommand::AdjustPlus10 ||
                       cmd == ApCommand::AdjustPlus1)
                          ? (cmd == ApCommand::AdjustPlus10 ? 10 : 1)
                          : (cmd == ApCommand::AdjustMinus10 ? -10 : -1);
    executeSessionHeadingDelta(delta);
  } else {
    const bool ok = putCommand(cmd);
    if (ok) {
      if (!refreshFromSignalK()) {
        AP_LOG("SK read after PUT failed");
      }
      logState("after");
      vibeAfterCommand(cmd, menuLongHold);
      sessionNeedsDisplay = true;
    } else {
      AP_LOG("command failed — no vibration");
      refreshFromSignalK();
      logState("after");
    }
  }
#endif
}

void AutopilotWatchy::handleButtonPress() {
  uint64_t wakeupBit = esp_sleep_get_ext1_wakeup_status();

  // Back (uppe vänster på din klocka) — ingen autopilot-funktion, spara batteri
  if (wakeupBit == AP_BACK_BTN_MASK) {
    AP_LOG("wake BACK -> sleep");
    disconnectBeforeSleep();
    deepSleep();
    return;
  }

  AP_LOG("wake btn=%s mask=0x%lx", btnName(wakeupBit),
         (unsigned long)wakeupBit);
  apLogBattery("wake");

  if (wakeupBit == AP_MENU_BTN_MASK) {
    const int menuPresses = consumePendingWakeMenuPresses();
    bool menuLongHold = false;
    ApCommand cmd = ApCommand::None;
    if (menuPresses != 0) {
      menuLongHold = menuPresses < 0;
      cmd = menuCmdFromPressCount(menuPresses);
    } else {
      cmd = resolveSelect(&menuLongHold);
    }
    const bool longHoldCmd = menuLongHold && cmd != ApCommand::None;

    if (cmd == ApCommand::None) {
      vibeWake();
      paintWatchFaceFromCache();
      waitButtonsReleased();
      AP_LOG("wake screen");
#if !SIM_MODE
      if (syncLiveDataBeforeDisplay()) {
        refreshDisplaySafe();
      }
#endif
      disconnectBeforeSleep();
      deepSleep();
      return;
    }

    if (longHoldCmd) {
      AP_LOG("menu long hold %s", stateLabel());
#if !SIM_MODE
      if (!SignalKClient::ensureConnected()) {
        skLinked = false;
        profileLabel[0] = '\0';
        AP_LOG("WiFi connect failed");
      } else {
        syncClockFromNtp();
        refreshFromSignalK();
      }
#endif
      executeCommand(cmd, true);
      waitButtonsReleased();
      if (sessionNeedsDisplay) {
        sessionNeedsDisplay = false;
        refreshDisplaySafe();
      }
      disconnectBeforeSleep();
      deepSleep();
      return;
    }

    vibeWake();
    paintWatchFaceFromCache();
    waitButtonsReleased();
#if !SIM_MODE
    if (!SignalKClient::ensureConnected()) {
      skLinked = false;
      profileLabel[0] = '\0';
      AP_LOG("WiFi connect failed");
    } else {
      syncClockFromNtp();
      refreshFromSignalK();
    }
#endif
    executeCommand(cmd, menuLongHold);
    runActiveSession();
    finishSessionWithDisplay();
    return;
  }

  int wakeHeadingDelta = 0;
  if (wakeupBit == AP_UP_BTN_MASK || wakeupBit == AP_DOWN_BTN_MASK) {
    wakeHeadingDelta = consumePendingWakeHeadingDelta();
    if (abs(wakeHeadingDelta) != 10) {
      if (wakeHeadingDelta == 0) {
        wakeHeadingDelta = (wakeupBit == AP_UP_BTN_MASK) ? 1 : -1;
      }
      // Extra burst window — catches 2nd click during slow boot (no wake display).
      wakeHeadingDelta = collectWakeHeadingDeltaImpl(wakeupBit, wakeHeadingDelta,
                                                     BTN_WAKE_EXTEND_MS);
    }
    AP_LOG("wake clicks -> %s%d", wakeHeadingDelta > 0 ? "+" : "",
           wakeHeadingDelta);
  }

  if (wakeupBit == AP_UP_BTN_MASK || wakeupBit == AP_DOWN_BTN_MASK) {
#if !SIM_MODE
    if (!SignalKClient::ensureConnected()) {
      skLinked = false;
      profileLabel[0] = '\0';
      AP_LOG("WiFi connect failed");
    } else {
      syncClockFromNtp();
      refreshFromSignalK();
    }
#endif
    if (wakeHeadingDelta != 0) {
      executeSessionHeadingDelta(wakeHeadingDelta);
    }
    waitButtonsReleased();
    runActiveSession();
    finishSessionWithDisplay();
    return;
  }

  AP_LOG("no command");
}

#if AP_DEBUG
void apLogPrint(const char *fmt, ...) {
  tmElements_t tm;
  Watchy::RTC.read(tm);
  Serial.printf("%02d:%02d:%02d [AP] ", tm.Hour, tm.Minute, tm.Second);

  char buf[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
}
#endif
