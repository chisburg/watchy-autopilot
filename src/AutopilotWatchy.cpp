#include "AutopilotWatchy.h"

#include <cmath>

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
RTC_DATA_ATTR ApDisplayState AutopilotWatchy::apState = ApDisplayState::Auto;
RTC_DATA_ATTR bool AutopilotWatchy::targetValid = true;
RTC_DATA_ATTR bool AutopilotWatchy::skLinked = true;

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
  AP_LOG("%s state=%s target=%.0f valid=%d sk=%d vbat=%.2f", tag,
         stateLabel(), targetHeadingDeg, (int)targetValid, (int)skLinked,
         getBatteryVoltage());
#endif
}

void AutopilotWatchy::formatHeading(float deg, char *buf, size_t len) const {
  int rounded = (int)roundf(deg) % 360;
  if (rounded < 0) {
    rounded += 360;
  }
  snprintf(buf, len, "%d°", rounded);
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

  if (targetValid && apState != ApDisplayState::Wind) {
    formatHeading(targetHeadingDeg, buf, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "---°");
  }

  display.setFont(&DSEG7_Classic_Bold_53);
  display.setCursor(20, 85);
  display.print(buf);

  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(70, 130);
  display.print(stateLabel());

#if SIM_MODE
  display.setCursor(5, 190);
  display.print("SIM");
#else
  if (!skLinked) {
    display.setCursor(5, 190);
    display.print("NO SK LINK");
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

ApCommand AutopilotWatchy::resolveSelect() {
  pinMode(MENU_BTN_PIN, INPUT);
  longVibeNext = false;
  unsigned long heldMs = 0;

  while (isPressed(AP_MENU_BTN_MASK)) {
    delay(10);
    heldMs += 10;
    if (heldMs >= BTN_HOLD_STANDBY_MS) {
      while (isPressed(AP_MENU_BTN_MASK)) {
        delay(5);
      }
      return ApCommand::SetStandby;
    }
  }

  int presses = countPresses(AP_MENU_BTN_MASK, BTN_SELECT_MULTI_WINDOW_MS, 2);
  if (presses >= 2) {
    longVibeNext = true;
    if (apState == ApDisplayState::Auto) {
      return ApCommand::SetWind;
    }
    return ApCommand::SetAuto;
  }
  if (presses == 1 && apState == ApDisplayState::Standby) {
    return ApCommand::SetAuto;
  }
  return ApCommand::None;
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

void AutopilotWatchy::runActiveSession() {
  AP_LOG("session start %ds", ACTIVE_SESSION_MS / 1000);
  unsigned long sessionEnd = millis() + ACTIVE_SESSION_MS;
  bool partial = true;

  while ((long)(sessionEnd - millis()) > 0) {
    uint64_t remaining = sessionEnd - millis();
    uint32_t slice = remaining > 200 ? 200 : (uint32_t)remaining;
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

    ApCommand cmd = resolveCommand(btn);
    if (cmd != ApCommand::None) {
      executeCommand(cmd);
      showWatchFace(partial);
      partial = true;
      sessionEnd = millis() + ACTIVE_SESSION_MS;
    }
  }
  AP_LOG("session end");
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

void AutopilotWatchy::vibeSingle() {
  vibePulse(VIB_SINGLE_MS);
}

void AutopilotWatchy::vibeDouble() {
  vibePulse(VIB_DOUBLE_MS);
  delay(VIB_GAP_MS);
  vibePulse(VIB_DOUBLE_MS);
}

bool AutopilotWatchy::isDoubleAction(ApCommand cmd) const {
  switch (cmd) {
  case ApCommand::AdjustPlus10:
  case ApCommand::AdjustMinus10:
  case ApCommand::SetStandby:
  case ApCommand::SetWind:
    return true;
  case ApCommand::SetAuto:
    return false;
  default:
    return false;
  }
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
    int wrapped = (int)roundf(targetHeadingDeg) % 360;
    if (wrapped < 0) {
      wrapped += 360;
    }
    targetHeadingDeg = (float)wrapped;
  }
}

void AutopilotWatchy::executeCommand(ApCommand cmd) {
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

  Serial.printf("[AP] command %s\n", name);

#if SIM_MODE
  simApply(cmd);
#else
  // Milestone 2+: WiFi wake, WebSocket PUT, wait for COMPLETED
#endif

  logState("after");

  if (longVibeNext || isDoubleAction(cmd)) {
    vibeDouble();
  } else {
    vibeSingle();
  }
  longVibeNext = false;
}

void AutopilotWatchy::handleButtonPress() {
  uint64_t wakeupBit = esp_sleep_get_ext1_wakeup_status();

  // Back (uppe vänster på din klocka) — ingen autopilot-funktion, spara batteri
  if (wakeupBit == AP_BACK_BTN_MASK) {
    AP_LOG("wake BACK -> sleep");
    deepSleep();
    return;
  }

  AP_LOG("wake btn=%s mask=0x%llx", btnName(wakeupBit),
         (unsigned long long)wakeupBit);

  ApCommand cmd = resolveCommand(wakeupBit);
  executeCommand(cmd);

  if (cmd != ApCommand::None) {
    showWatchFace(false);
    runActiveSession();
  } else {
    AP_LOG("no command");
  }
}
