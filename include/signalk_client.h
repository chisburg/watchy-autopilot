#ifndef SIGNALK_CLIENT_H
#define SIGNALK_CLIENT_H

#include "network_profiles.h"

#include <stdint.h>

struct SkAutopilotSnapshot {
  bool ok = false;
  const char *profileLabel = "";
  char state[16] = "";
  float targetHeadingDeg = 0.0f;
  bool targetValid = false;
  float windAngleDeg = 0.0f;
  bool windValid = false;
};

class SignalKClient {
public:
  // Cached profile WiFi connect (no scan). Returns false if no network.
  static bool ensureConnected();

  static void disconnect();
  static bool isWifiConnected();
  static const char *connectedProfileLabel();

  // Call after e-paper refresh — next PUT will reconnect if needed.
  static void markWifiStale();

  // Reconnect after e-paper refresh (full refresh can drop WiFi).
  static void recoverWifi();

  // HTTP GET vessels.self autopilot fields.
  static bool readAutopilot(SkAutopilotSnapshot &out);

  // HTTP PUT — waits for COMPLETED 200 (polls after HTTP 202 PENDING). Requires SK_DEVICE_TOKEN.
  static bool putAdjustHeading(int degrees);
  static bool putAutopilotState(const char *state);

  // Shorter HTTP timeouts during 20s button session (fast repeat presses).
  static void setSessionMode(bool active);

  // Reconnect quickly after e-paper refresh (before session loop).
  static void warmAfterDisplay();

private:
  static bool connectProfile(const SkProfile &profile);
  static bool connectBestProfile();
  static bool fetchAutopilot(const SkProfile &profile, SkAutopilotSnapshot &out);
};

#endif
