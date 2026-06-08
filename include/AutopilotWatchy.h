#ifndef AUTOPILOT_WATCHY_H
#define AUTOPILOT_WATCHY_H

#include "Watchy.h"
#include "ap_command.h"
#include "config.h"
#include "signalk_client.h"

enum class ApDisplayState : uint8_t {
  Standby,
  Auto,
  Wind,
  Track,
  Unknown,
};

class AutopilotWatchy : public Watchy {
  using Watchy::Watchy;

public:
  void drawWatchFace() override;
  void handleButtonPress() override;

  // Call at very start of setup() before Serial/WiFi — catches fast multi-click wake.
  static void captureWakeHeadingDeltaEarly();
  static int consumePendingWakeHeadingDelta();
  static void captureWakeMenuPressesEarly();
  static int consumePendingWakeMenuPresses();
  static ApCommand menuCmdFromLongHold();
  static ApCommand menuCmdFromPressCount(int presses);

  // Reset boot: WiFi + Signal K read, then full watch face.
  void bootSyncBeforeDisplay();

private:
  static RTC_DATA_ATTR int8_t pendingWakeHeadingDelta;
  static RTC_DATA_ATTR int8_t pendingWakeMenuPresses;
  static RTC_DATA_ATTR float targetHeadingDeg;
  static RTC_DATA_ATTR float windAngleDeg;
  static RTC_DATA_ATTR ApDisplayState apState;
  static RTC_DATA_ATTR bool targetValid;
  static RTC_DATA_ATTR bool windValid;
  static RTC_DATA_ATTR bool skLinked;
  static RTC_DATA_ATTR char profileLabel[8];

  bool activeSession = false;
  bool sessionNeedsDisplay = false;
  int8_t lastWifiRssiDbm = -127;

  bool refreshFromSignalK();
  void applySkSnapshot(const SkAutopilotSnapshot &snap);
  bool liveApply(ApCommand cmd);
  bool putCommand(ApCommand cmd);
  void executeCommand(ApCommand cmd, bool menuLongHold = false);
  void executeSessionCommand(ApCommand cmd, bool menuLongHold = false);
  int sessionCollectHeadingDelta(uint64_t btnMask);
  int collectWakeHeadingDelta(uint64_t btnMask);
  static int collectWakeHeadingDeltaImpl(uint64_t btnMask, int initialDelta = 0,
                                         uint16_t windowMs = BTN_WAKE_BURST_MS);
  void handleSessionHeadingPress(uint64_t btnMask);
  bool executeSessionHeadingDelta(int delta);
  void waitButtonsReleased();
  void paintWatchFaceFull(bool reinitPanel);
  void paintWatchFaceFromCache();
  void disconnectWifiBeforeDisplay();
  bool displayNeedsRecovery() const;
  uint16_t displaySettleMs() const;
  void refreshDisplaySafe();
  bool syncLiveDataBeforeDisplay();
  void syncClockFromNtp();
  void finishSessionWithDisplay();
  void disconnectBeforeSleep();
  void runActiveSession();
  uint64_t pollButtonDown(uint32_t timeoutMs);
  ApCommand resolveCommand(uint64_t wakeupBit);
  ApCommand resolveUpDown(bool up);
  ApCommand resolveSelect(bool *menuLongHold = nullptr);
  int countPresses(uint64_t btnMask, uint16_t windowMs, uint8_t maxPresses = 255);
  bool isPressed(uint64_t btnMask);
  void vibePulse(uint16_t onMs);
  void vibeWake();
  void vibeConfirmPause();
  void vibeSingle();
  void vibeDouble();
  void vibeLong();
  void vibeExtraLong();
  void vibeAfterCommand(ApCommand cmd, bool menuLongHold = false);
  static bool isModeCommand(ApCommand cmd);
  const char *stateLabel() const;
  void drawClock();
  void drawBattery();
  void formatHeading(float deg, char *buf, size_t len) const;
  void simApply(ApCommand cmd);
  void logState(const char *tag);
  static const char *btnName(uint64_t mask);
};

#endif
