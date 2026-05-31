#ifndef AUTOPILOT_CONFIG_H
#define AUTOPILOT_CONFIG_H

#ifndef SIM_MODE
#define SIM_MODE 1
#endif

#ifndef AP_DEBUG
#define AP_DEBUG 1
#endif

#if AP_DEBUG
#define AP_LOG(fmt, ...) Serial.printf("[AP] " fmt "\n", ##__VA_ARGS__)
#else
#define AP_LOG(fmt, ...) ((void)0)
#endif

// Signal K (Milestone 2+)
#define SK_HOST "192.168.1.105"
#define SK_PORT 3000
#define SK_WIFI_SSID "Jubilon J92 8-)"
#define SK_WIFI_PASS ""

// UI timing
#define SK_LINK_TIMEOUT_MS 3000
#define WIFI_WARM_SEC 45

// SELECT: enkel = STANDBY→AUTO, håll = STANDBY, dubbel = AUTO↔WIND
#define BTN_HOLD_ADJUST_MS 650
#define BTN_SELECT_MULTI_WINDOW_MS 550
#define BTN_HOLD_STANDBY_MS 1500
#define BTN_DEBOUNCE_MS 30
#define ACTIVE_SESSION_MS 20000

// Vibration: kort = ±1, dubbel = ±10 / lägesbyte
#define VIB_SINGLE_MS 200
#define VIB_DOUBLE_MS 450
#define VIB_GAP_MS 80

#endif
