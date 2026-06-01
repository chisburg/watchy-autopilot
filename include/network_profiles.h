#ifndef NETWORK_PROFILES_H
#define NETWORK_PROFILES_H

#include <stddef.h>
#include <stdint.h>

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#warning "Missing include/secrets.local.h — copy secrets.local.h.example"
#define SK_HOME_WIFI_PASS ""
#define SK_BOAT_WIFI_PASS ""
#endif

#ifndef SK_HOME_WIFI_PASS
#define SK_HOME_WIFI_PASS ""
#endif
#ifndef SK_BOAT_WIFI_PASS
#define SK_BOAT_WIFI_PASS ""
#endif
#ifndef SK_DEVICE_TOKEN
#define SK_DEVICE_TOKEN ""
#endif

// WiFi + Signal K profiles — visible SSID in scan wins (strongest RSSI).
// Passwords: include/secrets.local.h (gitignored)

struct SkProfile {
  const char *label;
  const char *ssid;
  const char *password;
  const char *skHost;
  uint16_t skPort;
};

static const SkProfile SK_PROFILES[] = {
    {
        "HOME",
        "8-)_IoT",
        SK_HOME_WIFI_PASS,
        "192.168.1.105",
        3000,
    },
    {
        "BOAT",
        "Jubilon J92 8-)",
        SK_BOAT_WIFI_PASS,
        "192.168.1.105",
        3000,
    },
};

static const size_t SK_PROFILE_COUNT =
    sizeof(SK_PROFILES) / sizeof(SK_PROFILES[0]);

#endif
