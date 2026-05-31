#ifndef AUTOPILOT_WATCHY_H
#define AUTOPILOT_WATCHY_H

#include "Watchy.h"
#include "config.h"

enum class ApDisplayState : uint8_t {
  Standby,
  Auto,
  Wind,
  Track,
  Unknown,
};

enum class ApCommand : uint8_t {
  None,
  AdjustPlus1,
  AdjustMinus1,
  AdjustPlus10,
  AdjustMinus10,
  SetAuto,
  SetStandby,
  SetWind,
};

class AutopilotWatchy : public Watchy {
  using Watchy::Watchy;

public:
  void drawWatchFace() override;
  void handleButtonPress() override;

private:
  static RTC_DATA_ATTR float targetHeadingDeg;
  static RTC_DATA_ATTR ApDisplayState apState;
  static RTC_DATA_ATTR bool targetValid;
  static RTC_DATA_ATTR   bool skLinked;

  bool longVibeNext = false;

  void executeCommand(ApCommand cmd);
  void runActiveSession();
  uint64_t pollButtonDown(uint32_t timeoutMs);
  ApCommand resolveCommand(uint64_t wakeupBit);
  ApCommand resolveUpDown(bool up);
  ApCommand resolveSelect();
  int countPresses(uint64_t btnMask, uint16_t windowMs, uint8_t maxPresses = 255);
  bool isPressed(uint64_t btnMask);
  void vibePulse(uint16_t onMs);
  void vibeSingle();
  void vibeDouble();
  bool isDoubleAction(ApCommand cmd) const;
  const char *stateLabel() const;
  void drawClock();
  void drawBattery();
  void formatHeading(float deg, char *buf, size_t len) const;
  void simApply(ApCommand cmd);
  void logState(const char *tag);
  static const char *btnName(uint64_t mask);
};

#endif
