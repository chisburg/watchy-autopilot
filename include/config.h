#ifndef AUTOPILOT_CONFIG_H
#define AUTOPILOT_CONFIG_H

#include <math.h>

#ifndef SIM_MODE
#define SIM_MODE 1
#endif

#ifndef AP_DEBUG
#define AP_DEBUG 1
#endif

// 1 = black/TEST diagnostic pattern. Keep 0 for normal watchface.
#ifndef AP_DISPLAY_TEST
#define AP_DISPLAY_TEST 0
#endif

#if AP_DEBUG
void apLogPrint(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define AP_LOG(fmt, ...) apLogPrint(fmt "\n", ##__VA_ARGS__)
#else
#define AP_LOG(fmt, ...) ((void)0)
#endif

// Wrap heading to 0–359° (Signal K may return 365 etc.)
inline float apNormalizeHeadingDeg(float deg) {
  float wrapped = fmodf(deg, 360.0f);
  if (wrapped < 0.0f) {
    wrapped += 360.0f;
  }
  return wrapped;
}

// WiFi profiles: include/network_profiles.h (HOME 8-)_IoT, BOAT Jubilon J92 8-))

// UI timing
#define SK_LINK_TIMEOUT_MS 3000
#define SK_PUT_TIMEOUT_MS 8000
// Initial PUT HTTP timeout (session = button burst; must allow 202 before poll).
#define SK_PUT_SESSION_TIMEOUT_MS 12000
// Poll async Signal K requests after HTTP 202 PENDING (Pi ~1 s; boat up to ~15 s).
#define SK_PUT_POLL_INTERVAL_MS 600
#define SK_PUT_POLL_TIMEOUT_MS 15000
#define SK_PUT_POLL_TIMEOUT_SESSION_MS 15000
// Max HTTP body for PUT 202 / request poll (~278 B on Pi; avoid heap String+JSONVar).
#define SK_HTTP_BODY_MAX 512
// GET vessels.self/steering/autopilot (~1–2 KB on boat; stack buffer, no JSON.parse).
#define SK_GET_BODY_MAX 3072
#define WIFI_CONNECT_MS 15000
#define WIFI_RECOVER_MS 2500
#define WIFI_WARM_SEC 45
// Pause after WiFi off before full e-paper (boat AP: weak RSSI → longer radio burst).
#define WIFI_DISPLAY_SETTLE_MS 1000

// MENU: enkel = wake, dubbel = AUTO↔WIND, håll = STANDBY↔AUTO/WIND→STANDBY
#define BTN_HOLD_ADJUST_MS 650
#define BTN_SELECT_MULTI_WINDOW_MS 550
#define BTN_HOLD_STANDBY_MS 1500
#define BTN_DEBOUNCE_MS 30
#define BTN_SESSION_BURST_MS 400
#define BTN_WAKE_BURST_MS 800
#define BTN_WAKE_EXTEND_MS 500
#define POST_PUT_COLLECT_MS 250
#define SESSION_DISPLAY_MIN_MS 1500
#define BATCH_DISPLAY_IDLE_MS 3000
#define WIFI_SESSION_WARM_MS 3000
#define ACTIVE_SESSION_MS 20000

// Vibration: kort = ±1, lång ×1/2/3 = STANDBY/AUTO/WIND, dubbel kort = ±10
#define VIB_CONFIRM_DELAY_MS 100
#define VIB_SINGLE_MS 200
#define VIB_DOUBLE_MS 450
#define VIB_GAP_MS 80

#endif
