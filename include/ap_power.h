#ifndef AP_POWER_H
#define AP_POWER_H

#include "config.h"

#if AP_DEBUG

// Battery % from LiPo voltage (same range as watchface).
int apBatteryPercent(float voltage);
float apReadBatteryV();

// [PWR] tag vbat=… pct=… [rssi=… wifi=…] [extra_ms=…]
void apLogBattery(const char *tag);
void apLogPower(const char *tag);

#else

inline int apBatteryPercent(float) { return 0; }
inline float apReadBatteryV() { return 0.0f; }
inline void apLogBattery(const char *) {}
inline void apLogPower(const char *) {}

#endif

#endif
